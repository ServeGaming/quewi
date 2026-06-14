#include "audio/AudioEngine.h"

#include "audio/AudioEffect.h"
#include "audio/AudioFile.h"
#include "audio/Db.h"

#include <QAudioFormat>
#include <QAudioSink>
#include <QHash>
#include <QMediaDevices>
#include <QtMath>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>

namespace quewi::audio {

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
        // Capture an immutable snapshot of the decoded buffer for the
        // voice's lifetime. The audio callback reads only this snapshot
        // — concurrent edits on the GUI thread (clear / reverseSamples /
        // normaliseSamples / re-load) publish a fresh snapshot but never
        // touch the one this voice already holds.
        auto buf = file->snapshot();
        if (!buf) return 0;

        Voice v;
        v.id = id;
        v.deviceId = deviceId;
        const double srcSr = buf->sampleRate;
        v.file = std::move(file);
        v.buf  = std::move(buf);
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
        v.channelGains  = params.channelGains;
        v.outputGains   = params.outputGains;
        v.peakPerChannel.fill(0.0f, m_outputChannels);

        // Effects chain — prepared HERE (the GUI/fire thread) because
        // Reverb/Delay prepare() allocate. Cache raw pointers for the audio
        // callback and keep the owning shared_ptrs in m_voiceFx so the
        // AudioEffect objects are destroyed on this thread (reapEffects),
        // never on the audio callback. Pre-size the scratch once.
        if (!params.effects.empty()) {
            auto chain = params.effects;   // this voice's own fresh chain
            v.effects.reserve(chain.size());
            for (const auto &fx : chain) {
                if (!fx) continue;
                fx->prepare(m_outputSampleRate);
                fx->reset();
                v.effects.push_back(fx.get());
            }
            if (!v.effects.empty()) {
                v.fxScratch.assign(static_cast<size_t>(kFxScratchFrames) * 2, 0.f);
                m_voiceFx.insert(id, std::move(chain));
            }
        }

