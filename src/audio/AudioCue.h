#pragma once

#include "audio/AudioFile.h"
#include "cues/Cue.h"

#include <memory>

namespace quewi::audio {

class AudioEngine;

// A cue that plays an audio file through the AudioEngine.
//
// Phase 5.5 covers:
//   - file path
//   - gain (dB)
//   - fade in / fade out (seconds)
//   - trim in / trim out (seconds; 0 = play to end)
//   - pan (-1.0 = full L … +1.0 = full R)
//   - loop
//
// Slices, per-output matrix levels, and effects-chain plugins live in
// follow-up commits and the planned full audio editor (Phase 9).
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

    void prepare();

    QString filePath()       const { return m_filePath; }
    double  gainDb()         const { return m_gainDb; }
    double  fadeInSeconds()  const { return m_fadeInSeconds; }
    double  fadeOutSeconds() const { return m_fadeOutSeconds; }
    double  trimInSeconds()  const { return m_trimInSeconds; }
    double  trimOutSeconds() const { return m_trimOutSeconds; }
    double  pan()            const { return m_pan; }
    bool    loop()           const { return m_loop; }
    QByteArray outputDeviceId() const { return m_outputDeviceId; }

    std::shared_ptr<AudioFile> audioFile() const { return m_file; }

    quint64 currentVoiceId() const { return m_currentVoiceId; }
    void    setCurrentVoiceId(quint64 id) { m_currentVoiceId = id; }

private:
    QString m_filePath;
    double  m_gainDb         = 0.0;
    double  m_fadeInSeconds  = 0.0;
    double  m_fadeOutSeconds = 0.0;
    double  m_trimInSeconds  = 0.0;
    double  m_trimOutSeconds = 0.0;
    double  m_pan            = 0.0;
    bool    m_loop           = false;
    QByteArray m_outputDeviceId;   // empty = use AudioEngine default

    std::shared_ptr<AudioFile> m_file;
    quint64                    m_currentVoiceId = 0;
};

} // namespace quewi::audio
