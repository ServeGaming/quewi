#include "lighting/LightCue.h"

#include <QJsonArray>
#include <QJsonObject>

#include <algorithm>

namespace quewi::lighting {

// ---------------- LightCue ----------------

LightCue::LightCue(QObject *parent) : cues::Cue(parent) {}
LightCue::~LightCue() = default;

QVariant LightCue::field(const QString &key) const
{
    if (key == QLatin1String("universe")) return m_universe;
    if (key == QLatin1String("channels")) return channelsMap();
    return cues::Cue::field(key);
}

void LightCue::setField(const QString &key, const QVariant &value)
{
    if (key == QLatin1String("universe")) {
        const auto u = static_cast<quint16>(value.toInt());
        if (m_universe == u) return;
        m_universe = u;
        emitChanged();
        return;
    }
    if (key == QLatin1String("channels")) {
        setChannelsMap(value.toMap());
        return;
    }
    cues::Cue::setField(key, value);
}

QVariantMap LightCue::channelsMap() const
{
    QVariantMap m;
    for (auto it = m_channels.constBegin(); it != m_channels.constEnd(); ++it) {
        m.insert(QString::number(it.key()), it.value());
    }
    return m;
}

void LightCue::setChannelsMap(const QVariantMap &map)
{
    QHash<int, int> next;
    for (auto it = map.constBegin(); it != map.constEnd(); ++it) {
        bool ok = false;
        const int ch = it.key().toInt(&ok);
        if (!ok || ch < 1 || ch > 512) continue;
        next.insert(ch, std::clamp(it.value().toInt(), 0, 255));
    }
    if (next == m_channels) return;
    m_channels = std::move(next);
    emitChanged();
}

QJsonObject LightCue::toPayload() const
{
    auto o = cues::Cue::toPayload();
    o.insert(QStringLiteral("universe"), m_universe);
    QJsonObject channels;
    for (auto it = m_channels.constBegin(); it != m_channels.constEnd(); ++it) {
        channels.insert(QString::number(it.key()), it.value());
    }
    o.insert(QStringLiteral("channels"), channels);
    return o;
}

void LightCue::fromPayload(const QJsonObject &payload)
{
    cues::Cue::fromPayload(payload);
    m_universe = static_cast<quint16>(payload.value(QStringLiteral("universe")).toInt(1));
    m_channels.clear();
    const auto channels = payload.value(QStringLiteral("channels")).toObject();
    for (auto it = channels.constBegin(); it != channels.constEnd(); ++it) {
        bool ok = false;
        const int ch = it.key().toInt(&ok);
        if (ok) m_channels.insert(ch, it.value().toInt());
    }
}

// ---------------- LightFadeCue ----------------

LightFadeCue::LightFadeCue(QObject *parent) : cues::Cue(parent) {}
LightFadeCue::~LightFadeCue() = default;

QVariant LightFadeCue::field(const QString &key) const
{
    if (key == QLatin1String("targetId"))        return m_targetId;
    if (key == QLatin1String("durationSeconds")) return m_durationSeconds;
    return cues::Cue::field(key);
}

void LightFadeCue::setField(const QString &key, const QVariant &value)
{
    if (key == QLatin1String("targetId")) {
        const auto v = value.toUuid();
        if (m_targetId == v) return;
        m_targetId = v;
        emitChanged();
        return;
    }
    if (key == QLatin1String("durationSeconds")) {
        if (qFuzzyCompare(m_durationSeconds, value.toDouble())) return;
        m_durationSeconds = value.toDouble();
        emitChanged();
        return;
    }
    cues::Cue::setField(key, value);
}

QJsonObject LightFadeCue::toPayload() const
{
    auto o = cues::Cue::toPayload();
    o.insert(QStringLiteral("targetId"),        m_targetId.toString());
    o.insert(QStringLiteral("durationSeconds"), m_durationSeconds);
    return o;
}

void LightFadeCue::fromPayload(const QJsonObject &payload)
{
    cues::Cue::fromPayload(payload);
    m_targetId = QUuid(payload.value(QStringLiteral("targetId")).toString());
    m_durationSeconds = payload.value(QStringLiteral("durationSeconds")).toDouble(m_durationSeconds);
}

} // namespace quewi::lighting