        std::lock_guard<std::mutex> lock(m_mutex);
        m_voices.push_back(std::move(v));
        return m_voices.back().id;
    }

    // GUI thread: drop a finished voice's effects chain so the AudioEffect
    // objects are destroyed HERE, never on the audio callback. No-op if this
    // mixer didn't own that voice id.
    void reapEffects(VoiceId id) { m_voiceFx.remove(id); }

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

    bool pauseVoice(VoiceId id)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto &v : m_voices) {
            if (v.id == id && !v.paused && !v.finished) {
                v.paused = true;
                return true;
            }
        }
        return false;
    }

    bool resumeVoice(VoiceId id)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        // Linear 5 ms attack ramp on resume so the silence-to-signal
        // transition isn't a hard click; long enough to mask the worst
        // discontinuities, short enough that operators don't perceive
        // a delay.
        const qint64 fadeSamples = static_cast<qint64>(0.005 * m_outputSampleRate);
        for (auto &v : m_voices) {
            if (v.id == id && v.paused) {
                v.paused = false;
                v.resumeFadeSamples = std::max<qint64>(fadeSamples, 1);
                v.resumeFadeCounter = 0;
                return true;
            }
        }
        return false;
    }

    bool isPausedVoice(VoiceId id) const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (const auto &v : m_voices) {
            if (v.id == id) return v.paused;
        }
        return false;
    }

    // Jump the voice's read position. seconds is measured from the
    // start of the source file (NOT from the trim-in point). Clamped
    // to [0, effectiveEnd-1] so the caller can't park us past the end.
    // Re-uses the resume-fade machinery to mask the splice with a 5 ms
    // attack ramp — same anti-click that pause→resume uses, since the
    // problem is identical (an instantaneous PCM discontinuity).
    bool seekVoice(VoiceId id, double seconds)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const qint64 fadeSamples =
            static_cast<qint64>(0.005 * m_outputSampleRate);
        for (auto &v : m_voices) {
            if (v.id != id) continue;
            if (!v.buf) return false;
            const double srcSr = v.buf->sampleRate;
            const qint64 total = v.buf->frameCount;
            if (srcSr <= 0.0 || total <= 0) return false;
            qint64 target = static_cast<qint64>(seconds * srcSr);
            const qint64 effEnd = (v.endFrame > 0)
                ? std::min(v.endFrame, total) : total;
            if (target < 0) target = 0;
            if (target >= effEnd) target = std::max<qint64>(0, effEnd - 1);
            v.readPos = target;
            // Short ramp on the next buffer so the discontinuity is
            // masked. Only meaningful if the voice isn't paused (a
            // paused voice will get the ramp at resume() time anyway).
            if (!v.paused) {
                v.resumeFadeSamples = std::max<qint64>(fadeSamples, 1);
                v.resumeFadeCounter = 0;
            }
            v.finished = false;
            return true;
        }
        return false;
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

    // Voice-cap eviction. Walks m_voices in insertion order (oldest
    // first) and fade-stops the first one that isn't already stopping.
    // Used by AudioEngine::fire when too many voices are already live.
    void stopOldestUnstopped(double fadeOutSeconds)
    {
        const qint64 samples = static_cast<qint64>(fadeOutSeconds * m_outputSampleRate);
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto &v : m_voices) {
            if (!v.stopRequested && !v.finished) {
                v.stopRequested = true;
                v.fadeOutSamples = std::max<qint64>(samples, 1);
                v.fadeOutFromGain = v.currentGain.load(std::memory_order_relaxed);
                v.fadeOutCounter = 0;
                return;
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

    bool setVoiceChannelGains(VoiceId id, const QList<float> &gains)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto &v : m_voices) {
            if (v.id == id) {
                v.channelGains = gains;
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

    // Flip the loop flag on a playing voice. The audio callback checks
    // v.loop every frame to decide whether to wrap readPos to start or
    // mark the voice finished — so toggling mid-playback takes effect
    // on the very next end-of-buffer. Guarded by the same mutex the
    // callback holds for its iteration so we can't tear a read.
    bool setVoiceLoop(VoiceId id, bool loop)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto &v : m_voices) {
            if (v.id == id) {
                v.loop = loop;
                // If we just turned loop ON and the voice was already
                // marked finished (e.g. ran past the end this buffer
                // but the cleanup pass hasn't removed it yet), un-mark
                // it. Otherwise the voice would be GC'd before the
                // next callback could wrap around.
                if (loop) v.finished = false;
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
            if (!v.buf) continue;
            ActiveVoice a;
            a.id = v.id;
            a.deviceId = v.deviceId;
            a.gainDb = linearToDb(v.gain.load(std::memory_order_relaxed));
            a.pan    = v.pan.load(std::memory_order_relaxed);
            a.positionSeconds = (v.srcSampleRate > 0)
                ? static_cast<double>(v.readPos) / v.srcSampleRate : 0.0;
            const qint64 endFrame = (v.endFrame > 0) ? v.endFrame
                                                     : v.buf->frameCount;
            a.durationSeconds = (v.srcSampleRate > 0 && endFrame > 0)
                ? static_cast<double>(endFrame) / v.srcSampleRate : 0.0;
            a.loop = v.loop;
            a.peakLeft  = v.peakL.load(std::memory_order_relaxed);
            a.peakRight = v.peakR.load(std::memory_order_relaxed);
            a.peakPerChannel = v.peakPerChannel;  // copy under m_mutex
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
        // Real-time-safe immutable PCM snapshot. The audio callback
        // reads exclusively through this; v.file is kept around only
        // for the GUI thread's queries (state, path) and to extend the
        // file's lifetime.
        std::shared_ptr<const AudioBufferSnapshot> buf;
        // Set once the voice has captured the file's FINAL snapshot
        // (one refresh after decode completes). After that the audio
        // callback never calls snapshot() again for this voice, so the
        // steady state is completely lock-free — important on macOS
        // where blocking the CoreAudio callback on the decoder thread's
        // publish mutex causes dropouts.
        bool     snapshotFinal = false;
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

        // Pause: when true, the mix loop emits silence for this voice
        // and leaves readPos untouched. resume() flips it back. A small
        // attack ramp on resume avoids the worst clicks; we don't ramp
        // on pause (operators expect instant silence on Pause).
        bool     paused = false;
        qint64   resumeFadeSamples = 0;
        qint64   resumeFadeCounter = 0;

        // Object-audio routing. Set once at fire() time from VBAP gains
        // for the cue's spatial position; the mixer reads it under the
        // same mutex snapshot as the rest of the voice. Empty means use
        // the legacy stereo pan path. Trajectories (animated positions)
        // would replace this with an atomic pointer-swap; v1 is static.
        QList<float> channelGains;

        // Per-output send gains for non-object-audio cues. Applied
        // AFTER the stereo pan; empty = passthrough. Mutated only at
        // addVoice() time today (live edits would land via a sibling
        // setVoiceOutputGains in a follow-up); under the mutex.
        QList<float> outputGains;

        // Per-channel peak — sized to the device's output channel count.
        // The mixer writes these every buffer; the UI thread reads under
        // the existing mutex snapshot so no atomics required.
        QList<float> peakPerChannel;

        // Per-cue effects chain — RAW non-owning pointers for the audio
        // callback. The shared_ptr owners live in Mixer::m_voiceFx (GUI
        // thread) and are destroyed there when the voice finishes, so the
        // callback never constructs or destroys an AudioEffect. Empty = dry.
        std::vector<AudioEffect *> effects;
        // Pre-allocated interleaved-stereo scratch (sized once at addVoice,
        // never resized on the audio thread) where the voice renders before
        // its chain runs. Only allocated for voices that carry effects.
        std::vector<float> fxScratch;

        Voice() = default;
        Voice(const Voice &) = delete;
        Voice &operator=(const Voice &) = delete;
        Voice(Voice &&other) noexcept
            : id(other.id)
            , deviceId(std::move(other.deviceId))
            , file(std::move(other.file))
            , buf(std::move(other.buf))
            , snapshotFinal(other.snapshotFinal)
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
            , channelGains(std::move(other.channelGains))
            , outputGains(std::move(other.outputGains))
            , peakPerChannel(std::move(other.peakPerChannel))
            , effects(std::move(other.effects))
            , fxScratch(std::move(other.fxScratch))
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
            buf  = std::move(other.buf);
            snapshotFinal = other.snapshotFinal;
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
            channelGains   = std::move(other.channelGains);
            outputGains    = std::move(other.outputGains);
            peakPerChannel = std::move(other.peakPerChannel);
            effects        = std::move(other.effects);
            fxScratch      = std::move(other.fxScratch);
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

    // Upper bound on a single readData block we run effects over. The
    // per-voice scratch is sized to this; if a callback ever asks for more
    // frames (never in practice) the voice plays dry for that buffer rather
    // than allocate on the audio thread.
    static constexpr qint64 kFxScratchFrames = 1 << 16;

    mutable std::mutex m_mutex;
    std::vector<Voice> m_voices;
    // Owns each voice's effects chain, keyed by VoiceId. GUI-thread only
    // (inserted in addVoice, removed in reapEffects); the audio callback only
    // ever reads the raw pointers cached in Voice::effects. Keeping ownership
    // here is what makes a finished voice's effects destruct on the GUI thread.
    QHash<VoiceId, std::vector<std::shared_ptr<AudioEffect>>> m_voiceFx;
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
            // Read exclusively through the immutable snapshot captured
            // at fire() time. v.buf can never be mutated after addVoice
            // returns, so the GUI thread is free to clear / reverse /
            // re-load the source file without affecting in-flight audio.
            if (!v.buf) continue;

            // Streaming refresh — if decode has produced more frames
            // since this voice captured its snapshot, swap to the
            // newer one. The new snapshot is a strict superset of
            // the old (decode is append-only) so readPos stays valid.
            //
            // CRITICAL on macOS: snapshot() takes a mutex that the
            // decoder thread also holds when publishing. The CoreAudio
            // render callback (which runs this loop) has a hard
            // deadline of a few ms, so blocking on that mutex risks a
            // dropout. Refresh every buffer WHILE decoding, then do
            // exactly ONE final refresh after decode completes (the
            // final snapshot is published right before state flips to
            // Loaded, so we must pick it up once to avoid truncating
            // the tail), and never lock again. After snapshotFinal the
            // steady state is completely lock-free. The snapshot()
            // mutex acquire synchronises memory, so the post-Loaded
            // refresh is guaranteed to see the final published buffer.
            if (v.file && !v.snapshotFinal && v.buf->frameCount > 0) {
                const bool stillLoading =
                    (v.file->state() == AudioFile::State::Loading);
                if (auto fresh = v.file->snapshot();
                    fresh && fresh->frameCount > v.buf->frameCount) {
                    v.buf = fresh;
                }
                if (!stillLoading) v.snapshotFinal = true;
            }

            // Paused voice — emit silence, don't advance readPos.
            // peakPerChannel decays via the UI smoothing.
            if (v.paused) {
                v.peakL.store(0.f, std::memory_order_relaxed);
                v.peakR.store(0.f, std::memory_order_relaxed);
                continue;
            }

            // Dereference the snapshot's shared_ptr<vector<float>> once;
            // the rest of the loop indexes through this reference. The
            // shared_ptr keeps the backing alive for the voice's
            // lifetime even if AudioFile cow-swaps to a bigger buffer.
            const auto &samples = *v.buf->samples;
            const int   inChans  = v.buf->channelCount;
            const qint64 totalFrames = v.buf->frameCount;
            if (inChans <= 0 || totalFrames <= 0) continue;
            const qint64 effectiveEnd = (v.endFrame > 0)
                ? std::min(v.endFrame, totalFrames) : totalFrames;
            // If decode is still in flight, treat end-of-snapshot as
            // "wait for more" instead of "voice finished". Reading
                // state() on the audio thread is a benign race: aligned
            // enum reads are atomic on x86/x64 and the worst-case
            // outcome is one buffer (~10 ms) of silence at a decode
            // catch-up boundary.
            const bool decodeOngoing = v.file
                && v.file->state() == AudioFile::State::Loading;

            const double srcSr = v.buf->sampleRate;
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
            // Per-channel peaks for this buffer. The mixer caps at 16
            // because the engine itself caps device channel count there.
            std::array<float, 16> bufPeaks{};
            // Tracks how many output frames were actually written, so
            // the readPos advance below only counts real progress —
            // important when the decode-ongoing stall breaks the loop
            // early and we must stay parked at end-of-decoded-data
            // until more arrives.
            qint64 framesWritten = 0;
            // Stereo-insert effects run only for non-object-audio voices that
            // carry a chain, and only when this block fits the pre-allocated
            // scratch (otherwise play dry for this buffer rather than allocate
            // on the RT thread). useChannelGains (object audio) takes priority
            // inside the loop, so an object-audio voice never hits this path.
            const bool hasEffects =
                !v.effects.empty()
                && framesWanted <= kFxScratchFrames
                && static_cast<qint64>(v.fxScratch.size()) >= framesWanted * 2;
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
                        // Stall instead of finish if decode is still
                        // in progress — the next buffer will refresh
                        // the snapshot and pick up new data.
                        if (decodeOngoing) break;
                        v.finished = true;
                        break;
                    }
                }
                ++framesWritten;

                double envGain = 1.0;
                // Source-frame playhead for this output frame. The cast
                // must wrap the PRODUCT f*rate — casting `rate` alone
                // truncated it to 0 for any downsampled material
                // (44.1 kHz file on a 48 kHz device, rate ≈ 0.919),
                // which froze the fade-in envelope flat within each
                // buffer.
                const qint64 absSamp = v.readPos + static_cast<qint64>(f * rate);
                if (v.fadeInSamples > 0 && absSamp < v.fadeInSamples) {
                    envGain *= static_cast<double>(absSamp) / static_cast<double>(v.fadeInSamples);
                }
                if (v.resumeFadeSamples > 0
                    && v.resumeFadeCounter < v.resumeFadeSamples) {
                    envGain *= static_cast<double>(v.resumeFadeCounter)
                             / static_cast<double>(v.resumeFadeSamples);
                    ++v.resumeFadeCounter;
                    if (v.resumeFadeCounter >= v.resumeFadeSamples) {
                        v.resumeFadeSamples = 0;
                    }
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
                    // gainFadeSamples is measured in OUTPUT frames
                    // (durationSeconds * outputSampleRate), and this loop
                    // runs once per output frame, so the counter advances
                    // by exactly 1 here. The old `+= (qint64)rate`
                    // truncated to 0 for downsampled material (fade never
                    // finished) and doubled for upsampled material.
                    ++v.gainFadeCounter;
                } else {
                    cur = v.gain.load(std::memory_order_relaxed);
                    v.currentGain.store(cur, std::memory_order_relaxed);
                }

                const double finalGain = cur * envGain;

                // Object-audio path: downmix the source to mono once per
                // frame, then scale by the precomputed VBAP gain per
                // output channel. Stereo/multichannel files become a
                // single point source in space — that's the standard
                // object-audio model. Trajectories arrive in 0.4.
                const bool useChannelGains =
                    !v.channelGains.isEmpty()
                    && v.channelGains.size() >= outChans;

                if (useChannelGains) {
                    auto sampleMono = [&](qint64 idx) -> float {
                        if (idx < 0) idx = 0;
                        if (idx >= totalFrames) idx = totalFrames - 1;
                        float acc = 0.f;
                        for (int sc = 0; sc < inChans; ++sc) {
                            acc += samples[static_cast<size_t>(idx) * static_cast<size_t>(inChans)
                                           + static_cast<size_t>(sc)];
                        }
                        return acc / float(inChans);
                    };
                    const float s0 = sampleMono(i0);
                    const float s1 = sampleMono(i1);
                    const float s  = s0 + static_cast<float>(frac) * (s1 - s0);
                    const float scaled = static_cast<float>(s * finalGain);
                    for (int oc = 0; oc < outChans; ++oc) {
                        const float g = v.channelGains[oc];
                        if (g == 0.f) continue;
                        const float written = scaled * g;
                        out[f * outChans + oc] += written;
                        const float a = std::fabs(written);
                        if (oc < 16 && a > bufPeaks[oc]) bufPeaks[oc] = a;
                    }
                    continue;
                }

                if (hasEffects) {
                    // Render this frame's gained + panned stereo pair into the
                    // scratch. The chain and the output-sum happen after the
                    // loop so the chain sees a contiguous interleaved block.
                    auto interpCh = [&](int sc) -> float {
                        auto smp = [&](qint64 idx) -> float {
                            if (idx < 0) idx = 0;
                            if (idx >= totalFrames) idx = totalFrames - 1;
                            return samples[static_cast<size_t>(idx) * static_cast<size_t>(inChans)
                                           + static_cast<size_t>(sc)];
                        };
                        const float a = smp(i0), b = smp(i1);
                        return a + static_cast<float>(frac) * (b - a);
                    };
                    const float sL = interpCh(0);
                    const float sR = interpCh(inChans > 1 ? 1 : 0);
                    v.fxScratch[static_cast<size_t>(f) * 2 + 0] =
                        static_cast<float>(sL * finalGain) * leftPan;
                    v.fxScratch[static_cast<size_t>(f) * 2 + 1] =
                        static_cast<float>(sR * finalGain) * rightPan;
                    continue;
                }

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
                    // Per-output send gain (post-pan). Empty = unity
                    // on every channel; shorter than outChans = unity
                    // for the trailing channels. Lets the operator
                    // route a stereo cue to FOH at 0 dB and lobby
                    // at -12 dB without touching the pan.
                    const float sendGain = (oc < v.outputGains.size())
                                              ? v.outputGains[oc] : 1.f;
                    const float written = static_cast<float>(s * finalGain)
                                            * panGain * sendGain;
                    out[f * outChans + oc] += written;
                    const float a = std::fabs(written);
                    if (oc < 16 && a > bufPeaks[oc]) bufPeaks[oc] = a;
                }
            }

            // Run this voice's chain over the rendered stereo block (a no-op
            // for disabled effects), then sum into the output applying the
            // per-output send gains. process() is allocation- and lock-free.
            if (hasEffects) {
                const int n = static_cast<int>(framesWritten);
                for (auto *fx : v.effects)
                    if (fx && fx->isEnabled())
                        fx->process(v.fxScratch.data(), n);
                for (qint64 f = 0; f < framesWritten; ++f) {
                    const float L = v.fxScratch[static_cast<size_t>(f) * 2 + 0];
                    const float R = v.fxScratch[static_cast<size_t>(f) * 2 + 1];
                    for (int oc = 0; oc < outChans; ++oc) {
                        float chanS;
                        if (outChans == 1)   chanS = 0.5f * (L + R);
                        else if (oc == 0)    chanS = L;
                        else if (oc == 1)    chanS = R;
                        else                 chanS = (oc % 2 == 0) ? L : R;
                        const float sendGain = (oc < v.outputGains.size())
                                                  ? v.outputGains[oc] : 1.f;
                        const float written = chanS * sendGain;
                        out[f * outChans + oc] += written;
                        const float a = std::fabs(written);
                        if (oc < 16 && a > bufPeaks[oc]) bufPeaks[oc] = a;
                    }
                }
            }

            // bufPeaks[0/1] also drive the legacy peakL/R atomics so the
            // existing stereo readers keep working.
            bufPeakL = bufPeaks[0];
            bufPeakR = (outChans > 1) ? bufPeaks[1] : 0.f;
            v.peakL.store(bufPeakL, std::memory_order_relaxed);
            v.peakR.store(bufPeakR, std::memory_order_relaxed);
            // Per-channel peaks for the active-cues panel.
            const int copyN = std::min<int>(outChans, v.peakPerChannel.size());
            for (int oc = 0; oc < copyN; ++oc) v.peakPerChannel[oc] = bufPeaks[oc];

            // Advance by the frames we actually rendered. When the
            // decode-ongoing stall broke the inner loop early,
            // framesWritten < framesWanted and we keep readPos parked
            // so the next buffer resumes exactly where we paused.
            const qint64 advanced = static_cast<qint64>(framesWritten * rate);
            v.readPos += advanced;
            if (v.loop && effectiveEnd > 0) v.readPos %= effectiveEnd;
            else if (v.readPos >= effectiveEnd && !decodeOngoing) {
                v.finished = true;
            }

            // Advance the stop-fade by the frames we actually wrote, not
            // the full buffer — on a decode stall (framesWritten <
            // framesWanted) advancing by framesWanted would run the
            // fade-out faster than real time and clip the tail.
            if (v.stopRequested) v.fadeOutCounter += framesWritten;

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
    , m_deviceWatcher(new QMediaDevices(this))
    , m_defaultDevice(QMediaDevices::defaultAudioOutput())
{
    qRegisterMetaType<quewi::audio::VoiceId>("quewi::audio::VoiceId");
    // React to the system default output changing (headphones in/out,
    // Control-Center output switch on macOS, default-device change on
    // Windows). Without this a mid-show device change left the engine
    // feeding a dead sink with no recovery.
    connect(m_deviceWatcher, &QMediaDevices::audioOutputsChanged,
            this, &AudioEngine::onSystemDefaultOutputChanged);
}

