#include "osc/EosFeedback.h"

#include "osc/OscCodec.h"
#include "osc/OscMessage.h"

#include <QHostAddress>
#include <QRegularExpression>
#include <QTcpSocket>
#include <QTimer>
#include <QtEndian>

namespace quewi::osc {

EosCueText parseEosCueText(const QString &text)
{
    // "<list>/<cue> <label…> <time> <pct>%". The label can be empty or hold
    // numbers of its own ("Scene 2"), so the time is only the token right
    // before the percentage, never the first number found.
    EosCueText out;
    QStringList tok = text.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (tok.isEmpty()) return out;
    const QString head = tok.takeFirst();
    const int slash = head.indexOf(QLatin1Char('/'));
    if (slash > 0) {
        out.list = head.left(slash);
        out.cue  = head.mid(slash + 1);
    } else {
        out.cue = head;
    }
    if (!tok.isEmpty() && tok.last().endsWith(QLatin1Char('%'))) {
        bool ok = false;
        const int pct = tok.last().chopped(1).toInt(&ok);
        if (ok) {
            out.percent = pct;
            tok.removeLast();
        }
    }
    // The time comes last (before the percentage, when there is one):
    // "5.00", "1:30", "5". The previous cue's text has no percentage at all
    // ("1/1 5.0" on Nomad).
    static const QRegularExpression timeRe(QStringLiteral(R"(^\d+(:\d{1,2})*(\.\d+)?$)"));
    if (!tok.isEmpty() && timeRe.match(tok.last()).hasMatch())
        out.time = tok.takeLast();
    out.label = tok.join(QLatin1Char(' '));
    return out;
}

double eosTimeSeconds(const QString &time)
{
    static const QRegularExpression re(QStringLiteral(R"(^\d+(:\d{1,2})*(\.\d+)?$)"));
    if (!re.match(time).hasMatch()) return -1.0;
    double total = 0.0;
    for (const auto &part : time.split(QLatin1Char(':'))) total = total * 60.0 + part.toDouble();
    return total;
}

EosFeedback::EosFeedback(QObject *parent) : QObject(parent)
{
    m_since.start();
    m_retry = new QTimer(this);
    m_retry->setInterval(3000);
    m_retry->setSingleShot(true);
    connect(m_retry, &QTimer::timeout, this, &EosFeedback::connectNow);

    m_ping = new QTimer(this);
    m_ping->setInterval(5000);
    connect(m_ping, &QTimer::timeout, this, [this] {
        if (!m_sock || m_sock->state() != QAbstractSocket::ConnectedState) return;
        // The desk went quiet: the link is gone even if TCP hasn't noticed.
        if (m_lastHeard.isValid() && m_lastHeard.elapsed() > m_silenceMs) {
            setLink(Link::Connecting, tr("The desk stopped answering — reconnecting"));
            m_sock->abort();
            m_retry->start(0);
            return;
        }
        send({QStringLiteral("/eos/ping"), {}});
    });
}

EosFeedback::~EosFeedback() = default;

void EosFeedback::setTimings(int reconnectMs, int pingMs, int silenceMs)
{
    m_retry->setInterval(reconnectMs);
    m_ping->setInterval(pingMs);
    m_silenceMs = silenceMs;
}

void EosFeedback::start(const QString &host, int port)
{
    const QString h = host.trimmed();
    if (m_wanted && h == m_host && port == m_port) return;
    stop();
    m_host = h;
    m_port = port > 0 ? port : 3032;
    m_wanted = true;
    if (m_host.isEmpty()) {
        setLink(Link::Failed, tr("No desk IP address set"));
        return;
    }
    connectNow();
}

void EosFeedback::stop()
{
    m_wanted = false;
    m_retry->stop();
    m_ping->stop();
    if (m_sock) {
        m_sock->disconnect(this);
        m_sock->abort();
        m_sock->deleteLater();
        m_sock = nullptr;
    }
    m_buf.clear();
    clearState();
    setLink(Link::Off);
}

void EosFeedback::clearState()
{
    const bool had = !m_active.isEmpty() || !m_pending.isEmpty() || !m_previous.isEmpty()
                     || !m_showName.isEmpty() || m_blind || m_progress >= 0.0;
    m_active = m_pending = m_previous = {};
    m_progress = -1.0;
    m_rate = 0.0;
    m_showName.clear();
    m_blind = false;
    m_haveActive = false;
    m_lastFiredKey.clear();
    if (had) emit stateChanged();
}

void EosFeedback::setLink(Link l, const QString &detail)
{
    const bool changed = l != m_link || detail != m_detail;
    m_link = l;
    m_detail = detail;
    if (changed) emit linkChanged(l);
}

void EosFeedback::connectNow()
{
    if (!m_wanted) return;
    if (m_sock) {
        m_sock->disconnect(this);
        m_sock->abort();
        m_sock->deleteLater();
    }
    m_buf.clear();
    m_sock = new QTcpSocket(this);
    connect(m_sock, &QTcpSocket::connected, this, [this] {
        m_lastHeard.start();
        setLink(Link::Connecting, tr("Connected — waiting for the desk"));
        // Ask for the desk's state; it pushes /eos/out/... from now on.
        send({QStringLiteral("/eos/subscribe"), {Argument::i(1)}});
        send({QStringLiteral("/eos/ping"), {}});
        m_ping->start();
    });
    connect(m_sock, &QTcpSocket::readyRead, this, &EosFeedback::onReadyRead);
    connect(m_sock, &QTcpSocket::disconnected, this, [this] {
        m_ping->stop();
        clearState();
        if (m_wanted) {
            setLink(Link::Connecting, tr("Lost the desk — reconnecting"));
            m_retry->start();
        }
    });
    connect(m_sock, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        if (!m_wanted || !m_sock) return;
        if (m_sock->state() == QAbstractSocket::ConnectedState) return;   // disconnected() handles it
        m_ping->stop();
        clearState();
        setLink(Link::Failed, tr("Can't reach %1:%2 (%3)")
                                  .arg(m_host).arg(m_port).arg(m_sock->errorString()));
        m_retry->start();
    });
    if (m_link != Link::Failed) setLink(Link::Connecting, tr("Connecting…"));
    m_sock->connectToHost(m_host, quint16(m_port));
}

