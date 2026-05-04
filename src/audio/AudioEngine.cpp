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

double linearToDb(double lin) {
    if (lin <= 1e-9) return -90.0;
    return 20.0 * std::log10(lin);
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

    VoiceId addVoice(VoiceId id,
                     std::shared_ptr<const AudioFile> file,
                     const VoiceParams &params,
                     const QByteArray &deviceId)
    {
        if (!file) return 0;

        Voice v;
        v.id = id;
        v.deviceId = deviceId;
        const double srcSr = file->sampleRate();
        v.file = std::move(file);
        v.gain.store(dbToLinear(params.gainDb), std::memory_order_relaxed);
        v.currentGain.store(dbToLinear(params.gainDb), std::memory_order_relaxed);
        v.targetGain.store(dbToLinear(params.gainDb), std::memory_order_relaxed);
        v.pan.store(std::clamp(params.pan, -1.0, 1.0), std::memory_order_relaxed);
        v.fadeInSamples = static_cast<qint64>(params.fadeInSeconds  * m_outputSampleRate);
        v.fadeOutOnStop = static_cast<qint64>(params.fadeOutSeconds * m_outputSampleRate);
        v.loop = params.loop;

        v.readPos = static_cast<qint64>(params.trimInSeconds * srcSr);
        if (params.trimOutSeconds > 0.0) {
            v.endFrame = static_cast<qint64>(params.trimOutSeconds * srcSr);
        }
        v.srcSampleRate = srcSr;

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

    bool setVoiceGain(VoiceId id, double gainDb)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto &v : m_voices) {
            if (v.id == id) {
                const double lin = dbToLinear(gainDb);
                v.gain.store(lin, std::memory_order_relaxed);
                v.targetGain.store(lin, std::memory_order_relaxed);
                v.gainFadeSamples = 0; // cancel any fade
                v.gainFadeCounter = 0;
                return true;
            }
        }
        return false;
    }

    bool setVoicePan(VoiceId id, double pan)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto &v : m_voices) {
            if (v.id == id) {
                v.pan.store(std::clamp(pan, -1.0, 1.0), std::memory_order_relaxed);
                return true;
            }
        }
        return false;
    }

    bool hasVoice(VoiceId id) const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (const auto &v : m_voices) if (v.id == id) return true;
        return false;
    }

    void appendActiveVoices(QList<ActiveVoice> &out) const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (const auto &v : m_voices) {
            if (!v.file) continue;
            ActiveVoice a;
            a.id = v.id;
            a.deviceId = v.deviceId;
            a.gainDb = linearToDb(v.gain.load(std::memory_order_relaxed));
            a.pan    = v.pan.load(std::memory_order_relaxed);
            a.positionSeconds = (v.srcSampleRate > 0)
                ? static_cast<double>(v.readPos) / v.srcSampleRate : 0.0;
            const qint64 endFrame = (v.endFrame > 0) ? v.endFrame
                                                     : v.file->frameCount();
            a.durationSeconds = (v.srcSampleRate > 0 && endFrame > 0)
                ? static_cast<double>(endFrame) / v.srcSampleRate : 0.0;
            a.loop = v.loop;
            a.peakLeft  = v.peakL.load(std::memory_order_relaxed);
            a.peakRight = v.peakR.load(std::memory_order_relaxed);
            out.append(a);
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
        QByteArray deviceId;
        std::shared_ptr<const AudioFile> file;
        qint64   readPos = 0;
        qint64   endFrame = 0;
        double   srcSampleRate = 0.0;
        std::atomic<double> gain{1.0};
        std::atomic<double> currentGain{1.0};
        std::atomic<double> targetGain{1.0};
        std::atomic<double> pan{0.0};
        std::atomic<float>  peakL{0.f};
        std::atomic<float>  peakR{0.f};
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

        Voice() = default;
        Voice(const Voice &) = delete;
        Voice &operator=(const Voice &) = delete;
        Voice(Voice &&other) noexcept
            : id(other.id)
            , deviceId(std::move(other.deviceId))
            , file(std::move(other.file))
            , readPos(other.readPos)
            , endFrame(other.endFrame)
            , srcSampleRate(other.srcSampleRate)
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
            pan.store(other.pan.load(std::memory_order_relaxed), std::memory_order_relaxed);
            peakL.store(other.peakL.load(std::memory_order_relaxed), std::memory_order_relaxed);
            peakR.store(other.peakR.load(std::memory_order_relaxed), std::memory_order_relaxed);
        }
        Voice &operator=(Voice &&other) noexcept {
            if (this == &other) return *this;
            id = other.id;
            deviceId = std::move(other.deviceId);
            file = std::move(other.file);
            readPos = other.readPos;
            endFrame = other.endFrame;
            srcSampleRate = other.srcSampleRate;
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
            pan.store(other.pan.load(std::memory_order_relaxed), std::memory_order_relaxed);
            peakL.store(other.peakL.load(std::memory_order_relaxed), std::memory_order_relaxed);
            peakR.store(other.peakR.load(std::memory_order_relaxed), std::memory_order_relaxed);
            return *this;
        }
    };

    AudioEngine *m_engine;
    int m_outputSampleRate = 48000;
    int m_outputChannels = 2;

    mutable std::mutex m_mutex;
    std::vector<Voice> m_voices;
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
            const qint64 effectiveEnd = (v.endFrame > 0)
                ? std::min(v.endFrame, totalFrames) : totalFrames;

            const double srcSr = v.file->sampleRate();
            const double dstSr = m_outputSampleRate;
            const double rate  = srcSr / dstSr;

            // Pan read once per buffer — UI changes lag by buffer length but
            // that's perceptually fine and avoids per-sample atomic loads.
            // Pre-multiply the +3 dB constant-power constant so the per-frame
            // path is a single multiply per channel.
            const double curPan = v.pan.load(std::memory_order_relaxed);
            const double panT   = (curPan + 1.0) * 0.5;
            constexpr double kSqrt2 = 1.41421356237309504880;
            const float leftPan  = float(std::cos(panT * M_PI / 2.0) * kSqrt2);
            const float rightPan = float(std::sin(panT * M_PI / 2.0) * kSqrt2);

            float bufPeakL = 0.f, bufPeakR = 0.f;
            for (qint64 f = 0; f < framesWanted; ++f) {
                if (v.finished) break;

                const double srcF = static_cast<double>(v.readPos) + f * rate;
                const qint64 i0 = static_cast<qint64>(srcF);
                const double frac = srcF - static_cast<double>(i0);
                qint64 i1 = i0 + 1;

                if (v.loop) {
                    if (i1 >= effectiveEnd) i1 -= effectiveEnd;
                } else if (i1 >= effectiveEnd) {
                    i1 = effectiveEnd - 1;
                }
                if (i0 >= effectiveEnd) {
                    if (!v.loop) {
                        v.finished = true;
                        break;
                    }
                }

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

                const double finalGain = cur * envGain;

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
                    float panGain = 1.f;
                    if (outChans == 2) {
                        panGain = (oc == 0) ? leftPan : rightPan;
                    }
                    const float written = static_cast<float>(s * finalGain) * panGain;
                    out[f * outChans + oc] += written;
                    const float a = std::fabs(written);
                    if (oc == 0) {
                        if (a > bufPeakL) bufPeakL = a;
                    } else if (oc == 1) {
                        if (a > bufPeakR) bufPeakR = a;
                    }
                }
            }
            v.peakL.store(bufPeakL, std::memory_order_relaxed);
            v.peakR.store(bufPeakR, std::memory_order_relaxed);

            const qint64 advanced = static_cast<qint64>(framesWanted * rate);
            v.readPos += advanced;
            if (v.loop && effectiveEnd > 0) v.readPos %= effectiveEnd;
            else if (v.readPos >= effectiveEnd) v.finished = true;

            if (v.stopRequested) v.fadeOutCounter += framesWanted;

            if (v.finished) finished.push_back(v.id);
        }

        if (!finished.empty()) {
            m_voices.erase(std::remove_if(m_voices.begin(), m_voices.end(),
                [&](const Voice &v) { return v.finished; }), m_voices.end());
        }
    }

    for (auto id : finished) {
        QMetaObject::invokeMethod(m_engine, "onMixerVoiceFinished",
            Qt::QueuedConnection, Q_ARG(quewi::audio::VoiceId, id));
    }

    return framesWanted * frameBytes;
}

