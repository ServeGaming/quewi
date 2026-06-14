#pragma once

#include <QIODevice>
#include <vector>

namespace quewi::audio { class AudioEditorTrack; }

namespace quewi::ui {

// A QIODevice that streams a dry stereo mix to a QAudioSink while applying
// one track's effect chain in real time. Because the effects read their
// parameters every block, tweaking the EQ / compressor (or toggling an
// effect) is heard immediately — no re-render. The dry buffer is referenced,
// not copied, so the owner must keep it alive for the playback's duration.
//
// Output is 16-bit interleaved stereo (what the editor's QAudioSink expects).
class LiveEffectDevice : public QIODevice {
    Q_OBJECT
public:
    explicit LiveEffectDevice(QObject *parent = nullptr);

    // dryStereo: interleaved stereo float mix WITH NO effects (referenced).
    // track:     whose enabled effects are applied live (may be null = dry).
    // startFrame: where playback begins.
    // Prepares + resets the track's effects at sampleRate. Opens the device.
    void start(const std::vector<float> *dryStereo,
               audio::AudioEditorTrack *track,
               int sampleRate, qint64 startFrame);

    qint64 currentFrame() const { return m_pos; }

protected:
    qint64 readData(char *data, qint64 maxlen) override;
    qint64 writeData(const char *, qint64) override { return -1; }
    bool   isSequential() const override { return true; }
    qint64 bytesAvailable() const override;

private:
    const std::vector<float> *m_dry = nullptr; // interleaved stereo, not owned
    qint64 m_pos   = 0;   // current frame
    qint64 m_total = 0;   // total frames in m_dry
    int    m_sampleRate = 48000;
    audio::AudioEditorTrack *m_track = nullptr;
    std::vector<float> m_scratch;
};

} // namespace quewi::ui
