#pragma once

#include <QObject>
#include <QString>
#include <atomic>
#include <memory>
#include <vector>

class QAudioDecoder;

namespace quewi::audio {

// Immutable snapshot of a decoded PCM buffer + its metadata. Published
// atomically via AudioFile::snapshot() so the audio mixer can hold a
// shared_ptr<const Snapshot> for the lifetime of a voice without any
// risk of the GUI thread pulling the samples out from under the
// real-time callback (e.g. clear / reverseSamples / re-load).
struct AudioBufferSnapshot {
    // Shared, immutable backing buffer. Holding a shared_ptr keeps the
    // bytes alive for the voice's lifetime even if AudioFile cow-swaps
    // its m_samples to a bigger backing on capacity overflow. Lets
    // publishSnapshot() copy a pointer instead of megabytes of audio,
    // which was the cause of the heap churn that looked like a leak
    // when the GO button was spammed mid-decode.
    std::shared_ptr<const std::vector<float>> samples;
    int    channelCount = 0;
    int    sampleRate   = 0;
    qint64 frameCount   = 0;
};

// Decoded PCM held in float32, interleaved by channel. Loading is
// asynchronous: kick it off with `load(path)`, listen for `stateChanged`
// or query `state()`. The decoded buffer and the peak overview are
// safe to read once state == Loaded.
//
// Peak overview: one peak (max-abs amplitude) per `kPeakBlock` source
// samples per channel. Used by the waveform widget — cheap to render
// even for hour-long files.
class AudioFile : public QObject {
    Q_OBJECT
public:
    static constexpr int kPeakBlock = 256;

    enum class State {
        Empty,
        Loading,
        Loaded,
        Failed,
    };
    Q_ENUM(State)

    explicit AudioFile(QObject *parent = nullptr);
    ~AudioFile() override;

    void load(const QString &path);
    void clear();

    State    state()       const { return m_state; }
    QString  path()        const { return m_path; }
    QString  errorString() const { return m_error; }

    // Valid once state == Loaded:
    int   sampleRate()   const { return m_sampleRate; }
    int   channelCount() const { return m_channelCount; }
    qint64 frameCount() const  { return m_frameCount; } // frames = samples per channel
    double durationSeconds() const;

    // Interleaved float32 buffer. Size == frameCount * channelCount.
    const std::vector<float> &samples() const { return *m_samples; }

    // Resident memory cost — what this file currently holds in RAM,
    // including the published snapshot (which holds a copy until the
    // mixer drops it). Used by the global memory-budget tracker.
    qint64 bytesUsed() const;

    // Peaks: one float per peak block per channel, interleaved.
    // Size == ceil(frameCount / kPeakBlock) * channelCount.
    const std::vector<float> &peaks() const { return m_peaks; }

    // Real-time-safe snapshot of the decoded buffer + metadata. Returns
    // the most recently published immutable snapshot, or null if no
    // buffer has been published yet (state != Loaded). The audio mixer
    // calls this once at fire() time and holds the shared_ptr for the
    // voice's lifetime, so concurrent edits or re-loads on the GUI
    // thread can't invalidate the in-flight playback.
    std::shared_ptr<const AudioBufferSnapshot> snapshot() const;

    // In-memory edits — non-destructive against the original file (a
    // re-load via load(path) restores the source). Phase 9's full audio
    // editor adds undo, multi-track, and effects on top of these.
    void reverseSamples();
    void normaliseSamples(float targetPeak = 0.891f); // -1 dBFS

signals:
    void stateChanged(State s);

private slots:
    void onBufferReady();
    void onFinished();
    void onError();

private:
    void setState(State s);
    void buildPeaksIncrementally(qint64 newFramesEnd);
    void publishSnapshot();
    void clearSnapshot();

    // Atomic shared_ptr — readers (audio thread) load lock-free, writers
    // (GUI thread, only in onFinished / reverseSamples / normaliseSamples
    // / clear) atomically swap a freshly-built immutable buffer in.
    std::atomic<std::shared_ptr<const AudioBufferSnapshot>> m_published;

    State                          m_state = State::Empty;
    QString                        m_path;
    QString                        m_error;

    std::unique_ptr<QAudioDecoder> m_decoder;
    int                            m_sampleRate = 0;
    int                            m_channelCount = 0;
    qint64                         m_frameCount = 0;

    // Shared, mutable-only-on-GUI-thread backing. The audio thread reads
    // through immutable snapshots that share this pointer, so capacity
    // overflows during decode COW into a fresh backing rather than
    // realloc'ing under live readers.
    std::shared_ptr<std::vector<float>> m_samples;
    std::vector<float>             m_peaks;
    qint64                         m_peakFramesProcessed = 0;
    // Progressive publication cursor. publishSnapshot() runs every
    // ~2 seconds of decoded audio so cues fired mid-decode have a
    // valid (partial) snapshot to play.
    qint64                         m_lastPublishedFrames = 0;
};

} // namespace quewi::audio
