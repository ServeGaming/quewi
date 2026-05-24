#include "osc/OscEngine.h"

#include "osc/OscCodec.h"
#include "osc/OscPattern.h"
#include "osc/OscSlip.h"

#include <QHostInfo>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUdpSocket>
#include <QWebSocket>
#include <QWebSocketServer>

namespace quewi::osc {

namespace {

QString peerKey(const QHostAddress &addr, quint16 port)
{
    return QStringLiteral("%1:%2").arg(addr.toString(), QString::number(port));
}

void emitEvent(OscEngine *engine, Direction dir, Transport tr,
               const QByteArray &raw, const QString &host, quint16 port)
{
    PacketEvent e;
    e.direction = dir;
    e.transport = tr;
    e.peerHost = host;
    e.peerPort = port;
    e.rawBytes = raw;
    if (auto decoded = Codec::decode(raw)) {
        e.parsed = *decoded;
        e.parseOk = true;
    } else {
        e.parseOk = false;
    }
    QMetaObject::invokeMethod(engine, "packetSeen", Qt::QueuedConnection,
                              Q_ARG(quewi::osc::PacketEvent, e));
}

} // namespace

OscEngine::OscEngine(QObject *parent)
    : QObject(parent)
    , m_udpOut(std::make_unique<QUdpSocket>(this))
{
    qRegisterMetaType<quewi::osc::PacketEvent>("quewi::osc::PacketEvent");
}

OscEngine::~OscEngine() = default;

// ---------------- Outbound ----------------

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
    if (dest.host.isEmpty() || dest.port == 0) {
        emit sendError(tr("Destination has no host/port"));
        return false;
    }

    QHostAddress addr(dest.host);
    if (addr.isNull()) {
        const auto info = QHostInfo::fromName(dest.host);
        if (info.addresses().isEmpty()) {
            emit sendError(tr("Could not resolve %1").arg(dest.host));
            return false;
        }
        addr = info.addresses().first();
    }

    switch (dest.transport) {
    case Destination::Udp: {
        const auto written = m_udpOut->writeDatagram(rawPacket, addr, dest.port);
        if (written < 0) {
            emit sendError(m_udpOut->errorString());
            return false;
        }
        emitEvent(this, Direction::Outbound, Transport::Udp,
                  rawPacket, addr.toString(), dest.port);
        return true;
    }
    case Destination::TcpSlip: {
        // One-shot connection per send for now. A connection pool can
        // come later if perf becomes an issue.
        auto *sock = new QTcpSocket(this);
        sock->connectToHost(addr, dest.port);
        if (!sock->waitForConnected(2000)) {
            const auto err = sock->errorString();
            sock->deleteLater();
            emit sendError(tr("TCP connect to %1: %2").arg(dest.host, err));
            return false;
        }
        const auto framed = SlipEncoder::encode(rawPacket);
        sock->write(framed);
        sock->flush();
        sock->disconnectFromHost();
        sock->deleteLater();
        emitEvent(this, Direction::Outbound, Transport::TcpSlip,
                  rawPacket, addr.toString(), dest.port);
        return true;
    }
    case Destination::WebSocket: {
        // Open, send one message, close. Same pragmatic choice as TCP.
        auto *ws = new QWebSocket();
        ws->setParent(this);
        QObject::connect(ws, &QWebSocket::connected, ws, [ws, rawPacket]() {
            ws->sendBinaryMessage(rawPacket);
            ws->close();
        });
        QObject::connect(ws, &QWebSocket::disconnected, ws, &QObject::deleteLater);
        const QUrl url(QStringLiteral("ws://%1:%2/").arg(addr.toString(), QString::number(dest.port)));
        ws->open(url);
        emitEvent(this, Direction::Outbound, Transport::WebSocket,
                  rawPacket, addr.toString(), dest.port);
        return true;
    }
    }
    emit sendError(tr("Unsupported transport"));
    return false;
}

// ---------------- Inbound ----------------

bool OscEngine::listenUdp(quint16 port)
{
    if (!m_udpIn) {
        m_udpIn = std::make_unique<QUdpSocket>(this);
        connect(m_udpIn.get(), &QUdpSocket::readyRead,
                this, &OscEngine::onUdpReadyRead);
    }
    if (m_udpIn->state() == QAbstractSocket::BoundState) m_udpIn->close();
    return m_udpIn->bind(QHostAddress::AnyIPv4, port);
}

bool OscEngine::listenTcpSlip(quint16 port)
{
    if (!m_tcpServer) {
        m_tcpServer = std::make_unique<QTcpServer>(this);
        connect(m_tcpServer.get(), &QTcpServer::newConnection,
                this, &OscEngine::onTcpNewConnection);
    }
    if (m_tcpServer->isListening()) m_tcpServer->close();
    return m_tcpServer->listen(QHostAddress::AnyIPv4, port);
}

bool OscEngine::listenWebSocket(quint16 port)
{
    if (!m_wsServer) {
        m_wsServer = std::make_unique<QWebSocketServer>(
            QStringLiteral("quewi-osc"),
            QWebSocketServer::NonSecureMode, this);
        connect(m_wsServer.get(), &QWebSocketServer::newConnection,
                this, &OscEngine::onWsNewConnection);
    }
    if (m_wsServer->isListening()) m_wsServer->close();
    return m_wsServer->listen(QHostAddress::AnyIPv4, port);
}

void OscEngine::stopAllListeners()
{
    if (m_udpIn) m_udpIn->close();
    if (m_tcpServer) m_tcpServer->close();
    if (m_wsServer) m_wsServer->close();
    for (auto &p : m_tcpPeers) {
        if (p.socket) p.socket->disconnectFromHost();
    }
    m_tcpPeers.clear();
    for (auto *w : m_wsPeers) w->close();
    m_wsPeers.clear();
}

