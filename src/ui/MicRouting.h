#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>

namespace quewi::ui {

// "Send soundboard sounds to my mic" — per computer, not per show (it's about
// this machine's audio devices), saved in QSettings under soundboard/mic/*.
//
// How it works: a virtual cable (VB-Audio CABLE, VoiceMeeter, …) is a pair
// of devices — audio played into its OUTPUT side comes out of its INPUT side,
// which apps like Discord, OBS or a game can pick as their microphone. quewi
// plays pad sounds into that output, optionally still to your speakers too,
// and optionally mixes your real microphone in, so the "mic" the app hears is
// your voice plus the sound effects.
class MicRouting : public QObject {
    Q_OBJECT
public:
    static MicRouting *instance();

    // The playback device the sounds go into (the cable's input side).
    // Empty = off.
    QByteArray deviceId() const { return m_deviceId; }
    bool       enabled() const { return !m_deviceId.isEmpty(); }
    // Keep playing pads on the board's own output as well, so you hear them.
    bool       monitor() const { return m_monitor; }
    // How loud the sounds are in the mic, relative to the pad's own level.
    double     sfxGainDb() const { return m_sfxGainDb; }
    // Mix a real microphone into the same device.
    bool       passthrough() const { return m_passthrough; }
    QByteArray inputDeviceId() const { return m_inputId; }
    double     inputGainDb() const { return m_inputGainDb; }

    void setDeviceId(const QByteArray &id);
    void setMonitor(bool on);
    void setSfxGainDb(double db);
    void setPassthrough(bool on);
    void setInputDeviceId(const QByteArray &id);
    void setInputGainDb(double db);

    // Looks like a virtual cable / virtual mic (by device name).
    static bool looksVirtual(const QString &description);

signals:
    // Anything changed. `inputChanged` additionally when the passthrough
    // itself (on/off, device, target) changed, which needs the capture
    // restarted rather than just a gain tweak.
    void changed();
    void inputChanged();

private:
    MicRouting();
    void save();

    QByteArray m_deviceId;
    bool       m_monitor = true;
    double     m_sfxGainDb = 0.0;
    bool       m_passthrough = false;
    QByteArray m_inputId;
    double     m_inputGainDb = 0.0;
};

} // namespace quewi::ui
