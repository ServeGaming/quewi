#include "audio/AudioEngine.h"

#include "audio/AudioFile.h"

#include <QAudioFormat>
#include <QAudioSink>
#include <QMediaDevices>
#include <QtMath>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>

namespace quewi::audio {

namespace {

double dbToLinear(double db) {
    return std::pow(10.0, db / 20.0);
}

} // namespace

// ---------------------------------------------------------------------
// Mixer — the QIODevice the QAudioSink pulls from.
// ---------------------------------------------------------------------

class AudioEngine::Mixer : public QIODevice {
public:
    explicit Mixer(AudioEngine *engine) : m_engine(engine) {}
    ~Mixer() override = default;

    void configure(int outputSampleRate, int outputChannels)
    {
        m_outputSampleRate = outputSampleRate;
        m_outputChannels = outputChannels;
    }

    VoiceId addVoice(std::shared_ptr<const AudioFile> file, const VoiceParams &params)
    {
        if (!file) return 0;

        Voice v;
        v.id = ++m_nextId;
        v.file = std::move(file);
        v.gain.store(dbToLinear(params.gainDb), std::memory_order_relaxed);
        v.fadeInSamples = static_cast<qint64>(params.fadeInSeconds  * m_outputSampleRate);
        v.fadeOutOnStop = static_cast<qint64>(params.fadeOutSeconds * m_outputSampleRate);
        v.loop = params.loop;

        std::lock_guard<std::mutex> lock(m_mutex);
        m_voices.push_back(std::move(v));
        return m_voices.back().id;
    }

    void stopVoice(VoiceId id, double fadeOutSeconds)
    {
        const qint64 samples = static_cast<qint64>(fadeOutSeconds * m_outputSampleRate);
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto &v : m_voices) {
            if (v.id == id && !v.stopRequested) {
                v.stopRequested = true;
                v.fadeOutSamples = std::max<qint64>(samples, 1);
                v.fadeOutFromGain = v.currentGain.load(std::memory_order_relaxed);
                v.fadeOutCounter = 0;
            }
        }
    }

    void stopAll(double fadeOutSeconds)
    {
        const qint64 samples = static_cast<qint64>(fadeOutSeconds * m_outputSampleRate);
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto &v : m_voices) {
            if (!v.stopRequested) {
                v.stopRequested = true;
                v.fadeOutSamples = std::max<qint64>(samples, 1);
                v.fadeOutFromGain = v.currentGain.load(std::memory_order_relaxed);
                v.fadeOutCounter = 0;
            }
        }
    }

    void fadeGain(VoiceId id, double targetDb, double durationSeconds)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto &v : m_voices) {
            if (v.id == id) {
                v.targetGain.store(dbToLinear(targetDb), std::memory_order_relaxed);
                v.gainFadeSamples = std::max<qint64>(
                    static_cast<qint64>(durationSeconds * m_outputSampleRate), 1);
                v.gainFadeCounter = 0;
                v.gainFadeFrom = v.currentGain.load(std::memory_order_relaxed);
            }
        }
    }

    int activeCount() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return static_cast<int>(m_voices.size());
    }

    qint64 readData(char *data, qint64 maxlen) override;
    qint64 writeData(const char *, qint64) override { return -1; } // output-only

    bool isSequential() const override { return true; }
    qint64 bytesAvailable() const override { return std::numeric_limits<qint64>::max() / 2; }

