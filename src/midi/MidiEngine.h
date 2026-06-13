#pragma once

#include <QByteArray>
#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QStringList>

#include <memory>

class RtMidiOut;

namespace quewi::midi {

// RtMidi-backed MIDI output. Phase-6 MVP focuses on send only — input
// (MIDI triggers, MTC chase) lands in Phase 7. Devices are opened lazily
// on first send to a given port name and cached for the rest of the run.
class MidiEngine : public QObject {
    Q_OBJECT
public:
    explicit MidiEngine(QObject *parent = nullptr);
    ~MidiEngine() override;

    // Snapshot of every output port name visible to the OS. Cached for
    // 5 s — opening RtMidiOut on Windows hits WinMM and is too slow to
    // call from every Inspector rebuild. Use refreshPorts() to force.
    QStringList outputPortNames() const;
    void        refreshPorts() const;

    // Send raw MIDI bytes to a port. Empty portName = first available
    // port. Returns false on enumeration failure or if the port can't
    // be opened; lastError() has the reason.
    bool sendRaw(const QString &portName, const QByteArray &bytes);

    // Build and send a MIDI Show Control sysex. command_format / command
    // are MSC enums; data is the trailing payload (q_number, q_list, q_path
    // ASCII-encoded with 0x00 separators). deviceId 0x7F = all-call.
    bool sendMsc(const QString &portName,
                 quint8 deviceId,
                 quint8 commandFormat,
                 quint8 command,
                 const QByteArray &payload);

    QString lastError() const { return m_lastError; }

private:
    RtMidiOut *openOrGetPort(const QString &portName);

    std::unique_ptr<RtMidiOut> m_out;   // current open port
    QString                    m_openPortName;
    // mutable so the const port-enumeration path can record an
    // enumeration failure (matches MidiInputEngine's behaviour).
    mutable QString            m_lastError;

    mutable QStringList        m_cachedPorts;
    mutable QElapsedTimer      m_cacheTimer;
};

} // namespace quewi::midi
