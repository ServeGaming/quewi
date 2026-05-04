#pragma once

#include "osc/OscMessage.h"

#include <QHostAddress>
#include <QObject>
#include <QString>
#include <memory>
#include <vector>

// quewi's OSC engine.
//
// Owns transports (UDP, TCP/SLIP, WebSocket — added incrementally) and
// the routing table from address pattern → handler. See OscCodec for the
// pure codec and OscMessage for the data types.
//
// Coverage matrix lives in docs/osc-coverage.md.

class QUdpSocket;

namespace quewi::osc {

class UdpInput;
class UdpOutput;

// A named outbound destination — what an OSC cue points at.
struct Destination {
    QString     id;        // stable handle (e.g. UUID-as-string)
    QString     name;      // human label, "Eos console", etc.
    QString     host;      // hostname or IP literal
    quint16     port = 0;
    enum Transport { Udp, TcpSlip, WebSocket } transport = Udp;
};

class OscEngine : public QObject {
    Q_OBJECT
public:
    explicit OscEngine(QObject *parent = nullptr);
    ~OscEngine() override;

    // Outbound. Returns false and emits sendError on failure.
    bool send(const Destination &dest, const Message &message);
    bool send(const Destination &dest, const Bundle &bundle);
    bool send(const Destination &dest, const QByteArray &rawPacket);

signals:
    void sendError(const QString &reason);
    void packetSent(const Destination &dest, qsizetype bytes);

private:
    std::unique_ptr<QUdpSocket> m_udpOut;
};

} // namespace quewi::osc
