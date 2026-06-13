#include "lighting/Sacn.h"

#include <QUdpSocket>
#include <QVariant>
#include <QtEndian>
#include <cstring>

namespace quewi::lighting {

namespace {

constexpr quint16 kAcnPort = 5568;

void writeU8(QByteArray &b, quint8 v) {
    b.append(static_cast<char>(v));
}
void writeU16(QByteArray &b, quint16 v) {
    const quint16 be = qToBigEndian(v);
    b.append(reinterpret_cast<const char *>(&be), 2);
}
void writeU32(QByteArray &b, quint32 v) {
    const quint32 be = qToBigEndian(v);
    b.append(reinterpret_cast<const char *>(&be), 4);
}

QHostAddress universeMulticast(quint16 universe) {
    const auto hi = static_cast<quint8>((universe >> 8) & 0xFF);
    const auto lo = static_cast<quint8>(universe & 0xFF);
    return QHostAddress(QStringLiteral("239.255.%1.%2").arg(hi).arg(lo));
}

} // namespace

SacnSender::SacnSender()
    : m_socket(std::make_unique<QUdpSocket>())
    , m_cid(QUuid::createUuid())
{
    m_socket->setSocketOption(QAbstractSocket::MulticastTtlOption, 1);
}

SacnSender::~SacnSender() = default;

QByteArray SacnSender::buildPacket(const QUuid &sourceCid,
                                   const QString &sourceName,
                                   quint16 universe,
                                   quint8 priority,
                                   quint8 sequenceNumber,
                                   const DmxFrame &frame)
{
    QByteArray pkt;
    pkt.reserve(638);

    // ---- Root Layer (38 bytes) ----
    writeU16(pkt, 0x0010);              // Preamble Size
    writeU16(pkt, 0x0000);              // Postamble Size
    pkt.append("ASC-E1.17", 12);        // ACN Packet Identifier (12 bytes, NUL-padded)
    while (pkt.size() < 16) pkt.append('\0');
    // Flags + length: top nibble = 0x7, low 12 bits = total PDU length
    const quint16 rootFlagsLen = 0x7000 | (638 - 16);
    writeU16(pkt, rootFlagsLen);
    writeU32(pkt, 0x00000004);          // Vector: VECTOR_ROOT_E131_DATA
    // CID (16 bytes)
    const auto cidBytes = sourceCid.toRfc4122();
    pkt.append(cidBytes);

    // ---- E1.31 Framing Layer (77 bytes) ----
    const quint16 framingFlagsLen = 0x7000 | (638 - 38);
    writeU16(pkt, framingFlagsLen);
    writeU32(pkt, 0x00000002);          // Vector: VECTOR_E131_DATA_PACKET
    // Source name (64 bytes UTF-8, NUL-padded)
    QByteArray nameBytes = sourceName.toUtf8().left(63);
    nameBytes.append('\0');
    while (nameBytes.size() < 64) nameBytes.append('\0');
    pkt.append(nameBytes);
    writeU8(pkt, priority);             // Priority 1..200
    writeU16(pkt, 0);                   // Sync universe (0 = no sync)
    writeU8(pkt, sequenceNumber);
    writeU8(pkt, 0);                    // Options
    writeU16(pkt, universe);

    // ---- DMP Layer (523 bytes) ----
    const quint16 dmpFlagsLen = 0x7000 | (638 - 115);
    writeU16(pkt, dmpFlagsLen);
    writeU8(pkt, 0x02);                 // Vector: VECTOR_DMP_SET_PROPERTY
    writeU8(pkt, 0xA1);                 // Address type & data type
    writeU16(pkt, 0x0000);              // First property address
    writeU16(pkt, 0x0001);              // Address increment
    writeU16(pkt, 513);                 // Property value count (start code + 512 slots)
    writeU8(pkt, 0x00);                 // DMX start code
    pkt.append(reinterpret_cast<const char *>(frame.data()), 512);

    return pkt;
}

bool SacnSender::sendUniverse(quint16 universe, const DmxFrame &frame)
{
    if (!m_socket) return false;
    // Per-universe sequence counter (E1.31 §6.7.2). operator[]
    // default-inserts 0 for a universe's first packet, then we
    // post-increment so each universe advances independently.
    const quint8 seq = m_sequence[universe]++;
    const auto pkt = buildPacket(m_cid, m_sourceName, universe,
                                 m_priority, seq, frame);
    const auto written = m_socket->writeDatagram(pkt, universeMulticast(universe), kAcnPort);
    return written == pkt.size();
}

} // namespace quewi::lighting
