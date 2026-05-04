#include "audio/AudioCue.h"

#include <QJsonObject>

namespace quewi::audio {

AudioCue::AudioCue(QObject *parent) : cues::Cue(parent) {}
AudioCue::~AudioCue() = default;

QVariant AudioCue::field(const QString &key) const
{
    if (key == QLatin1String("filePath"))       return m_filePath;
    if (key == QLatin1String("gainDb"))         return m_gainDb;
    if (key == QLatin1String("fadeInSeconds"))  return m_fadeInSeconds;
    if (key == QLatin1String("fadeOutSeconds")) return m_fadeOutSeconds;
    if (key == QLatin1String("trimInSeconds"))  return m_trimInSeconds;
    if (key == QLatin1String("trimOutSeconds")) return m_trimOutSeconds;
    if (key == QLatin1String("pan"))            return m_pan;
    if (key == QLatin1String("loop"))           return m_loop;
    return cues::Cue::field(key);
}

void AudioCue::setField(const QString &key, const QVariant &value)
{
    auto setDouble = [&](double &target) {
        if (qFuzzyCompare(target + 1.0, value.toDouble() + 1.0)) return false;
        target = value.toDouble();
        emitChanged();
        return true;
    };
    if (key == QLatin1String("filePath")) {
        const auto v = value.toString();
        if (m_filePath == v) return;
        m_filePath = v;
        m_file.reset();
        emitChanged();
        return;
    }
    if (key == QLatin1String("gainDb"))         { setDouble(m_gainDb);         return; }
    if (key == QLatin1String("fadeInSeconds"))  { setDouble(m_fadeInSeconds);  return; }
    if (key == QLatin1String("fadeOutSeconds")) { setDouble(m_fadeOutSeconds); return; }
    if (key == QLatin1String("trimInSeconds"))  { setDouble(m_trimInSeconds);  return; }
    if (key == QLatin1String("trimOutSeconds")) { setDouble(m_trimOutSeconds); return; }
    if (key == QLatin1String("pan"))            { setDouble(m_pan);            return; }
    if (key == QLatin1String("loop")) {
        if (m_loop == value.toBool()) return;
        m_loop = value.toBool();
        emitChanged();
        return;
    }
    cues::Cue::setField(key, value);
}

QJsonObject AudioCue::toPayload() const
{
    auto o = cues::Cue::toPayload();
    o.insert(QStringLiteral("filePath"),       m_filePath);
    o.insert(QStringLiteral("gainDb"),         m_gainDb);
    o.insert(QStringLiteral("fadeInSeconds"),  m_fadeInSeconds);
    o.insert(QStringLiteral("fadeOutSeconds"), m_fadeOutSeconds);
    o.insert(QStringLiteral("trimInSeconds"),  m_trimInSeconds);
    o.insert(QStringLiteral("trimOutSeconds"), m_trimOutSeconds);
    o.insert(QStringLiteral("pan"),            m_pan);
    o.insert(QStringLiteral("loop"),           m_loop);
    return o;
}

void AudioCue::fromPayload(const QJsonObject &payload)
{
    cues::Cue::fromPayload(payload);
    m_filePath       = payload.value(QStringLiteral("filePath")).toString();
    m_gainDb         = payload.value(QStringLiteral("gainDb")).toDouble();
    m_fadeInSeconds  = payload.value(QStringLiteral("fadeInSeconds")).toDouble();
    m_fadeOutSeconds = payload.value(QStringLiteral("fadeOutSeconds")).toDouble();
    m_trimInSeconds  = payload.value(QStringLiteral("trimInSeconds")).toDouble();
    m_trimOutSeconds = payload.value(QStringLiteral("trimOutSeconds")).toDouble();
    m_pan            = payload.value(QStringLiteral("pan")).toDouble();
    m_loop           = payload.value(QStringLiteral("loop")).toBool();
    m_file.reset();
}

void AudioCue::prepare()
{
    if (m_filePath.isEmpty()) return;

    if (!m_file) {
        m_file = std::make_shared<AudioFile>();
        m_file->load(m_filePath);
        // Bubble peak/state changes up so the inspector can repaint.
        connect(m_file.get(), &AudioFile::stateChanged,
                this, [this](AudioFile::State) { emitChanged(); });
    } else if (m_file->path() != m_filePath
               && m_file->state() != AudioFile::State::Loading) {
        m_file->load(m_filePath);
    }
}

} // namespace quewi::audio