private:
    struct Voice {
        VoiceId  id = 0;
        std::shared_ptr<const AudioFile> file;
        qint64   readPos = 0;          // frames into the file
        std::atomic<double> gain{1.0}; // user target gain (linear)
        std::atomic<double> currentGain{1.0}; // smoothed actual gain
        std::atomic<double> targetGain{1.0};  // for fade
        qint64   gainFadeSamples = 0;
        qint64   gainFadeCounter = 0;
        double   gainFadeFrom = 1.0;
        qint64   fadeInSamples = 0;
        qint64   fadeOutOnStop = 0;
        bool     stopRequested = false;
        qint64   fadeOutSamples = 0;
        qint64   fadeOutCounter = 0;
        double   fadeOutFromGain = 1.0;
        bool     loop = false;
        bool     finished = false;

        // Voices are not copyable — atomics — but we move them around in
        // the vector when erasing. Custom move ctor.
        Voice() = default;
        Voice(const Voice &) = delete;
        Voice &operator=(const Voice &) = delete;
        Voice(Voice &&other) noexcept
            : id(other.id)
            , file(std::move(other.file))
            , readPos(other.readPos)
            , gainFadeSamples(other.gainFadeSamples)
            , gainFadeCounter(other.gainFadeCounter)
            , gainFadeFrom(other.gainFadeFrom)
            , fadeInSamples(other.fadeInSamples)
            , fadeOutOnStop(other.fadeOutOnStop)
            , stopRequested(other.stopRequested)
            , fadeOutSamples(other.fadeOutSamples)
            , fadeOutCounter(other.fadeOutCounter)
            , fadeOutFromGain(other.fadeOutFromGain)
            , loop(other.loop)
            , finished(other.finished)
        {
            gain.store(other.gain.load(std::memory_order_relaxed), std::memory_order_relaxed);
            currentGain.store(other.currentGain.load(std::memory_order_relaxed), std::memory_order_relaxed);
            targetGain.store(other.targetGain.load(std::memory_order_relaxed), std::memory_order_relaxed);
        }
        Voice &operator=(Voice &&other) noexcept {
            if (this == &other) return *this;
            id = other.id;
            file = std::move(other.file);
            readPos = other.readPos;
            gainFadeSamples = other.gainFadeSamples;
            gainFadeCounter = other.gainFadeCounter;
            gainFadeFrom = other.gainFadeFrom;
            fadeInSamples = other.fadeInSamples;
            fadeOutOnStop = other.fadeOutOnStop;
            stopRequested = other.stopRequested;
            fadeOutSamples = other.fadeOutSamples;
            fadeOutCounter = other.fadeOutCounter;
            fadeOutFromGain = other.fadeOutFromGain;
            loop = other.loop;
            finished = other.finished;
            gain.store(other.gain.load(std::memory_order_relaxed), std::memory_order_relaxed);
            currentGain.store(other.currentGain.load(std::memory_order_relaxed), std::memory_order_relaxed);
            targetGain.store(other.targetGain.load(std::memory_order_relaxed), std::memory_order_relaxed);
            return *this;
        }
    };

    AudioEngine *m_engine;
    int m_outputSampleRate = 48000;
    int m_outputChannels = 2;

    mutable std::mutex m_mutex;
    std::vector<Voice> m_voices;
    std::atomic<VoiceId> m_nextId{0};
};