AudioEngine::~AudioEngine() = default;

void AudioEngine::setDefaultOutputDevice(const QAudioDevice &device)
{
    if (device.id() == m_defaultDevice.id()) return;
    m_defaultDevice = device;
    // The user explicitly chose a device, so stop auto-following the
    // system default — otherwise a later system change would yank
    // their chosen output away.
    m_followSystemDefault = false;
    // If the previous default has an open context, leave it for any voices
    // still running. Future fire()s with empty deviceId will pick up the
    // new default lazily.
}

void AudioEngine::onSystemDefaultOutputChanged()
{
    if (!m_followSystemDefault) return;
    const QAudioDevice sysDefault = QMediaDevices::defaultAudioOutput();
    if (sysDefault.isNull() || sysDefault.id() == m_defaultDevice.id()) {
        // The list changed but our default is still valid (e.g. a
        // non-default device was added/removed). Still prune any
        // contexts whose sink died as a side effect.
        pruneDeadContexts();
        return;
    }
    const QByteArray oldId = m_defaultDevice.id();
    m_defaultDevice = sysDefault;
    // Tear down the context bound to the OLD default; its CoreAudio sink
    // is (or is about to be) dead. Voices that were playing on it are
    // lost — Qt gives us no way to seamlessly migrate a live sink across
    // devices — but the next GO (and any auto-followed cue) rebuilds on
    // the new default instead of silently failing forever.
    m_contexts.erase(
        std::remove_if(m_contexts.begin(), m_contexts.end(),
            [&](const std::unique_ptr<DeviceContext> &c) {
                return c->device.id() == oldId;
            }),
        m_contexts.end());
    pruneDeadContexts();
    if (m_contexts.empty() && m_running.load()) {
        m_running.store(false);
        emit runningChanged(false);
    }
}

