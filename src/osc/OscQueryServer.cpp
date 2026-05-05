#include "osc/OscQueryServer.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

namespace {
// Hard limits on incoming HTTP requests. quewi only ever serves OSC
// Query GETs whose meaningful payload is tens of bytes; anything beyond
// these bounds is either a buggy client or someone trying to fill our
// memory.
constexpr qint64 kMaxRequestBytes      = 16 * 1024;
constexpr int    kRequestTimeoutMs     = 2000;
}

namespace quewi::osc {

OscQueryServer::OscQueryServer(QObject *parent) : QObject(parent) {}
OscQueryServer::~OscQueryServer() { stop(); }

bool OscQueryServer::listen(quint16 port)
{
    stop();
    m_server = std::make_unique<QTcpServer>(this);
    connect(m_server.get(), &QTcpServer::newConnection,
            this, &OscQueryServer::onNewConnection);
    if (!m_server->listen(QHostAddress::Any, port)) {
        m_server.reset();
        return false;
    }
    m_listenPort = m_server->serverPort();
    return true;
}

void OscQueryServer::stop()
{
    if (m_server) {
        m_server->close();
        m_server.reset();
    }
    m_listenPort = 0;
}

void OscQueryServer::onNewConnection()
{
    while (m_server && m_server->hasPendingConnections()) {
        auto *sock = m_server->nextPendingConnection();
        if (!sock) continue;
        connect(sock, &QTcpSocket::readyRead, this, &OscQueryServer::onClientReadyRead);
        connect(sock, &QTcpSocket::disconnected, sock, &QObject::deleteLater);

        // Per-connection timeout: if the client doesn't finish sending
        // its request quickly, drop them. Stops slow-loris-style holds
        // from tying up our event loop.
        auto *deadline = new QTimer(sock);
        deadline->setSingleShot(true);
        deadline->setInterval(kRequestTimeoutMs);
        connect(deadline, &QTimer::timeout, sock, [sock] {
            if (sock->state() != QAbstractSocket::UnconnectedState) {
                sock->abort();
            }
        });
        deadline->start();
    }
}

void OscQueryServer::onClientReadyRead()
{
    auto *sock = qobject_cast<QTcpSocket *>(sender());
    if (!sock) return;
    // Cap the read to a small fixed budget. A real OSC Query client
    // sends a few hundred bytes at most; anything bigger is junk or a
    // memory-exhaustion attempt.
    if (sock->bytesAvailable() > kMaxRequestBytes) {
        sock->write(httpResponse(413, "Payload Too Large", "text/plain",
                                 QByteArray("request too large")));
        sock->flush();
        sock->disconnectFromHost();
        return;
    }
    const auto data = sock->read(kMaxRequestBytes);
    if (data.isEmpty()) return;
    handleRequest(sock, data);
}

void OscQueryServer::handleRequest(QTcpSocket *sock, const QByteArray &request)
{
    // Parse just enough: "GET <path> HTTP/1.1"
    const int firstSp = request.indexOf(' ');
    const int secondSp = (firstSp > 0) ? request.indexOf(' ', firstSp + 1) : -1;
    QByteArray path;
    if (firstSp > 0 && secondSp > firstSp) {
        path = request.mid(firstSp + 1, secondSp - firstSp - 1);
    }

    QByteArray response;
    if (path == "/HOST_INFO" || path == "/host_info") {
        response = httpResponse(200, "OK", "application/json", buildHostInfo());
    } else if (path.startsWith("/") || path.isEmpty()) {
        response = httpResponse(200, "OK", "application/json", buildRoot());
    } else {
        response = httpResponse(404, "Not Found", "text/plain",
                                QByteArray("not found"));
    }
    sock->write(response);
    sock->flush();
    sock->disconnectFromHost();
}

QByteArray OscQueryServer::buildRoot() const
{
    QJsonObject root;
    root.insert(QStringLiteral("DESCRIPTION"), m_name);
    root.insert(QStringLiteral("FULL_PATH"),   QStringLiteral("/"));
    root.insert(QStringLiteral("ACCESS"),      0);

    QJsonObject contents;
    if (m_dict) {
        for (const auto &e : m_dict->entries()) {
            QJsonObject node;
            node.insert(QStringLiteral("FULL_PATH"),   e.address);
            node.insert(QStringLiteral("TYPE"),        e.typeTags);
            node.insert(QStringLiteral("DESCRIPTION"), e.description);
            node.insert(QStringLiteral("ACCESS"),      3); // read+write
            contents.insert(e.address, node);
        }
    }
    root.insert(QStringLiteral("CONTENTS"), contents);
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

QByteArray OscQueryServer::buildHostInfo() const
{
    QJsonObject obj;
    obj.insert(QStringLiteral("NAME"),          m_name);
    obj.insert(QStringLiteral("OSC_PORT"),      int(m_oscPort));
    obj.insert(QStringLiteral("OSC_TRANSPORT"), m_oscTransport);
    QJsonObject ext;
    ext.insert(QStringLiteral("ACCESS"),       true);
    ext.insert(QStringLiteral("VALUE"),        true);
    ext.insert(QStringLiteral("DESCRIPTION"),  true);
    ext.insert(QStringLiteral("TYPE"),         true);
    obj.insert(QStringLiteral("EXTENSIONS"), ext);
    return QJsonDocument(obj).toJson(QJsonDocument::Compact);
}

QByteArray OscQueryServer::httpResponse(int status, const char *reason,
                                        const char *contentType,
                                        const QByteArray &body)
{
    QByteArray out;
    out.reserve(body.size() + 128);
    out.append("HTTP/1.1 ");
    out.append(QByteArray::number(status));
    out.append(' ');
    out.append(reason);
    out.append("\r\nContent-Type: ");
    out.append(contentType);
    out.append("\r\nContent-Length: ");
    out.append(QByteArray::number(body.size()));
    out.append("\r\nAccess-Control-Allow-Origin: *");
    out.append("\r\nConnection: close\r\n\r\n");
    out.append(body);
    return out;
}

} // namespace quewi::osc