void EosFeedback::send(const Message &m)
{
    if (!m_sock || m_sock->state() != QAbstractSocket::ConnectedState) return;
    const QByteArray pkt = Codec::encode(m);
    QByteArray framed(4, Qt::Uninitialized);
    qToBigEndian<quint32>(quint32(pkt.size()), framed.data());
    framed.append(pkt);
    m_sock->write(framed);
}

void EosFeedback::onReadyRead()
{
    m_buf.append(m_sock->readAll());
    while (m_buf.size() >= 4) {
        const quint32 len = qFromBigEndian<quint32>(m_buf.constData());
        if (len > 1u << 20) {               // not OSC 1.0 framing: start over
            m_buf.clear();
            return;
        }
        if (quint32(m_buf.size()) < 4 + len) return;
        const QByteArray pkt = m_buf.mid(4, int(len));
        m_buf.remove(0, int(4 + len));
        m_lastHeard.start();
        if (m_link != Link::Live) setLink(Link::Live);
        const auto el = Codec::decode(pkt);
        if (!el) continue;
        // Bundles: walk them for their messages.
        std::vector<Element> stack{*el};
        while (!stack.empty()) {
            Element e = std::move(stack.back());
            stack.pop_back();
            if (auto *msg = std::get_if<Message>(&e)) handle(*msg);
            else for (auto &child : std::get<Bundle>(e).elements) stack.push_back(child);
        }
    }
}

