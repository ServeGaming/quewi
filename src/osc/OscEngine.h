#pragma once

#include "osc/OscMessage.h"

#include <QHostAddress>
#include <QObject>
#include <QString>
#include <functional>
#include <memory>
#include <vector>

class QUdpSocket;
class QTcpServer;
class QTcpSocket;
class QWebSocketServer;
class QWebSocket;

namespace quewi::osc {

class SlipDecoder;

// A named outbound destination — what an OSC cue points at.
struct Destination {
    QString id;
    QString name;
    QString host;
    quint16 port = 0;
    enum Transport { Udp, TcpSlip, WebSocket } transport = Udp;
};

// Direction tag for the monitor and the packetSeen signal.
enum class Direction { Outbound, Inbound };

// Transport tag for the monitor.
enum class Transport { Udp, TcpSlip, WebSocket };

// Metadata about a packet that the monitor wants to display.
struct PacketEvent {
    Direction direction;
    Transport transport;
    QString   peerHost;     // remote host:port (or "broadcast" / "n/a")
    quint16   peerPort = 0;
    QByteArray rawBytes;    // the wire bytes (post-SLIP-strip for TCP)
    Element   parsed;       // decoded; if decode failed, parsed.index() is implementation-defined
    bool      parseOk = true;
};

// The engine — owns transports, dispatches incoming messages by pattern,
// and emits a packetSeen signal so the monitor window can render every
// in/out packet without coupling to the dispatch path.
class OscEngine : public QObject {
    Q_OBJECT
public:
    explicit OscEngine(QObject *parent = nullptr);
    ~OscEngine() override;

    // ---------------- Outbound ----------------
    bool send(const Destination &dest, const Message &message);
    bool send(const Destination &dest, const Bundle &bundle);
    bool send(const Destination &dest, const QByteArray &rawPacket);

    // ---------------- Inbound ----------------
    // Bind a UDP listener on `port`. Returns true on success. Pass 0 to let the
    // OS pick any free port (useful when a fixed port is in use or in a
    // Windows-reserved/excluded range) — query the chosen one with udpPort().
    bool listenUdp(quint16 port);

    // The actual bound UDP port, or 0 if the listener isn't up. Reports the
    // OS-chosen port after a listenUdp(0), and lets callers show the real port.
    quint16 udpPort() const;

    // Start a TCP server (SLIP-framed) on `port`.
    bool listenTcpSlip(quint16 port);

    // Start a WebSocket server on `port`. Each incoming text/binary frame
    // is treated as one OSC packet.
    bool listenWebSocket(quint16 port);

    // Stop all listeners and disconnect any peers.
    void stopAllListeners();

    // ---------------- Subscriptions ----------------
    // Each subscription has an integer id so it can be removed later.
    // Pattern can be a literal address or any OSC pattern (?, *, [..], {..}, //).
    using Handler = std::function<void(const Message &)>;
    int subscribe(const QString &pattern, Handler handler);
    void unsubscribe(int id);

    // ---------------- Source info (during dispatch) ----------------
    // Valid ONLY while a subscription handler is executing. Lets a
    // handler reply back to whoever sent the request — needed for
    // query/reply patterns where the remote app expects a response
    // on the same channel it sent on. Calling these outside a
    // dispatch returns the last-seen values; safe but stale.
    QString   lastSenderHost()      const { return m_lastSenderHost; }
    quint16   lastSenderPort()      const { return m_lastSenderPort; }
    Transport lastSenderTransport() const { return m_lastSenderTransport; }

signals:
    void sendError(const QString &reason);
    void packetSeen(const quewi::osc::PacketEvent &event);

private slots:
    void onUdpReadyRead();
    void onTcpNewConnection();
    void onTcpReadyRead();
    void onTcpDisconnected();
    void onWsNewConnection();
    void onWsBinaryMessage(const QByteArray &message);
    void onWsTextMessage(const QString &message);
    void onWsDisconnected();

private:
    void handleIncomingPacket(Transport tr, const QByteArray &bytes,
                              const QString &peerHost, quint16 peerPort);
    void dispatch(const Element &element);

    struct Subscription {
        int id;
        QString pattern;
        Handler handler;
    };

    std::unique_ptr<QUdpSocket>       m_udpOut;
    std::unique_ptr<QUdpSocket>       m_udpIn;
    std::unique_ptr<QTcpServer>       m_tcpServer;
    std::unique_ptr<QWebSocketServer> m_wsServer;

    // Per-TCP-peer state: each connection has its own SLIP decoder.
    struct TcpPeer {
        QTcpSocket *socket = nullptr;
        std::unique_ptr<SlipDecoder> decoder;
    };
    std::vector<TcpPeer> m_tcpPeers;
    std::vector<QWebSocket *> m_wsPeers;

    std::vector<Subscription> m_subs;
    int m_nextSubId = 1;

    // Source of the most recently dispatched packet. Set in
    // handleIncomingPacket() before dispatch and read by handlers via
    // lastSenderHost/Port. Single-threaded (dispatch runs on the GUI
    // thread, driven by the socket signals).
    QString   m_lastSenderHost;
    quint16   m_lastSenderPort      = 0;
    Transport m_lastSenderTransport = Transport::Udp;
};

} // namespace quewi::osc

Q_DECLARE_METATYPE(quewi::osc::PacketEvent)
