#pragma once

#include "cues/Cue.h"
#include "osc/OscMessage.h"

namespace quewi::osc {

// A cue that fires a single OSC message at a host/port over UDP (TCP/SLIP
// and WebSocket transports come in a follow-up commit).
//
// Phase 2 keeps the args UX deliberately simple: a comma-separated text
// field where each token is auto-typed as int / float / string / bool /
// nil. A richer typed-arg editor (like the one sketched in UX.md §8)
// lands when the OSC monitor and `learn` mode arrive.
class OscCue : public cues::Cue {
    Q_OBJECT
public:
    explicit OscCue(QObject *parent = nullptr);
    ~OscCue() override;

    QString typeKey()  const override { return QStringLiteral("osc"); }
    QString typeName() const override { return tr("OSC"); }

    // Generic field bridge: address, host, port, transport, rawArgs.
    QVariant field(const QString &key) const override;
    void     setField(const QString &key, const QVariant &value) override;

    QJsonObject toPayload() const override;
    void        fromPayload(const QJsonObject &payload) override;

    // Build a Message ready to send. Parses rawArgs into typed Arguments.
    Message buildMessage() const;

    // Build a Destination from the cue's host/port/transport. The id is
    // derived from the cue's id so each cue is its own destination until
    // a shared patch lands.
    struct DestinationView {
        QString  id;
        QString  name;
        QString  host;
        quint16  port;
        int      transport; // matches Destination::Transport enum
    };
    DestinationView destination() const;

private:
    QString m_address  = QStringLiteral("/");
    QString m_host     = QStringLiteral("127.0.0.1");
    quint16 m_port     = 53000;
    int     m_transport = 0; // 0=UDP, 1=TCP/SLIP, 2=WebSocket
    QString m_rawArgs;
};

} // namespace quewi::osc