qint64 AudioEngine::Mixer::readData(char *data, qint64 maxlen)
{
    if (maxlen <= 0) return 0;

    const int outChans = m_outputChannels;
    const int frameBytes = sizeof(float) * outChans;
    qint64 framesWanted = maxlen / frameBytes;
    if (framesWanted <= 0) return 0;

    auto *out = reinterpret_cast<float *>(data);
    std::memset(out, 0, framesWanted * frameBytes);

    std::vector<VoiceId> finished;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto &v : m_voices) {
            if (!v.file || v.file->state() != AudioFile::State::Loaded) continue;

            const auto &samples = v.file->samples();
            const int   inChans  = v.file->channelCount();
            const qint64 totalFrames = v.file->frameCount();
            if (inChans <= 0 || totalFrames <= 0) continue;

            // Sample-rate mismatch: simple constant-stride resample with
            // linear interpolation. Adequate for v1; phase 7 polish can
            // upgrade to a band-limited filter.
            const double srcSr = v.file->sampleRate();
            const double dstSr = m_outputSampleRate;
            const double rate  = srcSr / dstSr;

            for (qint64 f = 0; f < framesWanted; ++f) {
                if (v.finished) break;

                // Source position in fractional frames.
                const double srcF = static_cast<double>(v.readPos) + f * rate;
                const qint64 i0 = static_cast<qint64>(srcF);
                const double frac = srcF - static_cast<double>(i0);
                qint64 i1 = i0 + 1;

                if (v.loop) {
                    if (i1 >= totalFrames) i1 -= totalFrames;
                } else if (i1 >= totalFrames) {
                    i1 = totalFrames - 1;
                }
                if (i0 >= totalFrames) {
                    if (v.loop) {
                        // Loop: wrap. (Already handled below as readPos modulo at end.)
                    } else {
                        v.finished = true;
                        break;
                    }
                }

                // Compute envelope gain.
                double envGain = 1.0;
                const qint64 absSamp = v.readPos + f * static_cast<qint64>(rate);
                if (v.fadeInSamples > 0 && absSamp < v.fadeInSamples) {
                    envGain *= static_cast<double>(absSamp) / static_cast<double>(v.fadeInSamples);
                }
                if (v.stopRequested) {
                    const qint64 cnt = v.fadeOutCounter + f;
                    if (cnt >= v.fadeOutSamples) {
                        v.finished = true;
                    } else {
                        envGain *= 1.0 - static_cast<double>(cnt) / static_cast<double>(v.fadeOutSamples);
                    }
                }

                // Smooth gain fade.
                double cur = v.currentGain.load(std::memory_order_relaxed);
                if (v.gainFadeSamples > 0 && v.gainFadeCounter < v.gainFadeSamples) {
                    const double t = static_cast<double>(v.gainFadeCounter) / v.gainFadeSamples;
                    cur = v.gainFadeFrom + (v.targetGain.load() - v.gainFadeFrom) * t;
                    v.currentGain.store(cur, std::memory_order_relaxed);
                    v.gainFadeCounter += static_cast<qint64>(rate);
                } else {
                    cur = v.gain.load(std::memory_order_relaxed);
                    v.currentGain.store(cur, std::memory_order_relaxed);
                }

                const double total = cur * envGain * v.fadeOutFromGain
                                     / (v.fadeOutFromGain == 0.0 ? 1.0 : v.fadeOutFromGain);
                // The line above intentionally cancels: we kept fadeOutFromGain
                // for documentation but the multiplication factor is already in
                // envGain via the linear ramp. Real factor:
                const double finalGain = cur * envGain;

                // Mix into output, channel-mapping:
                //   - source mono → fan to all output channels
                //   - source stereo → first two output channels; extras silent
                //   - matching channel count → 1:1
                for (int oc = 0; oc < outChans; ++oc) {
                    int srcCh;
                    if (inChans == 1)               srcCh = 0;
                    else if (oc < inChans)          srcCh = oc;
                    else                             srcCh = oc % inChans;

                    auto sample = [&](qint64 idx) -> float {
                        if (idx < 0) idx = 0;
                        if (idx >= totalFrames) idx = totalFrames - 1;
                        return samples[static_cast<size_t>(idx) * static_cast<size_t>(inChans)
                                       + static_cast<size_t>(srcCh)];
                    };
                    const float s0 = sample(i0);
                    const float s1 = sample(i1);
                    const float s  = s0 + static_cast<float>(frac) * (s1 - s0);
                    out[f * outChans + oc] += static_cast<float>(s * finalGain);
                }

                (void)total; // silence unused-variable warning
            }

            // Advance read position by the resampled amount.
            const qint64 advanced = static_cast<qint64>(framesWanted * rate);
            v.readPos += advanced;
            if (v.loop && totalFrames > 0) v.readPos %= totalFrames;
            else if (v.readPos >= totalFrames) v.finished = true;

            if (v.stopRequested) v.fadeOutCounter += framesWanted;

            if (v.finished) finished.push_back(v.id);
        }

        // Drop finished voices.
        if (!finished.empty()) {
            m_voices.erase(std::remove_if(m_voices.begin(), m_voices.end(),
                [&](const Voice &v) { return v.finished; }), m_voices.end());
        }
    }

    // Notify on the main thread.
    for (auto id : finished) {
        QMetaObject::invokeMethod(m_engine, "onMixerVoiceFinished",
            Qt::QueuedConnection, Q_ARG(quewi::audio::VoiceId, id));
    }

    return framesWanted * frameBytes;
}