// ---------------------------------------------------------------------
// AudioEngine
// ---------------------------------------------------------------------

namespace {
std::atomic<VoiceId> s_globalNextVoiceId{0};
}

AudioEngine::AudioEngine(QObject *parent)
    : QObject(parent)
    , m_defaultDevice(QMediaDevices::defaultAudioOutput())
{
    qRegisterMetaType<quewi::audio::VoiceId>("quewi::audio::VoiceId");
}

AudioEngine::~AudioEngine() = default;

void AudioEngine::setDefaultOutputDevice(const QAudioDevice &device)
{
    if (device.id() == m_defaultDevice.id()) return;
    m_defaultDevice = device;
    // If the previous default has an open context, leave it for any voices
    // still running. Future fire()s with empty deviceId will pick up the
    // new default lazily.
}

QAudioDevice AudioEngine::resolveDevice(const QByteArray &deviceId) const
{
    if (deviceId.isEmpty()) return m_defaultDevice;
    for (const auto &dev : QMediaDevices::audioOutputs()) {
        if (dev.id() == deviceId) return dev;
    }
    return m_defaultDevice; // fallback if device went away
}

AudioEngine::DeviceContext *AudioEngine::contextForDeviceId(const QByteArray &deviceId)
{
    for (auto &ctx : m_contexts) {
        if (ctx->device.id() == deviceId) return ctx.get();
    }
    return nullptr;
}

