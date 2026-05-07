#pragma once

#include <QByteArray>
#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QStringList>

#include <memory>

class RtMidiIn;

namespace quewi::midi {

// RtMidi-backed MIDI input. Sibling of MidiEngine (output-only).
//
// Used for two things:
//   1. Live "Learn" mode in Preferences — the next incoming message is
//      surfaced via messageReceived() so the binding UI can capture it.
//   2. Routing bound messages to actions (GO / Pause / Panic / Fade-All)
//      while the show runs.
//
// One open port at a time. Switching ports closes the old one cleanly
// (RtMidiIn's destructor handles the WinMM teardown).
class MidiInputEngine : public QObject {
    Q_OBJECT
public:
    explicit MidiInputEngine(QObject *parent = nullptr);
    ~MidiInputEngine() override;

    // Visible input ports, cached for 5 s — same shape as MidiEngine.
    QStringList inputPortNames() const;
    void        refreshPorts() const;

    // Open the named port. Empty string closes the current one.
    // Returns false on enumeration / open failure (lastError has why).
    bool openPort(const QString &portName);
    QString currentPortName() const { return m_openPortName; }

    QString lastError() const { return m_lastError; }

signals:
    // Every received non-system-realtime message arrives here on the
    // GUI thread (we marshal from RtMidi's callback via QueuedConnection).
    // status = first byte; bytes = full message.
    void messageReceived(quint8 status, const QByteArray &bytes);

private:
    static void onMidiCallback(double timestamp,
                               std::vector<unsigned char> *message,
                               void *userData);

    std::unique_ptr<RtMidiIn> m_in;
    QString                   m_openPortName;
    mutable QString           m_lastError;

    mutable QStringList       m_cachedPorts;
    mutable QElapsedTimer     m_cacheTimer;
};

} // namespace quewi::midi