// ---------------------------------------------------------------------
// AudioEngine
// ---------------------------------------------------------------------

AudioEngine::AudioEngine(QObject *parent)
    : QObject(parent)
    , m_outputDevice(QMediaDevices::defaultAudioOutput())
{
    qRegisterMetaType<quewi::audio::VoiceId>("quewi::audio::VoiceId");
}

AudioEngine::~AudioEngine() = default;

void AudioEngine::setOutputDevice(const QAudioDevice &device)
{
    if (device.id() == m_outputDevice.id()) return;
    m_outputDevice = device;
    if (m_running.load()) {
        shutdown();
        ensureRunning();
    }
}

bool AudioEngine::ensureRunning()
{
    if (m_running.load()) return true;

    QAudioFormat fmt;
    fmt.setSampleFormat(QAudioFormat::Float);
    fmt.setSampleRate(48000);
    fmt.setChannelCount(2);

    if (!m_outputDevice.isFormatSupported(fmt)) {
        // Fall back to whatever the device prefers.
        fmt = m_outputDevice.preferredFormat();
        if (fmt.sampleFormat() != QAudioFormat::Float) {
            fmt.setSampleFormat(QAudioFormat::Float);
        }
    }

    m_outputSampleRate.store(fmt.sampleRate());
    m_outputChannels.store(fmt.channelCount());

    m_mixer = std::make_unique<Mixer>(this);
    m_mixer->configure(fmt.sampleRate(), fmt.channelCount());
    m_mixer->open(QIODevice::ReadOnly);

    m_sink = std::make_unique<QAudioSink>(m_outputDevice, fmt, this);
    m_sink->setBufferSize(4096); // ~85 ms at 48k stereo float; tighten later
    m_sink->start(m_mixer.get());

    m_running.store(true);
    emit runningChanged(true);
    return true;
}

void AudioEngine::shutdown()
{
    if (!m_running.load()) return;
    if (m_sink) m_sink->stop();
    m_sink.reset();
    if (m_mixer) m_mixer->close();
    m_mixer.reset();
    m_running.store(false);
    emit runningChanged(false);
}

VoiceId AudioEngine::fire(const std::shared_ptr<const AudioFile> &file,
                          const VoiceParams &params)
{
    if (!ensureRunning()) return 0;
    if (!file || file->state() != AudioFile::State::Loaded) return 0;
    return m_mixer->addVoice(file, params);
}

void AudioEngine::stop(VoiceId id, double fadeOutSeconds)
{
    if (!m_mixer) return;
    m_mixer->stopVoice(id, fadeOutSeconds);
}

void AudioEngine::stopAll(double fadeOutSeconds)
{
    if (!m_mixer) return;
    m_mixer->stopAll(fadeOutSeconds);
}

void AudioEngine::fadeGain(VoiceId id, double targetDb, double durationSeconds)
{
    if (!m_mixer) return;
    m_mixer->fadeGain(id, targetDb, durationSeconds);
}

int AudioEngine::activeVoiceCount() const
{
    return m_mixer ? m_mixer->activeCount() : 0;
}

void AudioEngine::onMixerVoiceFinished(VoiceId id)
{
    emit voiceFinished(id);
}

} // namespace quewi::audio
