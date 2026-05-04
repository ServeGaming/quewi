#pragma once

#include "audio/AudioEditorModel.h"
#include <QObject>
#include <QString>
#include <vector>

namespace quewi::audio {

// Renders an AudioEditorModel to a flat stereo float PCM buffer.
// Used for:
//   1. Preview playback — render() → feed to QAudioSink
//   2. Bounce to file  — renderToWav(path) → write interleaved PCM to WAV
//
// All rendering is synchronous (called on the UI thread or a worker thread;
// use QFuture/QtConcurrent at the call site if needed).
class AudioEditorRenderer : public QObject {
    Q_OBJECT
public:
    explicit AudioEditorRenderer(AudioEditorModel *model, QObject *parent = nullptr);
    ~AudioEditorRenderer() override = default;

    // Render the full mix into an interleaved stereo float buffer.
    // Returns false and sets errorString() on failure.
    bool render(std::vector<float> &outStereo);

    // Render and write to a 24-bit PCM WAV file.
    bool renderToWav(const QString &path);

    QString errorString() const { return m_error; }

signals:
    void progress(int percent);

private:
    AudioEditorModel *m_model;
    QString           m_error;

    static float applyFade(float sample, qint64 frameInRegion,
                           qint64 regionDuration,
                           const FadeCurve &fadeIn,
                           const FadeCurve &fadeOut);

    bool writeWav(const QString &path, const std::vector<float> &stereo, int sampleRate);
};

} // namespace quewi::audio
