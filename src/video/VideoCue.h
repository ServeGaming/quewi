#pragma once

#include "cues/Cue.h"

#include <QColor>

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

    QVariant field(const QString &key) const override;
    void     setField(const QString &key, const QVariant &value) override;

    QJsonObject toPayload() const override;
    void        fromPayload(const QJsonObject &payload) override;

private:
    QString m_filePath;
    bool    m_loop = false;
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
