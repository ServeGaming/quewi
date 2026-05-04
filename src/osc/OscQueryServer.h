#pragma once

#include "osc/OscDictionary.h"

#include <QObject>
#include <QString>
#include <memory>

class QTcpServer;
class QTcpSocket;

namespace quewi::osc {

// Tiny HTTP/1.1 server that exposes the app's OSC namespace per the OSC
// Query Protocol: http://github.com/Vidvox/OSCQueryProposal
//
// Endpoints:
//   GET /         → JSON tree of every dictionary entry, OSC-Query shape
//   GET /HOST_INFO → server identity (NAME, OSC_PORT, OSC_TRANSPORT, EXTENSIONS)
//
// We back the namespace with an OscDictionary so the same data drives
// the cue editor's autocomplete and the wire-visible namespace. This
// is intentionally small — full pattern routing belongs in OscEngine.
class OscQueryServer : public QObject {
    Q_OBJECT
public:
    explicit OscQueryServer(QObject *parent = nullptr);
    ~OscQueryServer() override;

    // Set the namespace this server advertises. The pointer must outlive
    // the server. Mutations are reflected on the next request.
    void setDictionary(const OscDictionary *dict) { m_dict = dict; }

    // Identity returned in /HOST_INFO. Defaults: name="quewi", oscPort=0,
    // transport="UDP". Set oscPort to whatever OscEngine listens on.
    void setName(const QString &name)        { m_name = name; }
    void setOscPort(quint16 port)            { m_oscPort = port; }
    void setOscTransport(const QString &t)   { m_oscTransport = t; }

    bool listen(quint16 port);
    void stop();
    quint16 port() const { return m_listenPort; }

private slots:
    void onNewConnection();
    void onClientReadyRead();

private:
    void handleRequest(QTcpSocket *sock, const QByteArray &request);
    QByteArray buildRoot() const;
    QByteArray buildHostInfo() const;
    static QByteArray httpResponse(int status, const char *reason,
                                   const char *contentType,
                                   const QByteArray &body);

    std::unique_ptr<QTcpServer> m_server;
    const OscDictionary *m_dict = nullptr;
    QString  m_name = QStringLiteral("quewi");
    quint16  m_oscPort = 0;
    QString  m_oscTransport = QStringLiteral("UDP");
    quint16  m_listenPort = 0;
};

} // namespace quewi::osc