void EosFeedback::handle(const Message &m)
{
    auto str = [&m]() -> QString {
        if (m.args.empty() || m.args.front().tag != Argument::Tag::String) return {};
        return std::get<QString>(m.args.front().value);
    };
    const QString &a = m.address;
    bool changed = false;
    auto setText = [&](EosCueText &slot) {
        const EosCueText next = parseEosCueText(str());
        if (next == slot) return;
        slot = next;
        changed = true;
    };
    if (a == QLatin1String("/eos/out/active/cue/text")) {
        const QString was = m_active.list + QLatin1Char('/') + m_active.cue;
        const bool knew = m_haveActive;
        setText(m_active);
        m_haveActive = true;
        if (m_active.list + QLatin1Char('/') + m_active.cue != was) {
            m_rate = 0.0;                                          // a new cue
            // A different cue is running now: it was fired. (Not the state
            // the desk reports first thing after connecting.)
            if (knew && !m_active.isEmpty()) fired(m_active.list, m_active.cue, m_active.label);
        }
        if (m_active.percent >= 0 && m_active.percent / 100.0 != m_progress) {
            setProgress(m_active.percent / 100.0);
            changed = true;
        }
    } else if (a == QLatin1String("/eos/out/pending/cue/text")) {
        setText(m_pending);
    } else if (a == QLatin1String("/eos/out/previous/cue/text")) {
        setText(m_previous);
    } else if (a == QLatin1String("/eos/out/active/cue")) {
        // A float 0..1 as the active cue runs — smoother than the text's %.
        if (const auto v = firstNumber(m)) {
            const double p = std::clamp(*v, 0.0, 1.0);
            if (p != m_progress) { setProgress(p); changed = true; }
        }
    } else if (a == QLatin1String("/eos/out/show/name")) {
        if (str() != m_showName) { m_showName = str(); changed = true; }
    } else if (a == QLatin1String("/eos/out/event/state")) {
        if (const auto v = firstNumber(m)) {
            const bool blind = *v == 0.0;          // 0 = Blind, 1 = Live
            if (blind != m_blind) { m_blind = blind; changed = true; }
        }
    } else if (a.startsWith(QLatin1String("/eos/out/event/cue/")) && a.endsWith(QLatin1String("/fire"))) {
        // /eos/out/event/cue/<list>/<cue>/fire — the desk's own "cue fired".
        const QStringList p = a.split(QLatin1Char('/'), Qt::SkipEmptyParts);
        if (p.size() >= 7) {
            const QString label = (p[4] == m_active.list && p[5] == m_active.cue) ? m_active.label : QString();
            fired(p[4], p[5], label);
        }
    } else if (a == QLatin1String("/eos/out/blind") || a == QLatin1String("/eos/out/live")) {
        const bool blind = a.endsWith(QLatin1String("blind"));
        if (blind != m_blind) { m_blind = blind; changed = true; }
    }
    if (changed) emit stateChanged();
    emit messageReceived(m);
}

void EosFeedback::fired(const QString &list, const QString &cue, const QString &label)
{
    // The desk can say it twice (the event and the active cue changing):
    // once per cue within a second.
    const QString key = list + QLatin1Char('/') + cue;
    const qint64 t = now();
    if (key == m_lastFiredKey && t - m_lastFiredMs < 1000) return;
    m_lastFiredKey = key;
    m_lastFiredMs = t;
    emit cueFired(list, cue, label);
}

bool EosFeedback::isConnected() const
{
    return m_sock && m_sock->state() == QAbstractSocket::ConnectedState;
}

void EosFeedback::setClock(std::function<qint64()> nowMs) { m_clock = std::move(nowMs); }

qint64 EosFeedback::now() const { return m_clock ? m_clock() : m_since.elapsed(); }

void EosFeedback::setProgress(double p)
{
    const qint64 t = now();
    // How fast it's going: measured from the last two updates of the same
    // cue when it rose, else the cue's own time ("8.00" → 1/8 per second).
    // (Updates closer than 100 ms apart — the text and the float for the
    // same moment — say nothing about the pace.)
    if (m_progress >= 0.0 && p > m_progress && t - m_progressAtMs >= 100) {
        const double measured = (p - m_progress) * 1000.0 / double(t - m_progressAtMs);
        m_rate = std::min(10.0, m_rate > 0.0 ? 0.5 * m_rate + 0.5 * measured : measured);
    } else if (p < m_progress || m_progress < 0.0
               || (m_rate <= 0.0 && t - m_progressAtMs >= 100)) {
        const double secs = eosTimeSeconds(m_active.time);
        m_rate = secs > 0.0 ? 1.0 / secs : 0.0;
    }
    m_progress = p;
    m_progressAtMs = t;
}

double EosFeedback::activeProgress() const
{
    if (m_progress < 0.0 || m_progress >= 1.0 || m_rate <= 0.0) return m_progress;
    // Run on for at most 2 s past the last update: a desk that stops
    // reporting (held, or a dropped link) mustn't race the bar to the end.
    const double secs = std::min(2.0, (now() - m_progressAtMs) / 1000.0);
    return std::min(1.0, m_progress + m_rate * std::max(0.0, secs));
}

} // namespace quewi::osc
