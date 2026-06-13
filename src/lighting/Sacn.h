#pragma once

#include <QByteArray>
#include <QHostAddress>
#include <QString>
#include <QHash>
#include <QUuid>
#include <array>
#include <memory>

class QUdpSocket;

// sACN (E1.31) multicast DMX-over-IP. Hand-rolled per the spec:
//
//   * Root layer:    PDU containing source CID and the framing PDU
//   * Framing layer: source name, priority, sync addr, sequence, universe
//   * DMP layer:     start code + 512 channel slot array
//
// Multicast group for universe `u`: 239.255.<u_high>.<u_low> on port 5568.
// Universe 1 → 239.255.0.1.

namespace quewi::lighting {

using DmxFrame = std::array<quint8, 512>;

class SacnSender {
public:
    SacnSender();
    ~SacnSender();

    // Configure the source identity (called once by LightingEngine).
    void setSourceCid(const QUuid &cid)            { m_cid = cid; }
    void setSourceName(const QString &name)        { m_sourceName = name; }
    void setPriority(quint8 priority)              { m_priority = priority; }

    // Send one DMX frame to the universe's multicast group. Returns
    // false if the socket fails. Sequence numbers are tracked per-call.
    bool sendUniverse(quint16 universe, const DmxFrame &frame);

    // Build a packet without sending — exposed for tests.
    static QByteArray buildPacket(const QUuid &sourceCid,
                                  const QString &sourceName,
                                  quint16 universe,
                                  quint8 priority,
                                  quint8 sequenceNumber,
                                  const DmxFrame &frame);

private:
    std::unique_ptr<QUdpSocket> m_socket;
    QUuid                       m_cid;
    QString                     m_sourceName = QStringLiteral("quewi");
    quint8                      m_priority   = 100;
    // E1.31 sequence numbering is defined PER UNIVERSE. A single
    // shared counter makes each universe see a gappy, non-monotonic
    // sequence, and strict receivers run the out-of-sequence discard
    // algorithm (>20 out of order) and drop frames. Track one counter
    // per universe instead.
    QHash<quint16, quint8>      m_sequence;
};

} // namespace quewi::lighting
