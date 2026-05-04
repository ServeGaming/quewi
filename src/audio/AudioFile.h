#pragma once

#include <QObject>
#include <QString>
#include <atomic>
#include <memory>
#include <vector>

class QAudioDecoder;

namespace quewi::audio {

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
    const std::vector<float> &samples() const { return m_samples; }

    // Peaks: one float per peak block per channel, interleaved.
    // Size == ceil(frameCount / kPeakBlock) * channelCount.
    const std::vector<float> &peaks() const { return m_peaks; }

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

    State                          m_state = State::Empty;
    QString                        m_path;
    QString                        m_error;

    std::unique_ptr<QAudioDecoder> m_decoder;
    int                            m_sampleRate = 0;
    int                            m_channelCount = 0;
    qint64                         m_frameCount = 0;

    std::vector<float>             m_samples;
    std::vector<float>             m_peaks;
    qint64                         m_peakFramesProcessed = 0;
};

} // namespace quewi::audio
