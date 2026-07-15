#include "mix/Dm7Link.h"

#include <QTcpSocket>
#include <QTimer>

namespace quewi::mix {
namespace {

// Yamaha's keepalive: interval in ms, must be > 1000; the console's actual
// timeout is this + 1 s. Once armed, we must send SOMETHING within the window
// or the console drops us.
//
// Companion defaults this OFF with a warning. For an unattended show tool we
// want it ON, and Yamaha's own rationale says why: if a client dies without
// closing cleanly, the console keeps believing it's connected and REFUSES NEW
// CONNECTIONS until it notices. That's "quewi crashed and now won't reconnect
// during the interval" — exactly the failure we can't have mid-show.
constexpr int kKeepaliveMs   = 10000;
constexpr int kHeartbeatMs   = 4000;   // comfortably inside the window

} // namespace

Dm7Link::Dm7Link(QObject *parent) : ConsoleLink(parent) {}
Dm7Link::~Dm7Link() = default;

void Dm7Link::connectToConsole(const QString &host, quint16 port)
{
    disconnectFromConsole();

    m_sock = std::make_unique<QTcpSocket>(this);
    connect(m_sock.get(), &QTcpSocket::readyRead, this, &Dm7Link::onReadyRead);
    connect(m_sock.get(), &QTcpSocket::connected, this, &Dm7Link::onConnected);
    connect(m_sock.get(), &QTcpSocket::errorOccurred, this, &Dm7Link::onSocketError);

    setState(State::Connecting);
    m_sock->connectToHost(host, port ? port : defaultPort());
}

void Dm7Link::disconnectFromConsole()
{
    m_keepalive.reset();
    m_sock.reset();
    m_rxBuffer.clear();
    m_dcaCache.clear();
    m_split = false;
    setState(State::Disconnected);
}

void Dm7Link::onSocketError()
{
    if (!m_sock) return;
    setError(m_sock->errorString());
    setState(State::Failed);
}

void Dm7Link::send(const QString &line)
{
    if (!m_sock || m_sock->state() != QAbstractSocket::ConnectedState) return;
    m_sock->write(line.toUtf8() + '\n');   // LF, not CRLF
}

void Dm7Link::onConnected()
{
    // No handshake, no authentication — open the socket and start talking.
    send(QStringLiteral("devinfo productname"));
    send(QStringLiteral("devinfo version"));
    send(QStringLiteral("devinfo paramsetver"));
    send(QStringLiteral("devstatus runmode"));

    // Scene numbers on DM7 are strings ("4.00"), not integers, and the t-suffix
    // scene verbs misbehave without this. Must precede any scene work.
    send(QStringLiteral("scpmode sstype \"text\""));

    // Names may contain non-ASCII; without this they mangle. Companion never
    // sends it, which is why non-ASCII names are a known annoyance there.
    send(QStringLiteral("scpmode encoding utf8"));

    send(QStringLiteral("scpmode keepalive %1").arg(kKeepaliveMs));
    m_keepalive = std::make_unique<QTimer>(this);
    m_keepalive->setInterval(kHeartbeatMs);
    connect(m_keepalive.get(), &QTimer::timeout, this, &Dm7Link::onKeepaliveTick);
    m_keepalive->start();

    // NB: not Connected yet. We wait for devinfo productname, because until we
    // know DM7 vs DM7 Compact we don't know the channel count, and every
    // capability check downstream depends on it.
}

void Dm7Link::onKeepaliveTick()
{
    // Any command counts as a heartbeat; a bare LF would do. runmode is cheap
    // and its reply doubles as proof the link is still alive.
    send(QStringLiteral("devstatus runmode"));
}

void Dm7Link::onReadyRead()
{
    if (!m_sock) return;
    m_rxBuffer += QString::fromUtf8(m_sock->readAll());

    // TCP is a stream: a reply can arrive split across reads, and several can
    // arrive in one. Only consume complete LF-terminated lines.
    int newline = -1;
    while ((newline = m_rxBuffer.indexOf(u'\n')) >= 0) {
        const QString line = m_rxBuffer.left(newline);
        m_rxBuffer.remove(0, newline + 1);
        if (const auto reply = dm7::parseReply(line))
            handleReply(*reply);
        // Unparseable lines are ignored, not fatal.
    }
}

void Dm7Link::applyModelCapabilities(const QString &productName)
{
    Capabilities caps;
    caps.model    = productName;
    caps.firmware = m_firmware;

    // DM7 and DM7 Compact differ ONLY in input count. DCAs, mute groups and
    // buses are identical, so the core logic is model-independent.
    //
    // Don't hardcode 120: on a Compact, channels 73-120 don't exist, and the
    // community module gets this wrong (its changelog shows it flip-flopped
    // between 72 and 120 for all DM7s).
    caps.channelCount = productName.contains(QLatin1String("Compact"), Qt::CaseInsensitive)
                      ? dm7::kChannelsCompact
                      : dm7::kChannelsDm7;
    caps.dcaCount       = dm7::kDcaCount;        // 24 — three times the X32's 8
    caps.muteGroupCount = dm7::kMuteGroupCount;  // 12
    caps.inputMetering  = true;   // 120 ch, three pickoffs. Unlike CL/QL.
    caps.channelEq      = true;   // 4 bands + HPF and LPF. Unlike CL/QL and TF.
    caps.liveCapture    = true;   // unsolicited NOTIFY, no subscription needed
    setCapabilities(caps);
}

void Dm7Link::handleReply(const dm7::Reply &reply)
{
    if (reply.status == dm7::Reply::Status::Error) {
        // Non-fatal: an unknown address usually means this firmware doesn't
        // expose something we probed for. That's information, not a failure.
        setError(tr("Console rejected %1: %2").arg(reply.action, reply.value));
        return;
    }

    if (reply.action == QLatin1String("devinfo")) {
        if (reply.address == QLatin1String("version"))     m_firmware = reply.value;
        if (reply.address == QLatin1String("productname")) m_productName = reply.value;
        if (m_productName.isEmpty()) return;

        // These arrive as separate replies in whatever order the console sends
        // them, so refresh capabilities on every devinfo rather than only on
        // the first — otherwise whichever field lands second is lost.
        applyModelCapabilities(m_productName);

        if (state() == State::Connecting) {
            // Gate Connected on productname specifically: until we know DM7 vs
            // DM7 Compact we don't know the channel count, and every
            // capability check downstream depends on it.
            setState(State::Connected);
            requestInitialState();
        }
        return;
    }

    // Split mode. Read before trusting any DCA index.
    if (reply.address == QLatin1String(dm7::kSplitOn)) {
        const bool split = reply.value.toInt() != 0;
        if (split != m_split) {
            m_split = split;
            emit splitModeDetected(split);
        }
        return;
    }
    if (reply.address == QLatin1String(dm7::kSplitDcaStart)) { m_dcaStart = reply.value.toInt(); return; }
    if (reply.address == QLatin1String(dm7::kSplitDcaNum))   { m_dcaNum   = reply.value.toInt(); return; }

    // A scene was recalled. The console does NOT enumerate what changed, so
    // our cached view is worthless and the only correct move is a full resync.
    if (reply.action.startsWith(QLatin1String("sscurrent")) ||
        reply.action.startsWith(QLatin1String("ssrecall"))) {
        m_dcaCache.clear();
        emit resyncRequired(tr("The console recalled a scene."));
        requestInitialState();
        return;
    }

    // ── The one that matters ─────────────────────────────────────────
    //
    // Only a NOTIFY is a surface change. An OK is the console echoing our own
    // write back at us; treating that as news is how a feedback loop starts.
    if (reply.address == QLatin1String(dm7::kDcaAssign) && reply.x >= 0 && reply.y >= 0) {
        if (!reply.isSurfaceChange()) return;

        const int channel = reply.x + 1;      // wire is 0-based, we are 1-based
        const int dca     = reply.y + 1;
        DcaSet current = dcaAssignment(channel);
        if (reply.value.toInt() != 0) current.insert(dca);
        else                          current.remove(dca);
        noteSurfaceDcaAssignment(channel, current);
        return;
    }

    if (reply.address == QLatin1String(dm7::kChannelOn) && reply.x >= 0) {
        if (!reply.isSurfaceChange()) return;
        emit surfaceChannelMuteChanged(reply.x + 1, dm7::mutedFromOnValue(reply.value.toInt()));
        return;
    }
}

void Dm7Link::requestInitialState()
{
    // Split mode first — it decides whether our DCA indices mean anything.
    send(dm7::getCommand(QLatin1String(dm7::kSplitOn), 0, 0));
    send(dm7::getCommand(QLatin1String(dm7::kSplitDcaStart), 0, 0));
    send(dm7::getCommand(QLatin1String(dm7::kSplitDcaNum), 0, 0));

    // NOT a full 120x24 = 2880-message DCA sync here. That takes ~14 seconds at
    // a safe message rate and would stall connect. The cue we fire next
    // overwrites everything we care about anyway, and NOTIFY keeps us current
    // afterwards. Mute state is cheap and worth having.
    for (int ch = 1; ch <= capabilities().channelCount; ++ch)
        send(dm7::getCommand(QLatin1String(dm7::kChannelOn), ch - 1, 0));
}

void Dm7Link::writeDcaAssignment(int channel, const DcaSet &previous, const DcaSet &next)
{
    // THE point of ConsoleLink handing us (previous, next).
    //
    // X32 has no per-membership address, so it ignores `previous` and writes
    // the whole 8-bit mask. DM7 is the exact opposite: it has ONLY
    // per-membership addresses, one message per (channel, DCA) pair. Writing
    // all 24 pairs per channel per cue would be 24x the traffic for no reason
    // — and a full show sync is already 120x24 = 2880 messages.
    //
    // So: send only what actually changed. Usually one or two messages.
    for (int dca : previous)
        if (!next.contains(dca))
            send(dm7::setCommand(QLatin1String(dm7::kDcaAssign), channel - 1, dca - 1,
                                 QStringLiteral("0")));

    for (int dca : next)
        if (!previous.contains(dca))
            send(dm7::setCommand(QLatin1String(dm7::kDcaAssign), channel - 1, dca - 1,
                                 QStringLiteral("1")));
}

void Dm7Link::setChannelMuted(int channel, bool muted)
{
    if (!isChannelValid(channel)) return;
    send(dm7::setCommand(QLatin1String(dm7::kChannelOn), channel - 1, 0,
                         QString::number(dm7::onValueForMuted(muted))));
}

void Dm7Link::setDcaLabel(int dca, const QString &name)
{
    if (!isDcaValid(dca)) return;
    // Max name length is disputed — the community table says 64, Yamaha's own
    // spec says 8. Don't truncate on a guess: send it and let the console
    // decide. Truncating to the wrong figure loses characters that would have
    // fit; sending too many is at worst silently clipped by the desk.
    send(dm7::setCommand(QLatin1String(dm7::kDcaName), dca - 1, 0, dm7::quote(name)));
}

} // namespace quewi::mix
