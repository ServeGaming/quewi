#pragma once

#include "cues/Cue.h"

#include <QByteArray>

namespace quewi::midi {

// Sends a raw MIDI message (any bytes — note on/off, CC, PC, sysex)
// to a chosen output port. The Inspector can build common messages
// from a friendlier UI; the source of truth is the byte string.
class MidiCue : public cues::Cue {
    Q_OBJECT
public:
    explicit MidiCue(QObject *parent = nullptr);
    ~MidiCue() override;

    QString typeKey()  const override { return QStringLiteral("midi"); }
    QString typeName() const override { return tr("MIDI"); }

    QVariant field(const QString &key) const override;
    void     setField(const QString &key, const QVariant &value) override;

    QJsonObject toPayload() const override;
    void        fromPayload(const QJsonObject &payload) override;

    QString    portName() const { return m_portName; }
    QByteArray bytes()    const { return m_bytes; }

private:
    QString    m_portName;
    QByteArray m_bytes;
};

// MIDI Show Control. Stores the structured fields; the engine builds
// the sysex frame at fire time. Common command formats / commands are
// listed in the Inspector combos for convenience.
class MscCue : public cues::Cue {
    Q_OBJECT
public:
    explicit MscCue(QObject *parent = nullptr);
    ~MscCue() override;

    QString typeKey()  const override { return QStringLiteral("msc"); }
    QString typeName() const override { return tr("MSC"); }

    QVariant field(const QString &key) const override;
    void     setField(const QString &key, const QVariant &value) override;

    QJsonObject toPayload() const override;
    void        fromPayload(const QJsonObject &payload) override;

    QString  portName()      const { return m_portName; }
    int      deviceId()      const { return m_deviceId; }
    int      commandFormat() const { return m_commandFormat; }
    int      command()       const { return m_command; }
    QString  qNumber()       const { return m_qNumber; }
    QString  qList()         const { return m_qList; }
    QString  qPath()         const { return m_qPath; }

    // Build the sysex payload (everything between command_format and F7).
    QByteArray buildPayload() const;

private:
    QString m_portName;
    int     m_deviceId      = 0x7F;       // all-call by default
    // MSC command_format per MMA RP-002: 0x01 = Lighting (general),
    // 0x10 = Sound (general), 0x40 = Projection. Default to Lighting —
    // the most common MSC target in theatre (ETC Eos / grandMA taking
    // GO). The old default 0x10 with a "Lighting" comment was
    // self-contradictory and actually addressed sound devices.
    int     m_commandFormat = 0x01;       // Lighting (general)
    int     m_command       = 0x01;       // GO
    QString m_qNumber;
    QString m_qList;
    QString m_qPath;
};

} // namespace quewi::midi
