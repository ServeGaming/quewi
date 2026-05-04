#pragma once

#include <QAudioDevice>
#include <QIODevice>
#include <QObject>
#include <QPointer>
#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

class QAudioSink;

namespace quewi::audio {

class AudioFile;

using VoiceId = quint64;

// What the engine plays. Owned by the engine. Read by the mixer thread
// via mutex-guarded snapshot taken at the top of each readData() call —
// inside the callback, only atomic gain and the immutable AudioFile
// samples are touched.
struct VoiceParams {
    double gainDb        = 0.0;
    double fadeInSeconds  = 0.0;
    double fadeOutSeconds = 0.0;
    bool   loop           = false;
};

// The engine owns one output device and any number of active voices.
// Devices are opened lazily — the first fire() call brings the audio
// hardware up; idle quewi keeps the audio thread silent.
class AudioEngine : public QObject {
    Q_OBJECT
public:
    explicit AudioEngine(QObject *parent = nullptr);
    ~AudioEngine() override;

    // Pick the output device. Defaults to the system default. Must be
    // called before the device is opened, OR triggers a stop+restart.
    void setOutputDevice(const QAudioDevice &device);
    QAudioDevice outputDevice() const { return m_outputDevice; }

    // Spin up the device explicitly (otherwise lazy at first fire()).
    bool ensureRunning();
    void shutdown();

    // Start a new voice playing the given file. Returns 0 on failure.
    VoiceId fire(const std::shared_ptr<const AudioFile> &file,
                 const VoiceParams &params);

    // Active voice control.
    void stop(VoiceId id, double fadeOutSeconds = 0.05);
    void stopAll(double fadeOutSeconds = 0.05);

    // Adjust a voice's gain over time (used by Fade cues).
    void fadeGain(VoiceId id, double targetDb, double durationSeconds);

    bool isRunning() const { return m_running.load(); }
    int  outputSampleRate() const { return m_outputSampleRate.load(); }
    int  outputChannels()   const { return m_outputChannels.load(); }
    int  activeVoiceCount() const;

signals:
    void runningChanged(bool running);
    void voiceFinished(quewi::audio::VoiceId id);

private:
    class Mixer;

    void onMixerVoiceFinished(VoiceId id);

    QAudioDevice                  m_outputDevice;
    std::unique_ptr<QAudioSink>   m_sink;
    std::unique_ptr<Mixer>        m_mixer;
    std::atomic<bool>             m_running{false};
    std::atomic<int>              m_outputSampleRate{0};
    std::atomic<int>              m_outputChannels{0};
};

} // namespace quewi::audio
