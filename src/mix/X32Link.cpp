#include "mix/X32Link.h"

#include "osc/OscCodec.h"

#include <QRegularExpression>
#include <QTimer>
#include <QUdpSocket>

using namespace quewi::osc;

namespace quewi::mix {
namespace {

// /xremote lapses after 10 s. Companion re-sends every 1.5 s in production —
// a 6.6x margin that survives lossy Wi-Fi. Match it; the cost is trivial and
// the failure is a show losing live capture silently.
constexpr int kKeepaliveMs = 1500;

// Two silent windows (~3 s) with no traffic at all. The console is chatty
// once /xremote is live, so prolonged silence means we probably lost the
// registration race for one of its four client slots.
constexpr int kSilentWindowsBeforeAlarm = 2;

// Scene Safe bitmap for input channels. bit 5 = Groups (DCA + mute group
// assign) — the one that decides whether a scene recall wipes our work.
constexpr int   kSceneSafeGroupsBit = 5;
constexpr quint8 kSceneSafeGroupsMask = 1u << kSceneSafeGroupsBit;

const QString kSceneSafeInputsAddr = QStringLiteral("/-show/showfile/show/inputs");

// Pull the 1-based strip number out of "/ch/07/grp/dca" -> 7.
std::optional<int> channelFromAddress(const QString &addr, QLatin1String suffix)
{
    static const QRegularExpression re(QStringLiteral("^/ch/(\\d{2})/(.+)$"));
    const auto m = re.match(addr);
    if (!m.hasMatch()) return std::nullopt;
    if (m.captured(2) != suffix) return std::nullopt;
    bool ok = false;
    const int ch = m.captured(1).toInt(&ok);
    return ok ? std::optional<int>(ch) : std::nullopt;
}

} // namespace

X32Link::X32Link(QObject *parent) : ConsoleLink(parent) {}

X32Link::~X32Link()
{
    // Sockets and timer die with us; nothing to unregister — /xremote simply
    // lapses after 10 s of silence, which is the protocol's own design.
}

void X32Link::connectToConsole(const QString &host, quint16 port)
{
    disconnectFromConsole();

    m_host = QHostAddress(host);
    if (m_host.isNull()) {
        setError(tr("'%1' is not a valid address.").arg(host));
        setState(State::Failed);
        return;
    }
    m_port = port ? port : defaultPort();

    m_rx = std::make_unique<QUdpSocket>(this);
    m_tx = std::make_unique<QUdpSocket>(this);

    // Bind both to OS-chosen ports. What matters is only that they DIFFER, so
    // the console treats tx as "another client" and relays its changes to rx.
    if (!m_rx->bind(QHostAddress::AnyIPv4, 0) || !m_tx->bind(QHostAddress::AnyIPv4, 0)) {
        setError(tr("Couldn't open a UDP socket for the console."));
        setState(State::Failed);
        return;
    }
    connect(m_rx.get(), &QUdpSocket::readyRead, this, &X32Link::onRxReadyRead);

    setState(State::Connecting);

    // /info tells us the model and firmware. The reply comes back to rx
    // because that's the source port we sent it from.
    query({QStringLiteral("/info"), {}});

    m_keepalive = std::make_unique<QTimer>(this);
    m_keepalive->setInterval(kKeepaliveMs);
    connect(m_keepalive.get(), &QTimer::timeout, this, &X32Link::onKeepaliveTick);
    m_keepalive->start();
    onKeepaliveTick();   // register immediately, don't wait 1.5 s
}

void X32Link::disconnectFromConsole()
{
    m_keepalive.reset();
    m_rx.reset();
    m_tx.reset();
    m_dcaCache.clear();
    m_linkedChannels.clear();
    m_groupsSafed = false;
    m_silentKeepalives = 0;
    setState(State::Disconnected);
}

void X32Link::sendFrom(QUdpSocket *sock, const osc::Message &m)
{
    if (!sock || state() == State::Disconnected) return;
    const QByteArray packet = Codec::encode(m);
    if (sock->writeDatagram(packet, m_host, m_port) < 0)
        setError(tr("Send failed: %1").arg(sock->errorString()));
}

void X32Link::onKeepaliveTick()
{
    // /xremote must come from rx — registration is keyed on (IP, source port),
    // and rx is the socket we want the console talking to.
    query({QStringLiteral("/xremote"), {}});

    if (state() == State::Connected) {
        if (++m_silentKeepalives == kSilentWindowsBeforeAlarm) {
            // Not fatal — we can still drive the desk. But live capture is
            // dead and the operator must know, because the symptom otherwise
            // is "quewi mysteriously ignores console moves".
            emit remoteRegistrationLost();
            setError(tr("The console isn't reporting changes. It accepts only four "
                        "remote clients — X32-Edit or a tablet may have taken the slot."));
        }
    }
}

void X32Link::onRxReadyRead()
{
    while (m_rx && m_rx->hasPendingDatagrams()) {
        QByteArray buf(int(m_rx->pendingDatagramSize()), Qt::Uninitialized);
        m_rx->readDatagram(buf.data(), buf.size());

        m_silentKeepalives = 0;   // any traffic proves we're still registered

        const auto decoded = Codec::decode(buf);
        if (!decoded) continue;   // not fatal: the desk emits some malformed replies
        if (const auto *msg = std::get_if<Message>(&*decoded))
            handleMessage(*msg);
    }
}

void X32Link::handleMessage(const osc::Message &m)
{
    if (m.address == QLatin1String("/info")) {
        handleInfoReply(m);
        return;
    }

    // Scene Safe bitmap.
    if (m.address == kSceneSafeInputsAddr) {
        if (const auto n = firstNumber(m)) {
            m_sceneSafeInputs = quint8(*n);
            const bool safed = (m_sceneSafeInputs & kSceneSafeGroupsMask) != 0;
            if (safed != m_groupsSafed) {
                m_groupsSafed = safed;
                emit sceneSafeGroupsChanged(safed);
            }
        }
        return;
    }

    // Channel links. "/config/chlink/1-2" -> both members are linked.
    if (m.address.startsWith(QLatin1String("/config/chlink/"))) {
        const auto n = firstNumber(m);
        const QString pair = m.address.mid(QStringLiteral("/config/chlink/").size());
        const auto parts = pair.split(u'-');
        if (n && parts.size() == 2) {
            const int a = parts[0].toInt(), b = parts[1].toInt();
            if (*n != 0) { m_linkedChannels.insert(a); m_linkedChannels.insert(b); }
            else         { m_linkedChannels.remove(a); m_linkedChannels.remove(b); }
        }
        return;
    }

    // A scene/snippet/cue was recalled. Neither protocol enumerates what a
    // recall changed, so the only correct response is to throw away our cached
    // view and resync. If Groups isn't safed, our assignments are also gone.
    if (m.address.startsWith(QLatin1String("/-action/go")) ||
        m.address == QLatin1String("/-show/prepos/current")) {
        m_dcaCache.clear();
        emit resyncRequired(m_groupsSafed
            ? tr("The console recalled a scene.")
            : tr("The console recalled a scene and Scene Safe 'Groups' is off — "
                 "DCA assignments have been reverted by the desk."));
        requestInitialState();
        return;
    }

    // DCA membership changed on the surface.
    if (const auto ch = channelFromAddress(m.address, QLatin1String("grp/dca"))) {
        if (const auto n = firstNumber(m)) {
            const auto list = x32::dcaMaskToList(x32::DcaMask(*n));
            noteSurfaceDcaAssignment(*ch, DcaSet(list.begin(), list.end()));
        }
        return;
    }

    // Mute changed on the surface. mix/on is ON-not-mute: 0 = muted.
    if (const auto ch = channelFromAddress(m.address, QLatin1String("mix/on"))) {
        if (const auto n = firstNumber(m))
            emit surfaceChannelMuteChanged(*ch, x32::mutedFromOnValue(int(*n)));
        return;
    }
}

void X32Link::handleInfoReply(const osc::Message &m)
{
    // /info ,ssss <server_version> <server_name> <console_model> <console_version>
    if (m.args.size() < 4) return;

    auto str = [&](int i) -> QString {
        return m.args[size_t(i)].tag == Argument::Tag::String
             ? std::get<QString>(m.args[size_t(i)].value) : QString();
    };

    Capabilities caps;
    caps.model    = str(2);
    caps.firmware = str(3);

    // Every X32/M32 variant — Standard, Compact, Producer, Rack, Core, and the
    // Midas equivalents — exposes an identical OSC surface. The differences are
    // physical: faders, local preamps, screen size. So these are constants
    // here, unlike on Yamaha where they genuinely vary.
    caps.channelCount   = x32::kChannelCount;   // 32
    caps.dcaCount       = x32::kDcaCount;       // 8
    caps.muteGroupCount = 6;
    caps.inputMetering  = true;    // /meters/1: 32 ch + gate GR + dyn GR
    caps.channelEq      = true;    // 4 bands on inputs
    caps.liveCapture    = true;    // via /xremote
    setCapabilities(caps);

    if (state() != State::Connected) {
        setState(State::Connected);
        requestInitialState();
    }
}

void X32Link::requestInitialState()
{
    // Scene Safe first — it decides whether anything else we do will survive.
    query({kSceneSafeInputsAddr, {}});

    // Channel link state, or we produce double-moves nobody can explain.
    for (int a = 1; a <= x32::kChannelCount - 1; a += 2)
        query({QStringLiteral("/config/chlink/%1-%2").arg(a).arg(a + 1), {}});

    // Current DCA membership and mute for every channel.
    for (int ch = 1; ch <= x32::kChannelCount; ++ch) {
        query({x32::chAddr(ch, QLatin1String("grp/dca")), {}});
        query({x32::chAddr(ch, QLatin1String("mix/on")), {}});
    }
}

bool X32Link::isChannelLinked(int channel) const
{
    return m_linkedChannels.contains(channel);
}

void X32Link::requestSceneSafeGroups()
{
    set({kSceneSafeInputsAddr,
         {Argument::i(m_sceneSafeInputs | kSceneSafeGroupsMask)}});
    query({kSceneSafeInputsAddr, {}});   // read back rather than assume
}

void X32Link::writeDcaAssignment(int channel, const DcaSet &previous, const DcaSet &next)
{
    // The X32 has no per-membership address: grp/dca is replace-not-toggle, so
    // `previous` is irrelevant here — we always write the whole mask. (The DM7
    // link will use `previous` to send only the pairs that differ.)
    Q_UNUSED(previous);

    QVector<int> list(next.begin(), next.end());
    set({x32::chAddr(channel, QLatin1String("grp/dca")),
         {Argument::i(x32::dcaListToMask(list))}});
}

void X32Link::setChannelMuted(int channel, bool muted)
{
    if (!isChannelValid(channel)) return;
    // mix/on, inverted: 0 = muted. There is no /ch/NN/mute address.
    set({x32::chAddr(channel, QLatin1String("mix/on")),
         {Argument::i(x32::onValueForMuted(muted))}});
}

void X32Link::setDcaLabel(int dca, const QString &name)
{
    if (!isDcaValid(dca)) return;
    // 12 characters max, and /dca/N is single-digit — not zero-padded like
    // /ch/01. x32::dcaAddr exists so that can only be got wrong once.
    set({x32::dcaAddr(dca, QLatin1String("config/name")),
         {Argument::s(name.left(12))}});
}

} // namespace quewi::mix