AudioEngine::DeviceContext *AudioEngine::ensureContextForDevice(const QAudioDevice &device)
{
    if (device.isNull()) {
        m_lastError = tr("No audio output device available");
        emit engineError(m_lastError);
        return nullptr;
    }

    const QByteArray key = device.id();
    if (auto *existing = contextForDeviceId(key)) return existing;

    QAudioFormat fmt;
    fmt.setSampleFormat(QAudioFormat::Float);
    fmt.setSampleRate(48000);
    fmt.setChannelCount(2);
    if (!device.isFormatSupported(fmt)) {
        fmt = device.preferredFormat();
        if (fmt.sampleFormat() != QAudioFormat::Float) {
            fmt.setSampleFormat(QAudioFormat::Float);
        }
    }
    if (!device.isFormatSupported(fmt)) {
        m_lastError = tr("Device '%1' rejects float32 format").arg(device.description());
        emit engineError(m_lastError);
        return nullptr;
    }

    auto ctx = std::make_unique<DeviceContext>();
    ctx->device = device;
    ctx->sampleRate = fmt.sampleRate();
    ctx->channels   = fmt.channelCount();
    ctx->mixer = std::make_unique<Mixer>(this);
    ctx->mixer->configure(fmt.sampleRate(), fmt.channelCount());
    ctx->mixer->open(QIODevice::ReadOnly);
    ctx->sink = std::make_unique<QAudioSink>(device, fmt, this);
    // 128 KB ≈ 340 ms at 48k stereo float. Generous so Windows context
    // switches when the user opens another window or alt-tabs out don't
    // starve the audio callback. The Phase-7 GoEngine will tighten this
    // once we have sample-accurate scheduling.
    ctx->sink->setBufferSize(131072);

    auto *ctxPtr = ctx.get();
    connect(ctx->sink.get(), &QAudioSink::stateChanged, this,
        [this, ctxPtr](QAudio::State s) {
            if (s == QAudio::StoppedState && ctxPtr->sink) {
                const auto err = ctxPtr->sink->error();
                if (err != QAudio::NoError) {
                    m_lastError = tr("Audio sink error on '%1': %2")
                        .arg(ctxPtr->device.description())
                        .arg(static_cast<int>(err));
                    emit engineError(m_lastError);
                }
            }
        });

    ctx->sink->start(ctx->mixer.get());
    if (ctx->sink->error() != QAudio::NoError) {
        m_lastError = tr("Failed to start audio sink for '%1' (error %2)")
            .arg(device.description())
            .arg(static_cast<int>(ctx->sink->error()));
        emit engineError(m_lastError);
        return nullptr;
    }

    m_contexts.push_back(std::move(ctx));
    (void)key;

    if (!m_running.load()) {
        m_running.store(true);
        emit runningChanged(true);
    }
    return ctxPtr;
}

