#pragma once

#include "cues/Cue.h"
#include "video/PictureTiming.h"

#include <QColor>

namespace quewi::audio { class AudioCue; }

namespace quewi::video {

// Common base for the three visual cue types. Holds the geometry,
// screen, and opacity properties so the inspector can edit them
// uniformly without duplicating every accessor on each subclass.
class VisualCue : public cues::Cue {
    Q_OBJECT
public:
    explicit VisualCue(QObject *parent = nullptr) : cues::Cue(parent) {}

    int    screenIndex() const { return m_screenIndex; }
    double posX()        const { return m_posX; }
    double posY()        const { return m_posY; }
    double posW()        const { return m_posW; }
    double posH()        const { return m_posH; }
    double opacity()     const { return m_opacity; }

    QVariant field(const QString &key) const override;
    void     setField(const QString &key, const QVariant &value) override;

    // Live playing-voice handle, mirroring AudioCue::currentVoiceId. Set
    // at fire time so the Inspector can resolve this cue to its live
    // VideoLayer (scrubber/seek/pause); cleared on VideoEngine::voiceFinished.
    // Transient runtime state — NOT serialized.
    quint64 currentVoiceId() const { return m_currentVoiceId; }
    void    setCurrentVoiceId(quint64 id) { m_currentVoiceId = id; }

protected:
    QJsonObject visualToPayload() const;
    void        visualFromPayload(const QJsonObject &payload);

    quint64 m_currentVoiceId = 0;
    int    m_screenIndex = 0;
    double m_posX = 0.0;
    double m_posY = 0.0;
    double m_posW = 1.0;
    double m_posH = 1.0;
    double m_opacity = 1.0;
};

class VideoCue : public VisualCue {
    Q_OBJECT
public:
    explicit VideoCue(QObject *parent = nullptr);
    ~VideoCue() override;

    QString typeKey()  const override { return QStringLiteral("video"); }
    QString typeName() const override { return tr("Video"); }

    QString filePath() const { return m_filePath; }
    bool    loop()     const { return m_loop; }

    // ── The video's soundtrack ─────────────────────────────────────────
    // Played through the AudioEngine (the video player itself stays muted)
    // with full audio-cue settings: level, fades, pan, output, effects and
    // the audio editor. Held as an embedded AudioCue that never sits in a
    // list — its file follows the video's, its loop follows the video's.
    // Editable through setField as "sound.<audio field>" (undoable like any
    // field) and "soundEnabled".
    audio::AudioCue *sound() const { return m_sound; }

    // ── Trims and picture fades (the video editor) ─────────────────────
    // In / Out are the soundtrack's trims — one value for picture and sound,
    // so they can't drift apart. Fields "trimInSeconds" / "trimOutSeconds"
    // (Out 0 = end of file) set the sound's; "pictureFadeInSeconds" /
    // "pictureFadeOutSeconds" ramp the picture's opacity after In / before
    // Out. The sound keeps its own fades ("sound.fadeInSeconds" …).
    double trimInSeconds()  const;
    double trimOutSeconds() const;
    double pictureFadeInSeconds()  const { return m_pictureFadeIn; }
    double pictureFadeOutSeconds() const { return m_pictureFadeOut; }
    PictureTiming pictureTiming() const;
    // Sections cut out (the soundtrack's, field "cuts") — picture and sound
    // jump over them together.
    const audio::Cuts &cuts() const;
    bool soundEnabled() const { return m_soundEnabled; }
    // The audio cue a voice-carrying cue plays through: an AudioCue itself,
    // or a VideoCue's sound (when it's on). nullptr otherwise.
    static audio::AudioCue *audioOf(cues::Cue *cue);

    QVariant field(const QString &key) const override;
    void     setField(const QString &key, const QVariant &value) override;

    QJsonObject toPayload() const override;
    void        fromPayload(const QJsonObject &payload) override;

private:
    QString m_filePath;
    bool    m_loop = false;
    audio::AudioCue *m_sound = nullptr;   // child; never null
    // New video cues play their sound. A show saved before video sound
    // existed loads with it OFF, so it plays exactly as it always did.
    bool    m_soundEnabled = true;
    double  m_pictureFadeIn  = 0.0;
    double  m_pictureFadeOut = 0.0;
};

class ImageCue : public VisualCue {
    Q_OBJECT
public:
    explicit ImageCue(QObject *parent = nullptr);
    ~ImageCue() override;

    QString typeKey()  const override { return QStringLiteral("image"); }
    QString typeName() const override { return tr("Image"); }

    QString filePath() const { return m_filePath; }

    QVariant field(const QString &key) const override;
    void     setField(const QString &key, const QVariant &value) override;

    QJsonObject toPayload() const override;
    void        fromPayload(const QJsonObject &payload) override;

private:
    QString m_filePath;
};

class TextCue : public VisualCue {
    Q_OBJECT
public:
    explicit TextCue(QObject *parent = nullptr);
    ~TextCue() override;

    QString typeKey()  const override { return QStringLiteral("text"); }
    QString typeName() const override { return tr("Text"); }

    QString text()         const { return m_text; }
    int     fontPixelSize() const { return m_fontPixelSize; }
    QColor  textColor()    const { return m_textColor; }

    QVariant field(const QString &key) const override;
    void     setField(const QString &key, const QVariant &value) override;

    QJsonObject toPayload() const override;
    void        fromPayload(const QJsonObject &payload) override;

private:
    QString m_text;
    int     m_fontPixelSize = 96;
    QColor  m_textColor = Qt::white;
};

} // namespace quewi::video
