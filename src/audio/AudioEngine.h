#pragma once

#include <QAudioDevice>
#include <QByteArray>
#include <QIODevice>
#include <QList>
#include <QObject>
#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

class QAudioSink;
class QMediaDevices;

namespace quewi::audio {

class AudioFile;
class AudioEffect;

using VoiceId = quint64;

// What the engine plays. Owned by the engine. Read by the mixer thread
// via mutex-guarded snapshot taken at the top of each readData() call —
// inside the callback, only atomic gain/pan and the immutable AudioFile
// samples are touched.
struct VoiceParams {
    double gainDb         = 0.0;
    double fadeInSeconds  = 0.0;
    double fadeOutSeconds = 0.0;
    double trimInSeconds  = 0.0;   // start playback this far into the file
    double trimOutSeconds = 0.0;   // 0 = play to end of file
    double pan            = 0.0;   // -1 = full L, 0 = centre, +1 = full R
    bool   loop           = false;

    // Empty = use the current default output device.
    QByteArray outputDeviceId;

    // Object-audio routing. When channelGains is non-empty, the mixer
    // ignores `pan` and writes each output channel scaled by the
    // matching gain. The vector length matches the device's channel
    // count. Computed by AudioEngine::fire() from the cue's spatial
    // position and the speaker patch — voices stay agnostic to VBAP.
    QList<float> channelGains;

    // Per-output send gains (linear) applied AFTER the stereo pan.
    // Empty = passthrough on every output. Independent of channelGains:
    // non-object-audio cues use this to route a stereo signal to FOH at
    // 0 dB and lobby at -12 dB without touching the pan. Object-audio
    // cues ignore it (channelGains already encodes the routing).
    QList<float> outputGains;

    // Per-cue effects chain (EQ/comp/reverb/delay) applied to the fired
    // voice as a stereo insert before the send matrix. Built fresh per
    // fire from the cue's saved rack; empty = dry. Ignored for object-
    // audio (channelGains) voices in v1. See docs/dev/realtime-fx-plan.md.
    // (Carried here now; the mixer wires it in a follow-up commit.)
    std::vector<std::shared_ptr<AudioEffect>> effects;
};

// Snapshot of a currently-playing voice — surfaced to the UI so the
// active-cues panel can show meters, position, and a stop button.
struct ActiveVoice {
    VoiceId    id              = 0;
    QByteArray deviceId;
    double     gainDb          = 0.0;
    double     pan             = 0.0;
    double     positionSeconds = 0.0;
    double     durationSeconds = 0.0;   // 0 if unknown / loop
    bool       loop            = false;
    // Linear (0..1) peak magnitude over the most recent mixer buffer.
    // Polled at ~30 Hz by the active-cues panel; UI decays the displayed
    // value so brief peaks remain visible.
    float      peakLeft        = 0.f;
    float      peakRight       = 0.f;
    // Per-channel peaks, length == device output channel count. Object
    // audio cues use this for per-output meters; stereo cues fall back
    // to peakLeft/peakRight which still mirror channels 0/1.
    QList<float> peakPerChannel;
};

// The engine owns one or more output devices and any number of active
// voices. Each voice is bound to a specific device; voices on different
// devices play simultaneously through their own QAudioSink. Devices are
// opened lazily — the first fire() call brings the relevant hardware up;
// idle quewi keeps the audio threads silent.
class AudioEngine : public QObject {
    Q_OBJECT
public:
    explicit AudioEngine(QObject *parent = nullptr);
    ~AudioEngine() override;

    // The default device is used by voices with an empty outputDeviceId.
    void setDefaultOutputDevice(const QAudioDevice &device);
    QAudioDevice defaultOutputDevice() const { return m_defaultDevice; }

    // Spin up the default device explicitly (otherwise lazy at first fire()).
    bool ensureRunning();
    void shutdown();

    VoiceId fire(const std::shared_ptr<const AudioFile> &file,
                 const VoiceParams &params);