bool AudioEngine::ensureRunning()
{
    m_lastError.clear();
    if (m_defaultDevice.isNull()) m_defaultDevice = QMediaDevices::defaultAudioOutput();
    return ensureContextForDevice(m_defaultDevice) != nullptr;
}

void AudioEngine::shutdown()
{
    for (auto &ctx : m_contexts) {
        if (ctx->sink) ctx->sink->stop();
        ctx->sink.reset();
        if (ctx->mixer) ctx->mixer->close();
        ctx->mixer.reset();
    }
    m_contexts.clear();
    if (m_running.load()) {
        m_running.store(false);
        emit runningChanged(false);
    }
}

VoiceId AudioEngine::fire(const std::shared_ptr<const AudioFile> &file,
                          const VoiceParams &params)
{
    if (!file || file->state() != AudioFile::State::Loaded) return 0;

    const QAudioDevice dev = resolveDevice(params.outputDeviceId);
    auto *ctx = ensureContextForDevice(dev);
    if (!ctx) return 0;

    const VoiceId id = ++s_globalNextVoiceId;
    return ctx->mixer->addVoice(id, file, params, dev.id());
}

void AudioEngine::stop(VoiceId id, double fadeOutSeconds)
{
    for (auto &ctx : m_contexts) {
        if (ctx->mixer && ctx->mixer->hasVoice(id)) {
            ctx->mixer->stopVoice(id, fadeOutSeconds);
            return;
        }
    }
}

void AudioEngine::stopAll(double fadeOutSeconds)
{
    for (auto &ctx : m_contexts) {
        if (ctx->mixer) ctx->mixer->stopAll(fadeOutSeconds);
    }
}

void AudioEngine::fadeGain(VoiceId id, double targetDb, double durationSeconds)
{
    for (auto &ctx : m_contexts) {
        if (ctx->mixer && ctx->mixer->hasVoice(id)) {
            ctx->mixer->fadeGain(id, targetDb, durationSeconds);
            return;
        }
    }
}

void AudioEngine::setVoiceGain(VoiceId id, double gainDb)
{
    for (auto &ctx : m_contexts) {
        if (ctx->mixer && ctx->mixer->setVoiceGain(id, gainDb)) return;
    }
}

void AudioEngine::setVoicePan(VoiceId id, double pan)
{
    for (auto &ctx : m_contexts) {
        if (ctx->mixer && ctx->mixer->setVoicePan(id, pan)) return;
    }
}

QList<ActiveVoice> AudioEngine::activeVoices() const
{
    QList<ActiveVoice> out;
    for (const auto &ctx : m_contexts) {
        if (ctx->mixer) ctx->mixer->appendActiveVoices(out);
    }
    return out;
}

int AudioEngine::activeVoiceCount() const
{
    int n = 0;
    for (const auto &ctx : m_contexts) if (ctx->mixer) n += ctx->mixer->activeCount();
    return n;
}

void AudioEngine::onMixerVoiceFinished(VoiceId id)
{
    emit voiceFinished(id);
}

} // namespace quewi::audio
