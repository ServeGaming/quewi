#include "lighting/ArtNet.h"

#include <QUdpSocket>
#include <QtEndian>

namespace quewi::lighting {

namespace {
constexpr quint16 kArtNetPort = 6454;
} // namespace

ArtNetSender::ArtNetSender()
    : m_socket(std::make_unique<QUdpSocket>())
    , m_target(QHostAddress::Broadcast)
{
}

ArtNetSender::~ArtNetSender() = default;

void ArtNetSender::setOutputInterface(const QHostAddress &localAddr)
{
    if (!m_socket || localAddr.isNull()) return;
    // Art-Net is broadcast, so setMulticastInterface doesn't apply; binding
    // the send socket to the chosen NIC's address scopes egress to it.
    if (m_socket->localAddress() == localAddr) return;  // already bound there
    m_socket->bind(localAddr, 0, QAbstractSocket::ReuseAddressHint);
}

QByteArray ArtNetSender::buildPacket(quint16 universe, quint8 sequenceNumber,
                                     const DmxFrame &frame)
{
    QByteArray pkt;
    pkt.reserve(18 + 512);

    pkt.append("Art-Net", 7);
    pkt.append('\0');                                   // ID = "Art-Net\0"

    // OpCode 0x5000 (OpOutput / ArtDmx) — sent LITTLE-endian on the wire.
    pkt.append(static_cast<char>(0x00));
    pkt.append(static_cast<char>(0x50));

    // Protocol version 14 — BIG-endian (Hi, Lo).
    pkt.append(static_cast<char>(0x00));
    pkt.append(static_cast<char>(0x0E));

    pkt.append(static_cast<char>(sequenceNumber));      // Sequence (0 = disabled)
    pkt.append(static_cast<char>(0x00));                // Physical

    // 15-bit Port-Address: SubUni (low byte) then Net (high 7 bits).
    pkt.append(static_cast<char>(universe & 0xFF));
    pkt.append(static_cast<char>((universe >> 8) & 0x7F));

    // Length of the DMX data, BIG-endian (even, 2..512). Always a full frame.
    const quint16 len = qToBigEndian<quint16>(static_cast<quint16>(512));
    pkt.append(reinterpret_cast<const char *>(&len), 2);

    pkt.append(reinterpret_cast<const char *>(frame.data()), 512);
    return pkt;
}

bool ArtNetSender::sendUniverse(quint16 universe, const DmxFrame &frame)
{
    if (!m_socket) return false;
    // Per-port-address sequence, 1..255 (0 means "ignore sequence", so we
    // never sit on 0 once we've started).
    quint8 s = m_sequence.value(universe, 0);
    s = (s >= 255) ? quint8(1) : quint8(s + 1);
    m_sequence[universe] = s;

    const auto pkt = buildPacket(universe, s, frame);
    const auto written = m_socket->writeDatagram(pkt, m_target, kArtNetPort);
    return written == pkt.size();
}

} // namespace quewi::lighting
