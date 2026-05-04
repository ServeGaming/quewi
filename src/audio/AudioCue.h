#pragma once

#include "audio/AudioFile.h"
#include "cues/Cue.h"

#include <memory>

namespace quewi::audio {

class AudioEngine;

// A cue that plays an audio file through the AudioEngine. Phase 3
// covers the essentials — file, gain, fade in/out, loop. Trim points,
// slices, and per-output matrix levels land in a follow-up alongside
// the waveform editor.
class AudioCue : public cues::Cue {
    Q_OBJECT
public:
    explicit AudioCue(QObject *parent = nullptr);
    ~AudioCue() override;

    QString typeKey()  const override { return QStringLiteral("audio"); }
    QString typeName() const override { return tr("Audio"); }

    QVariant field(const QString &key) const override;
    void     setField(const QString &key, const QVariant &value) override;

    QJsonObject toPayload() const override;
    void        fromPayload(const QJsonObject &payload) override;

    // Begin loading the file in the background. Calls into the shared
    // AudioFile if the path matches an existing one; otherwise creates
    // a new AudioFile owned by the cue. Idempotent.
    void prepare();

    // Convenience accessors so other code (Inspector, FadeCue) can read
    // state without going through the field bridge.
    QString filePath()       const { return m_filePath; }
    double  gainDb()         const { return m_gainDb; }
    double  fadeInSeconds()  const { return m_fadeInSeconds; }
    double  fadeOutSeconds() const { return m_fadeOutSeconds; }
    bool    loop()           const { return m_loop; }

    std::shared_ptr<AudioFile> audioFile() const { return m_file; }

    // The voice id last returned by AudioEngine::fire(). 0 if not
    // currently playing.
    quint64 currentVoiceId() const { return m_currentVoiceId; }
    void    setCurrentVoiceId(quint64 id) { m_currentVoiceId = id; }

private:
    QString m_filePath;
    double  m_gainDb         = 0.0;
    double  m_fadeInSeconds  = 0.0;
    double  m_fadeOutSeconds = 0.0;
    bool    m_loop           = false;

    std::shared_ptr<AudioFile> m_file;
    quint64                    m_currentVoiceId = 0;
};

} // namespace quewi::audio
