#include "video/VideoCue.h"

#include "audio/AudioCue.h"

#include <QJsonObject>
#include <QSignalBlocker>
#include <algorithm>

namespace quewi::video {

// ---------------- VisualCue ----------------

QVariant VisualCue::field(const QString &key) const
{
    if (key == QLatin1String("screenIndex")) return m_screenIndex;
    if (key == QLatin1String("posX"))        return m_posX;
    if (key == QLatin1String("posY"))        return m_posY;
    if (key == QLatin1String("posW"))        return m_posW;
    if (key == QLatin1String("posH"))        return m_posH;
    if (key == QLatin1String("opacity"))     return m_opacity;
    return cues::Cue::field(key);
}

void VisualCue::setField(const QString &key, const QVariant &value)
{
    auto setDouble = [&](double &target) {
        if (qFuzzyCompare(target, value.toDouble())) return false;
        target = value.toDouble();
        emitChanged();
        return true;
    };
    if (key == QLatin1String("screenIndex")) {
        if (m_screenIndex == value.toInt()) return;
        m_screenIndex = value.toInt();
        emitChanged();
        return;
    }
    if (key == QLatin1String("posX"))    { setDouble(m_posX);    return; }
    if (key == QLatin1String("posY"))    { setDouble(m_posY);    return; }
    if (key == QLatin1String("posW"))    { setDouble(m_posW);    return; }
    if (key == QLatin1String("posH"))    { setDouble(m_posH);    return; }
    if (key == QLatin1String("opacity")) { setDouble(m_opacity); return; }
    cues::Cue::setField(key, value);
}

QJsonObject VisualCue::visualToPayload() const
{
    auto o = cues::Cue::toPayload();
    o.insert(QStringLiteral("screenIndex"), m_screenIndex);
    o.insert(QStringLiteral("posX"), m_posX);
    o.insert(QStringLiteral("posY"), m_posY);
    o.insert(QStringLiteral("posW"), m_posW);
    o.insert(QStringLiteral("posH"), m_posH);
    o.insert(QStringLiteral("opacity"), m_opacity);
    return o;
}

void VisualCue::visualFromPayload(const QJsonObject &payload)
{
    cues::Cue::fromPayload(payload);
    m_screenIndex = payload.value(QStringLiteral("screenIndex")).toInt(m_screenIndex);
    m_posX        = payload.value(QStringLiteral("posX")).toDouble(m_posX);
    m_posY        = payload.value(QStringLiteral("posY")).toDouble(m_posY);
    m_posW        = payload.value(QStringLiteral("posW")).toDouble(m_posW);
    m_posH        = payload.value(QStringLiteral("posH")).toDouble(m_posH);
    m_opacity     = payload.value(QStringLiteral("opacity")).toDouble(m_opacity);
}

// ---------------- VideoCue ----------------

VideoCue::VideoCue(QObject *parent)
    : VisualCue(parent)
    , m_sound(new audio::AudioCue(this))
{
    // Any change to the sound (level, effects, the editor) is a change to
    // this cue: it marks the show unsaved and refreshes the row.
    connect(m_sound, &cues::Cue::changed, this, [this] { emitChanged(); });
}
VideoCue::~VideoCue() = default;

audio::AudioCue *VideoCue::audioOf(cues::Cue *cue)
{
    if (auto *ac = qobject_cast<audio::AudioCue *>(cue)) return ac;
    if (auto *vc = qobject_cast<VideoCue *>(cue); vc && vc->soundEnabled()) return vc->sound();
    return nullptr;
}

double VideoCue::trimInSeconds()  const { return m_sound->trimInSeconds(); }
double VideoCue::trimOutSeconds() const { return m_sound->trimOutSeconds(); }

PictureTiming VideoCue::pictureTiming() const
{
    PictureTiming t;
    t.inSeconds      = trimInSeconds();
    t.outSeconds     = trimOutSeconds();
    t.fadeInSeconds  = m_pictureFadeIn;
    t.fadeOutSeconds = m_pictureFadeOut;
    t.loop           = m_loop;
    return t;
}

QVariant VideoCue::field(const QString &key) const
{
    if (key == QLatin1String("trimInSeconds") || key == QLatin1String("trimOutSeconds"))
        return m_sound->field(key);
    if (key == QLatin1String("pictureFadeInSeconds"))  return m_pictureFadeIn;
    if (key == QLatin1String("pictureFadeOutSeconds")) return m_pictureFadeOut;
    if (key == QLatin1String("filePath"))     return m_filePath;
    if (key == QLatin1String("loop"))         return m_loop;
    if (key == QLatin1String("soundEnabled")) return m_soundEnabled;
    if (key.startsWith(QLatin1String("sound."))) return m_sound->field(key.mid(6));
    return VisualCue::field(key);
}

void VideoCue::setField(const QString &key, const QVariant &value)
{
    if (key == QLatin1String("filePath")) {
        if (m_filePath == value.toString()) return;
        m_filePath = value.toString();
        // The soundtrack is the video's own audio, so it follows the file.
        m_sound->setField(QStringLiteral("filePath"), m_filePath);
        emitChanged();
        return;
    }
    if (key == QLatin1String("loop")) {
        if (m_loop == value.toBool()) return;
        m_loop = value.toBool();
        m_sound->setField(QStringLiteral("loop"), m_loop);
        emitChanged();
        return;
    }
    // The picture's In / Out are the sound's trims (its changed() re-emits ours).
    if (key == QLatin1String("trimInSeconds") || key == QLatin1String("trimOutSeconds")) {
        m_sound->setField(key, std::max(0.0, value.toDouble()));
        return;
    }
    if (key == QLatin1String("pictureFadeInSeconds")
        || key == QLatin1String("pictureFadeOutSeconds")) {
        double &target = key == QLatin1String("pictureFadeInSeconds") ? m_pictureFadeIn
                                                                       : m_pictureFadeOut;
        const double v = std::max(0.0, value.toDouble());
        if (qFuzzyCompare(target + 1.0, v + 1.0)) return;
        target = v;
        emitChanged();
        return;
    }
    if (key == QLatin1String("soundEnabled")) {
        if (m_soundEnabled == value.toBool()) return;
        m_soundEnabled = value.toBool();
        emitChanged();
        return;
    }
    if (key.startsWith(QLatin1String("sound."))) {
        m_sound->setField(key.mid(6), value);   // its changed() re-emits ours
        return;
    }
    VisualCue::setField(key, value);
}

QJsonObject VideoCue::toPayload() const
{
    auto o = visualToPayload();
    o.insert(QStringLiteral("filePath"), m_filePath);
    o.insert(QStringLiteral("loop"), m_loop);
    o.insert(QStringLiteral("soundEnabled"), m_soundEnabled);
    o.insert(QStringLiteral("pictureFadeInSeconds"),  m_pictureFadeIn);
    o.insert(QStringLiteral("pictureFadeOutSeconds"), m_pictureFadeOut);
    o.insert(QStringLiteral("sound"), m_sound->toPayload());
    return o;
}

void VideoCue::fromPayload(const QJsonObject &payload)
{
    visualFromPayload(payload);
    m_filePath = payload.value(QStringLiteral("filePath")).toString();
    m_loop     = payload.value(QStringLiteral("loop")).toBool();
    // Missing = a show from before video sound existed: keep it silent.
    m_soundEnabled = payload.value(QStringLiteral("soundEnabled")).toBool(false);
    m_pictureFadeIn  = payload.value(QStringLiteral("pictureFadeInSeconds")).toDouble();
    m_pictureFadeOut = payload.value(QStringLiteral("pictureFadeOutSeconds")).toDouble();
    const QSignalBlocker block(m_sound);
    if (payload.contains(QStringLiteral("sound")))
        m_sound->fromPayload(payload.value(QStringLiteral("sound")).toObject());
    if (m_sound->filePath().isEmpty())
        m_sound->setField(QStringLiteral("filePath"), m_filePath);
    m_sound->setField(QStringLiteral("loop"), m_loop);
}

// ---------------- ImageCue ----------------

ImageCue::ImageCue(QObject *parent) : VisualCue(parent) {}
ImageCue::~ImageCue() = default;

QVariant ImageCue::field(const QString &key) const
{
    if (key == QLatin1String("filePath")) return m_filePath;
    return VisualCue::field(key);
}

void ImageCue::setField(const QString &key, const QVariant &value)
{
    if (key == QLatin1String("filePath")) {
        if (m_filePath == value.toString()) return;
        m_filePath = value.toString();
        emitChanged();
        return;
    }
    VisualCue::setField(key, value);
}

QJsonObject ImageCue::toPayload() const
{
    auto o = visualToPayload();
    o.insert(QStringLiteral("filePath"), m_filePath);
    return o;
}

void ImageCue::fromPayload(const QJsonObject &payload)
{
    visualFromPayload(payload);
    m_filePath = payload.value(QStringLiteral("filePath")).toString();
}

// ---------------- TextCue ----------------

TextCue::TextCue(QObject *parent) : VisualCue(parent) {}
TextCue::~TextCue() = default;

QVariant TextCue::field(const QString &key) const
{
    if (key == QLatin1String("text"))         return m_text;
    if (key == QLatin1String("fontPixelSize")) return m_fontPixelSize;
    if (key == QLatin1String("textColor"))    return m_textColor;
    return VisualCue::field(key);
}

void TextCue::setField(const QString &key, const QVariant &value)
{
    if (key == QLatin1String("text")) {
        if (m_text == value.toString()) return;
        m_text = value.toString();
        emitChanged();
        return;
    }
    if (key == QLatin1String("fontPixelSize")) {
        if (m_fontPixelSize == value.toInt()) return;
        m_fontPixelSize = value.toInt();
        emitChanged();
        return;
    }
    if (key == QLatin1String("textColor")) {
        const auto c = value.value<QColor>();
        if (m_textColor == c) return;
        m_textColor = c;
        emitChanged();
        return;
    }
    VisualCue::setField(key, value);
}

QJsonObject TextCue::toPayload() const
{
    auto o = visualToPayload();
    o.insert(QStringLiteral("text"), m_text);
    o.insert(QStringLiteral("fontPixelSize"), m_fontPixelSize);
    o.insert(QStringLiteral("textColor"), m_textColor.name(QColor::HexArgb));
    return o;
}

void TextCue::fromPayload(const QJsonObject &payload)
{
    visualFromPayload(payload);
    m_text          = payload.value(QStringLiteral("text")).toString();
    m_fontPixelSize = payload.value(QStringLiteral("fontPixelSize")).toInt(m_fontPixelSize);
    const auto colName = payload.value(QStringLiteral("textColor")).toString();
    if (!colName.isEmpty()) m_textColor = QColor(colName);
}

} // namespace quewi::video
