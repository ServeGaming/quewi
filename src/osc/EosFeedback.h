#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QString>

class QTcpSocket;
class QTimer;

namespace quewi::osc {

struct Message;

// One of the Eos "/eos/out/{active,pending,previous}/cue/text" strings, taken
// apart: "1/12 Chorus wash 5.00 100%" → list 1, cue 12, label "Chorus wash",
// time "5.00", 100 %. An empty string (no cue) gives an empty result.
struct EosCueText {
    QString list, cue, label, time;
    int     percent = -1;            // -1 = not given
    bool isEmpty() const { return cue.isEmpty(); }
    bool operator==(const EosCueText &o) const {
        return list == o.list && cue == o.cue && label == o.label && time == o.time
            && percent == o.percent;
    }
};
EosCueText parseEosCueText(const QString &text);

// Reads an ETC Eos console's state back (Eos, Ion, Element, Nomad): which
// cue is running and which is pending, its progress, the show name, Blind.
//
// Over TCP to the desk's OSC port 3032 (OSC 1.0: each packet prefixed with a
// 4-byte big-endian length), the way HeliOSC talks to an Ion: no OSC TX port
// to set up on the desk — "OSC TCP" just has to be allowed, which it is by
// default. On connect it sends /eos/subscribe 1 (the desk then pushes its
// /eos/out/... state), pings every 5 s, and if the desk goes quiet for 12 s it
// reconnects. Reconnects every 3 s while it can't reach the desk.
//
// Read-only: quewi still sends its lighting triggers the usual way (UDP to
// the desk's OSC RX port), so this never changes what the desk does.
class EosFeedback : public QObject {
    Q_OBJECT
public:
    enum class Link { Off, Connecting, Live, Failed };
    Q_ENUM(Link)

    explicit EosFeedback(QObject *parent = nullptr);
    ~EosFeedback() override;

    // Connect to host:port (and keep trying). Same host/port = no-op.
    void start(const QString &host, int port = 3032);
    void stop();                       // disconnect; link Off

    Link    link() const { return m_link; }
    QString host() const { return m_host; }
    int     port() const { return m_port; }
    QString detail() const { return m_detail; }   // why it failed / "Connecting…"

    const EosCueText &active() const   { return m_active; }
    const EosCueText &pending() const  { return m_pending; }
    const EosCueText &previous() const { return m_previous; }
    double  activeProgress() const { return m_progress; }   // 0..1, < 0 unknown
    QString showName() const { return m_showName; }
    bool    blind() const { return m_blind; }

    // Feed one decoded message as if the desk sent it (tests, and the TCP path).
    void handle(const Message &m);

    // Timing, overridable for tests (ms).
    void setTimings(int reconnectMs, int pingMs, int silenceMs);

signals:
    void linkChanged(quewi::osc::EosFeedback::Link link);
    void stateChanged();               // any cue / show / blind change

private:
    void connectNow();
    void setLink(Link l, const QString &detail = {});
    void onReadyRead();
    void send(const Message &m);
    void clearState();

    QTcpSocket *m_sock = nullptr;
    QTimer     *m_retry = nullptr;
    QTimer     *m_ping = nullptr;
    QElapsedTimer m_lastHeard;
    QByteArray  m_buf;
    QString     m_host;
    int         m_port = 3032;
    bool        m_wanted = false;
    Link        m_link = Link::Off;
    QString     m_detail;
    int         m_silenceMs = 12000;

    EosCueText m_active, m_pending, m_previous;
    double     m_progress = -1.0;
    QString    m_showName;
    bool       m_blind = false;
};

} // namespace quewi::osc