void AudioEngine::pruneDeadContexts()
{
    m_contexts.erase(
        std::remove_if(m_contexts.begin(), m_contexts.end(),
            [](const std::unique_ptr<DeviceContext> &c) {
                return !c->sink || c->sink->state() == QAudio::StoppedState;
            }),
        m_contexts.end());
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
    if (auto *existing = contextForDeviceId(key)) {
        // Reuse the context only if its sink is still alive. macOS
        // CoreAudio stops the sink on device format/rate changes, on
        // disconnect, and on transient glitches; a stopped sink in the
        // cache would otherwise be handed back forever and every
        // subsequent GO would play silence. If it died, drop it and
        // fall through to rebuild a fresh one on the (possibly changed)
        // device.
        const bool dead = !existing->sink
            || existing->sink->state() == QAudio::StoppedState;
        if (!dead) return existing;
        m_contexts.erase(
            std::remove_if(m_contexts.begin(), m_contexts.end(),
                [&](const std::unique_ptr<DeviceContext> &c) {
                    return c->device.id() == key;
                }),
            m_contexts.end());
    }

    // Channel-count negotiation. Object audio (Phase 6 / v0.3) needs more
    // than two channels; ask the device for what it can do, then pick a
    // sensible upper bound (16) so a 64-out aggregate device doesn't get
    // us a 64-channel mixer for a stereo-only show. The user's Speaker
    // Patch tells the renderer which channels to actually drive.
    const QAudioFormat preferred = device.preferredFormat();
    const int devMaxChans  = qBound(2, preferred.channelCount(), 16);

    QAudioFormat fmt;
    fmt.setSampleFormat(QAudioFormat::Float);
    fmt.setChannelCount(devMaxChans);

    // Prefer the device's OWN nominal sample rate. macOS hardware is
    // very commonly configured at 44100 Hz (MacBook speakers, most USB
    // interfaces); hardcoding 48000 made CoreAudio either resample
    // internally or — worse — run the sink against the 44.1 kHz hardware
    // clock while the mixer believed it was feeding 48 kHz, which plays
    // music at the wrong speed and pitch. Try the device's preferred
    // rate first, then the common studio rates as fallbacks.
    const int preferredRate = preferred.sampleRate() > 0
                              ? preferred.sampleRate() : 48000;
    bool rateOk = false;
    for (int rate : { preferredRate, 48000, 44100 }) {
        fmt.setSampleRate(rate);
        if (device.isFormatSupported(fmt)) { rateOk = true; break; }
    }
    if (!rateOk) {
        // Wholesale fall back to the device's preferred format — covers
        // devices that only advertise specific (rate, channels) combos.
        fmt = preferred;
        if (fmt.sampleFormat() != QAudioFormat::Float) {
            fmt.setSampleFormat(QAudioFormat::Float);
        }
        if (fmt.channelCount() < 2) fmt.setChannelCount(2);
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
    // Target ~300 ms of buffering computed from the NEGOTIATED rate and
    // channel count, not a fixed byte count. A fixed 128 KB was ~340 ms
    // at 48k stereo but only ~85 ms on an 8-channel device — far too
    // tight, raising underrun risk exactly where the channel count is
    // highest. Generous buffering keeps a GUI-thread context switch
    // (opening a window, a Control-Center repaint on macOS) from
    // starving the audio callback.
    {
        const int bytesPerFrame = fmt.channelCount() * int(sizeof(float));
        const int targetFrames   = fmt.sampleRate() * 3 / 10;   // 300 ms
        ctx->sink->setBufferSize(std::max(131072, targetFrames * bytesPerFrame));
    }

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

    // Re-read the format the sink ACTUALLY negotiated. Qt's CoreAudio
    // backend can substitute a different sample rate than we asked for;
    // if the mixer keeps resampling toward the requested rate while the
    // hardware runs at another, music plays at the wrong speed/pitch.
    // Reconfiguring only the sample rate is safe here — it changes the
    // resampler ratio but not the interleave width — and no voices have
    // been added yet. A channel-count substitution would be a deeper
    // mismatch (the sink was built with fmt's channel count); warn but
    // don't try to live-rewire it.
    {
        const QAudioFormat actual = ctx->sink->format();
        if (actual.sampleRate() > 0
            && actual.sampleRate() != ctx->sampleRate) {
            qWarning("AudioEngine: sink negotiated %d Hz, requested %d Hz — "
                     "reconfiguring mixer to match.",
                     actual.sampleRate(), ctx->sampleRate);
            ctx->sampleRate = actual.sampleRate();
            ctx->mixer->configure(actual.sampleRate(), ctx->channels);
        }
        if (actual.channelCount() > 0
            && actual.channelCount() != ctx->channels) {
            qWarning("AudioEngine: sink negotiated %d channels, requested %d "
                     "— output may be misaligned.",
                     actual.channelCount(), ctx->channels);
        }
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
    // Allow firing on a partially-decoded file as long as it has
    // published at least one snapshot. The mixer will refresh the
    // voice's snapshot as more decode arrives, so a cue fired during
    // decode plays the head immediately and continues seamlessly.
    // Failed/empty files still bail.
    if (!file) return 0;
    if (file->state() == AudioFile::State::Failed
        || file->state() == AudioFile::State::Empty) return 0;
    if (!file->snapshot()) return 0;

    const QAudioDevice dev = resolveDevice(params.outputDeviceId);
    auto *ctx = ensureContextForDevice(dev);
    if (!ctx) return 0;

    // Soft voice cap. Stops a runaway "spam GO 500 times on one cue"
    // from piling unbounded voices in the mixer. When at the cap, the
    // oldest still-running voice gets a 100 ms fade-stop to make room
    // for the new one. 64 is generous for any real cueing show — pro
    // tools tend to cap around 32-64.
    constexpr int kVoiceCap = 64;
    if (ctx->mixer && ctx->mixer->activeCount() >= kVoiceCap) {
        ctx->mixer->stopOldestUnstopped(0.10);
    }

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

void AudioEngine::setVoiceLoop(VoiceId id, bool loop)
{
    for (auto &ctx : m_contexts) {
        if (ctx->mixer && ctx->mixer->setVoiceLoop(id, loop)) return;
    }
}

void AudioEngine::setVoiceChannelGains(VoiceId id, const QList<float> &gains)
{
    for (auto &ctx : m_contexts) {
        if (ctx->mixer && ctx->mixer->setVoiceChannelGains(id, gains)) return;
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

int AudioEngine::outputChannelCount(const QByteArray &outputDeviceId)
{
    const QAudioDevice dev = resolveDevice(outputDeviceId);
    auto *ctx = ensureContextForDevice(dev);
    return ctx ? ctx->channels : 0;
}

bool AudioEngine::pause(VoiceId id)
{
    for (auto &ctx : m_contexts) {
        if (ctx->mixer && ctx->mixer->pauseVoice(id)) return true;
    }
    return false;
}

bool AudioEngine::resume(VoiceId id)
{
    for (auto &ctx : m_contexts) {
        if (ctx->mixer && ctx->mixer->resumeVoice(id)) return true;
    }
    return false;
}

bool AudioEngine::isPaused(VoiceId id) const
{
    for (const auto &ctx : m_contexts) {
        if (ctx->mixer && ctx->mixer->isPausedVoice(id)) return true;
    }
    return false;
}

bool AudioEngine::seek(VoiceId id, double seconds)
{
    for (auto &ctx : m_contexts) {
        if (ctx->mixer && ctx->mixer->seekVoice(id, seconds)) return true;
    }
    return false;
}

void AudioEngine::onMixerVoiceFinished(VoiceId id)
{
    // Destroy the finished voice's effects chain on THIS (GUI) thread — the
    // mixer erased the voice on the audio callback but deliberately left the
    // owning shared_ptrs in m_voiceFx so the AudioEffect (QObject) destructors
    // never run on the RT thread. VoiceIds are global, so only one mixer owns it.
    for (auto &ctx : m_contexts)
        if (ctx->mixer) ctx->mixer->reapEffects(id);
    emit voiceFinished(id);
}

} // namespace quewi::audio
