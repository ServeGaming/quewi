#pragma once

#include "mix/ConsoleLink.h"
#include "mix/Dm7Value.h"

#include <memory>

class QTcpSocket;
class QTimer;

namespace quewi::mix {

// Yamaha DM7 console link. RCP over TCP 49280, ASCII, LF-terminated.
// Protocol details and sourcing: docs/dev/console-protocols.md.
//
// NOTE we deliberately do NOT use DM7's OSC interface (UDP 49900). It's
// officially documented and current, but WRITE-ONLY — no get, no notify, no
// subscription — so it can't read state or capture live edits. The OSC spec is
// useful only as a parameter reference.
class Dm7Link : public ConsoleLink {
    Q_OBJECT
public:
    explicit Dm7Link(QObject *parent = nullptr);
    ~Dm7Link() override;

    void    connectToConsole(const QString &host, quint16 port = 0) override;
    void    disconnectFromConsole() override;
    quint16 defaultPort() const override { return 49280; }
    QString protocolName() const override { return QStringLiteral("Yamaha DM7"); }

    void setDcaLabel(int dca, const QString &name) override;
    void setChannelMuted(int channel, bool muted) override;

    // ── Split mode ───────────────────────────────────────────────────
    //
    // A DM7 can run as two independent mixers, partitioning channels, DCAs and
    // mute groups between two units. If we ignore it, our DCA indices address
    // the wrong unit. Read-only on the console; we read it before touching
    // anything and refuse to guess.
    bool isSplit() const { return m_split; }
    int  dcaStartChannel() const { return m_dcaStart; }   // 0-based, from the console

signals:
    void splitModeDetected(bool split);

protected:
    void writeDcaAssignment(int channel, const DcaSet &previous,
                            const DcaSet &next) override;

private slots:
    void onReadyRead();
    void onConnected();
    void onSocketError();
    void onKeepaliveTick();

private:
    void send(const QString &line);
    void handleReply(const dm7::Reply &reply);
    void requestInitialState();
    void applyModelCapabilities(const QString &productName);

    std::unique_ptr<QTcpSocket> m_sock;
    std::unique_ptr<QTimer>     m_keepalive;
    QString m_rxBuffer;          // TCP is a stream; lines can split across reads

    QString m_productName;
    QString m_firmware;

    bool m_split    = false;
    int  m_dcaStart = 0;
    int  m_dcaNum   = dm7::kDcaCount;
};

} // namespace quewi::mix