    // Active voice control.
    void stop(VoiceId id, double fadeOutSeconds = 0.05);
    void stopAll(double fadeOutSeconds = 0.05);
    void fadeGain(VoiceId id, double targetDb, double durationSeconds);

    // Pause / resume — the voice keeps its read position and stays in
    // the engine's voice table, but produces silence until resume(). A
    // short anti-click fade (~5ms) is applied at the boundary. Returns
    // true if the voice was found.
    bool pause(VoiceId id);
    bool resume(VoiceId id);
    bool isPaused(VoiceId id) const;

    // Jump a playing voice to a new position, given in seconds from the
    // start of the source file. Clamped to [0, fileEnd) — caller doesn't
    // need to bounds-check. Returns true if the voice was found and the
    // seek applied. Used by the ACTIVE strip's scrubbable progress bar
    // so the operator can click anywhere in the bar to drop the playhead
    // there mid-show. A short anti-click ramp at the splice would be
    // ideal — for now we accept a brief discontinuity since theatre use
    // is overwhelmingly seek-while-paused.
    bool seek(VoiceId id, double seconds);

    // Real-time live changes — applied immediately, no fade. Used by the
    // inspector when the user nudges a slider on a playing cue.
    void setVoiceGain(VoiceId id, double gainDb);
    void setVoicePan(VoiceId id, double pan);
    // Toggle loop on a playing voice. Takes effect on the next
    // end-of-source — the callback wraps readPos to 0 instead of
    // marking finished.
    void setVoiceLoop(VoiceId id, bool loop);

    // Swap the per-output-channel gain vector for a playing voice. Used
    // by the trajectory ticker — each tick recomputes VBAP gains for the
    // new (azimuth, elevation, spread) and pushes them in. Mutex-guarded
    // so the audio callback either sees the old vector or the new one,
    // never a torn state. Vector length should match the device channel
    // count.
    void setVoiceChannelGains(VoiceId id, const QList<float> &gains);

    // For the active-cues panel. Cheap snapshot, safe to call at 4 Hz.
    QList<ActiveVoice> activeVoices() const;

    bool isRunning() const { return m_running.load(); }
    int  activeVoiceCount() const;

    // Output channel count for the device that would be used by a fire()
    // with this `outputDeviceId`. Returns 0 if the device can't be opened
    // (caller should fall back to legacy stereo pan). Lazy: opens the
    // device if it isn't already running, so the first call may incur a
    // small latency. Used by the Object Audio path so it can size the
    // VBAP gain vector to whatever the device reports.
    int  outputChannelCount(const QByteArray &outputDeviceId);

    QString lastError() const { return m_lastError; }

signals:
    void runningChanged(bool running);
    void voiceFinished(quewi::audio::VoiceId id);
    void engineError(const QString &reason);

private:
    class Mixer;

    struct DeviceContext {
        QAudioDevice                  device;
        std::unique_ptr<QAudioSink>   sink;
        std::unique_ptr<Mixer>        mixer;
        int                           sampleRate = 48000;
        int                           channels   = 2;
    };

    DeviceContext *ensureContextForDevice(const QAudioDevice &device);
    DeviceContext *contextForDeviceId(const QByteArray &deviceId);
    QAudioDevice   resolveDevice(const QByteArray &deviceId) const;

    void onMixerVoiceFinished(VoiceId id);
    // Called when the system default audio output changes (headphones
    // plugged in, Control-Center / sound-prefs output switch). Drops
    // any context bound to the old default so the next GO rebuilds on
    // the new device instead of playing into a dead sink.
    void onSystemDefaultOutputChanged();
    // Remove any device context whose sink has stopped (dead device /
    // CoreAudio error), so it isn't handed back to a later GO.
    void pruneDeadContexts();

    QMediaDevices                              *m_deviceWatcher = nullptr;
    bool                                        m_followSystemDefault = true;
    QAudioDevice                                m_defaultDevice;
    std::vector<std::unique_ptr<DeviceContext>> m_contexts;
    std::atomic<bool>                           m_running{false};
    QString                                     m_lastError;
};

} // namespace quewi::audio
