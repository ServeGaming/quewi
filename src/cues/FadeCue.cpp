#include "cues/FadeCue.h"

#include <QJsonObject>

namespace quewi::cues {

FadeCue::FadeCue(QObject *parent) : Cue(parent) {}
FadeCue::~FadeCue() = default;

QVariant FadeCue::field(const QString &key) const
{
    if (key == QLatin1String("targetId"))        return m_targetId;
    if (key == QLatin1String("parameter"))       return m_parameter;
    if (key == QLatin1String("targetValue"))     return m_targetValue;
    if (key == QLatin1String("durationSeconds")) return m_durationSeconds;
    return Cue::field(key);
}

void FadeCue::setField(const QString &key, const QVariant &value)
{
    if (key == QLatin1String("targetId")) {
        const auto v = value.toUuid();
        if (m_targetId == v) return;
        m_targetId = v;
        emitChanged();
        return;
    }
    if (key == QLatin1String("parameter")) {
        const auto v = value.toString();
        if (m_parameter == v) return;
        m_parameter = v;
        emitChanged();
        return;
    }
    if (key == QLatin1String("targetValue")) {
        if (qFuzzyCompare(m_targetValue, value.toDouble())) return;
        m_targetValue = value.toDouble();
        emitChanged();
        return;
    }
    if (key == QLatin1String("durationSeconds")) {
        if (qFuzzyCompare(m_durationSeconds, value.toDouble())) return;
        m_durationSeconds = value.toDouble();
        emitChanged();
        return;
    }
    Cue::setField(key, value);
}

QJsonObject FadeCue::toPayload() const
{
    auto o = Cue::toPayload();
    o.insert(QStringLiteral("targetId"),        m_targetId.toString());
    o.insert(QStringLiteral("parameter"),       m_parameter);
    o.insert(QStringLiteral("targetValue"),     m_targetValue);
    o.insert(QStringLiteral("durationSeconds"), m_durationSeconds);
    return o;
}

void FadeCue::fromPayload(const QJsonObject &payload)
{
    Cue::fromPayload(payload);
    m_targetId        = QUuid(payload.value(QStringLiteral("targetId")).toString());
    m_parameter       = payload.value(QStringLiteral("parameter")).toString(m_parameter);
    m_targetValue     = payload.value(QStringLiteral("targetValue")).toDouble(m_targetValue);
    m_durationSeconds = payload.value(QStringLiteral("durationSeconds")).toDouble(m_durationSeconds);
}

} // namespace quewi::cues
