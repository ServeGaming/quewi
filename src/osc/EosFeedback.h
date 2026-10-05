#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QString>

#include <functional>

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
// An Eos time as seconds: "5", "5.00", "1:30", "1:02:03.5". -1 if it isn't one.
double eosTimeSeconds(const QString &time);

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
    // The active cue's progress, 0..1 (< 0 unknown), moved on smoothly
    // between the desk's updates: the desk only says where it is every so
    // often, so this runs on at the cue's pace (its time, or the rate the
    // last updates rose at) for up to a couple of seconds past the last one.
    double  activeProgress() const;
    double  reportedProgress() const { return m_progress; }   // as last sent
    QString showName() const { return m_showName; }
    bool    blind() const { return m_blind; }

    // Feed one decoded message as if the desk sent it (tests, and the TCP path).
    void handle(const Message &m);

    // Send a message to the desk over this link (nothing if it isn't up).
    // For readers that share the connection, like EosCueLists — they only
    // ever send /eos/get/... requests, never anything that changes the desk.
    void sendToDesk(const Message &m) { send(m); }
    bool isConnected() const;

    // Timing, overridable for tests (ms).
    void setTimings(int reconnectMs, int pingMs, int silenceMs);
    void setClock(std::function<qint64()> nowMs);   // tests: a fake clock

signals:
    void linkChanged(quewi::osc::EosFeedback::Link link);
    void stateChanged();               // any cue / show / blind change
    // Every message the desk sends, after this class has read it (so a
    // sibling reader — EosCueLists — shares the one TCP connection).
    void messageReceived(const quewi::osc::Message &m);
    // A cue just ran on the desk (someone pressed GO, Back, went to a cue…):
    // from /eos/out/event/cue/<list>/<cue>/fire, or the active cue changing to
    // another cue. Once per fire, never for the state the desk reports when
    // the link comes up. (For recording lighting triggers from the desk.)
    void cueFired(const QString &list, const QString &cue, const QString &label);

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
    // Smoothing: when m_progress was last set, and how fast it's rising.
    void       setProgress(double p);
    qint64     now() const;
    std::function<qint64()> m_clock;
    QElapsedTimer m_since;
    qint64     m_progressAtMs = 0;
    double     m_rate = 0.0;              // progress per second; 0 = unknown
    QString    m_showName;
    bool       m_blind = false;
    bool       m_haveActive = false;      // the desk has said what's active since connecting
    QString    m_lastFiredKey;
    qint64     m_lastFiredMs = -100000;
    void       fired(const QString &list, const QString &cue, const QString &label);
};

} // namespace quewi::osc
