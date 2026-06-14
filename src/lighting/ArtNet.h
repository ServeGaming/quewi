#pragma once

#include "lighting/Sacn.h"   // DmxFrame

#include <QHash>
#include <QHostAddress>

#include <memory>

class QUdpSocket;

// Art-Net 4 ArtDmx output — DMX-over-UDP, the most common lighting-network
// protocol. Broadcasts to UDP port 6454.
//
// ArtDmx packet (18-byte header + 512 data bytes):
//   "Art-Net\0" (8) | OpCode 0x5000 little-endian (2) | ProtVer 14 big-endian
//   (2) | Sequence (1) | Physical (1) | SubUni = low byte of the 15-bit
//   port-address (1) | Net = high 7 bits (1) | Length big-endian (2) | data[512]
//
// quewi's universe number maps straight to the 15-bit Art-Net Port-Address,
// so universe N on the console is universe N on the node, and the frame is
// sent to the limited broadcast address so any node on the LAN receives it.

namespace quewi::lighting {

class ArtNetSender {
public:
    ArtNetSender();
    ~ArtNetSender();

    void setBroadcastAddress(const QHostAddress &addr) { m_target = addr; }

    // Send one DMX frame for `universe`. Returns false if the socket fails.
    bool sendUniverse(quint16 universe, const DmxFrame &frame);

    // Build an ArtDmx packet without sending — exposed for tests.
    static QByteArray buildPacket(quint16 universe, quint8 sequenceNumber,
                                  const DmxFrame &frame);

private:
    std::unique_ptr<QUdpSocket> m_socket;
    QHostAddress                m_target;
    // Art-Net sequence is per port-address; 0 means "disabled", so we run
    // 1..255 and wrap back to 1 (never 0 after the first packet).
    QHash<quint16, quint8>      m_sequence;
};

} // namespace quewi::lighting
