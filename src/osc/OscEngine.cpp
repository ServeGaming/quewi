#include "osc/OscEngine.h"

#include "osc/OscCodec.h"

#include <QHostInfo>
#include <QUdpSocket>

namespace quewi::osc {

OscEngine::OscEngine(QObject *parent)
    : QObject(parent)
    , m_udpOut(std::make_unique<QUdpSocket>(this))
{
}

OscEngine::~OscEngine() = default;

bool OscEngine::send(const Destination &dest, const Message &message)
{
    return send(dest, Codec::encode(message));
}

bool OscEngine::send(const Destination &dest, const Bundle &bundle)
{
    return send(dest, Codec::encode(bundle));
}

bool OscEngine::send(const Destination &dest, const QByteArray &rawPacket)
{
    if (dest.transport != Destination::Udp) {
        emit sendError(tr("TCP/SLIP and WebSocket transports are not yet implemented"));
        return false;
    }
    if (dest.host.isEmpty() || dest.port == 0) {
        emit sendError(tr("Destination has no host/port"));
        return false;
    }

    QHostAddress addr(dest.host);
    if (addr.isNull()) {
        // Try sync DNS — fine for now since this is called off the audio path.
        // A future tweak: cache resolutions and refresh periodically.
        const auto info = QHostInfo::fromName(dest.host);
        if (info.addresses().isEmpty()) {
            emit sendError(tr("Could not resolve %1").arg(dest.host));
            return false;
        }
        addr = info.addresses().first();
    }

    const auto written = m_udpOut->writeDatagram(rawPacket, addr, dest.port);
    if (written < 0) {
        emit sendError(m_udpOut->errorString());
        return false;
    }
    emit packetSent(dest, written);
    return true;
}

} // namespace quewi::osc