// ---------------- Subscriptions ----------------

int OscEngine::subscribe(const QString &pattern, Handler handler)
{
    const int id = m_nextSubId++;
    m_subs.push_back({id, pattern, std::move(handler)});
    return id;
}

void OscEngine::unsubscribe(int id)
{
    m_subs.erase(std::remove_if(m_subs.begin(), m_subs.end(),
        [id](const Subscription &s) { return s.id == id; }),
        m_subs.end());
}

// ---------------- Slot handlers ----------------

void OscEngine::onUdpReadyRead()
{
    while (m_udpIn && m_udpIn->hasPendingDatagrams()) {
        const auto size = m_udpIn->pendingDatagramSize();
        QByteArray data;
        data.resize(static_cast<int>(size));
        QHostAddress senderAddr;
        quint16 senderPort = 0;
        m_udpIn->readDatagram(data.data(), size, &senderAddr, &senderPort);
        handleIncomingPacket(Transport::Udp, data,
                             senderAddr.toString(), senderPort);
    }
}

void OscEngine::onTcpNewConnection()
{
    while (m_tcpServer && m_tcpServer->hasPendingConnections()) {
        auto *sock = m_tcpServer->nextPendingConnection();
        TcpPeer peer;
        peer.socket = sock;
        peer.decoder = std::make_unique<SlipDecoder>();
        m_tcpPeers.push_back(std::move(peer));
        connect(sock, &QTcpSocket::readyRead,    this, &OscEngine::onTcpReadyRead);
        connect(sock, &QTcpSocket::disconnected, this, &OscEngine::onTcpDisconnected);
    }
}

void OscEngine::onTcpReadyRead()
{
    auto *sock = qobject_cast<QTcpSocket *>(sender());
    if (!sock) return;
    auto it = std::find_if(m_tcpPeers.begin(), m_tcpPeers.end(),
        [sock](const TcpPeer &p) { return p.socket == sock; });
    if (it == m_tcpPeers.end()) return;

    const QByteArray bytes = sock->readAll();
    const auto frames = it->decoder->feed(bytes);
    for (const auto &frame : frames) {
        handleIncomingPacket(Transport::TcpSlip, frame,
                             sock->peerAddress().toString(), sock->peerPort());
    }
}

void OscEngine::onTcpDisconnected()
{
    auto *sock = qobject_cast<QTcpSocket *>(sender());
    if (!sock) return;
    m_tcpPeers.erase(std::remove_if(m_tcpPeers.begin(), m_tcpPeers.end(),
        [sock](const TcpPeer &p) { return p.socket == sock; }),
        m_tcpPeers.end());
    sock->deleteLater();
}

void OscEngine::onWsNewConnection()
{
    while (m_wsServer && m_wsServer->hasPendingConnections()) {
        auto *ws = m_wsServer->nextPendingConnection();
        m_wsPeers.push_back(ws);
        connect(ws, &QWebSocket::binaryMessageReceived,
                this, &OscEngine::onWsBinaryMessage);
        connect(ws, &QWebSocket::textMessageReceived,
                this, &OscEngine::onWsTextMessage);
        connect(ws, &QWebSocket::disconnected,
                this, &OscEngine::onWsDisconnected);
    }
}

void OscEngine::onWsBinaryMessage(const QByteArray &message)
{
    auto *ws = qobject_cast<QWebSocket *>(sender());
    handleIncomingPacket(Transport::WebSocket, message,
                         ws ? ws->peerAddress().toString() : QString(),
                         ws ? ws->peerPort() : 0);
}

void OscEngine::onWsTextMessage(const QString &message)
{
    // Some clients send OSC packets as text frames (uncommon but seen
    // in practice). Decode UTF-8 → bytes and route the same way.
    onWsBinaryMessage(message.toUtf8());
}

void OscEngine::onWsDisconnected()
{
    auto *ws = qobject_cast<QWebSocket *>(sender());
    if (!ws) return;
    m_wsPeers.erase(std::remove(m_wsPeers.begin(), m_wsPeers.end(), ws),
                    m_wsPeers.end());
    ws->deleteLater();
}

void OscEngine::handleIncomingPacket(Transport tr, const QByteArray &bytes,
                                     const QString &peerHost, quint16 peerPort)
{
    emitEvent(this, Direction::Inbound, tr, bytes, peerHost, peerPort);

    if (auto decoded = Codec::decode(bytes)) {
        // Stash sender info so query handlers can reply back to the
        // peer that sent the request. Single-threaded — dispatch runs
        // synchronously on the GUI thread, the socket signals serialize.
        m_lastSenderHost      = peerHost;
        m_lastSenderPort      = peerPort;
        m_lastSenderTransport = tr;
        dispatch(*decoded);
    }
}

void OscEngine::dispatch(const Element &element)
{
    std::visit([this](const auto &x) {
        using U = std::decay_t<decltype(x)>;
        if constexpr (std::is_same_v<U, Message>) {
            for (const auto &sub : m_subs) {
                if (Pattern::matches(sub.pattern, x.address)) {
                    sub.handler(x);
                }
            }
        } else if constexpr (std::is_same_v<U, Bundle>) {
            // Time-tag scheduling lands with the GoEngine in Phase 6.
            // Until then, fire bundle elements immediately regardless of
            // their time tag — adequate for monitoring and forwarding.
            for (const auto &child : x.elements) dispatch(child);
        }
    }, element);
}

} // namespace quewi::osc
