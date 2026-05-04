#include "video/VideoCue.h"

#include <QJsonObject>

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

VideoCue::VideoCue(QObject *parent) : VisualCue(parent) {}
VideoCue::~VideoCue() = default;

QVariant VideoCue::field(const QString &key) const
{
    if (key == QLatin1String("filePath")) return m_filePath;
    if (key == QLatin1String("loop"))     return m_loop;
    return VisualCue::field(key);
}

void VideoCue::setField(const QString &key, const QVariant &value)
{
    if (key == QLatin1String("filePath")) {
        if (m_filePath == value.toString()) return;
        m_filePath = value.toString();
        emitChanged();
        return;
    }
    if (key == QLatin1String("loop")) {
        if (m_loop == value.toBool()) return;
        m_loop = value.toBool();
        emitChanged();
        return;
    }
    VisualCue::setField(key, value);
}

QJsonObject VideoCue::toPayload() const
{
    auto o = visualToPayload();
    o.insert(QStringLiteral("filePath"), m_filePath);
    o.insert(QStringLiteral("loop"), m_loop);
    return o;
}

void VideoCue::fromPayload(const QJsonObject &payload)
{
    visualFromPayload(payload);
    m_filePath = payload.value(QStringLiteral("filePath")).toString();
    m_loop     = payload.value(QStringLiteral("loop")).toBool();
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
