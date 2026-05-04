#include "cues/WaitCue.h"

#include <QJsonObject>

namespace quewi::cues {

WaitCue::WaitCue(QObject *parent) : Cue(parent) {}
WaitCue::~WaitCue() = default;

QVariant WaitCue::field(const QString &key) const
{
    if (key == QLatin1String("durationSeconds")) return m_durationSeconds;
    return Cue::field(key);
}

void WaitCue::setField(const QString &key, const QVariant &value)
{
    if (key == QLatin1String("durationSeconds")) {
        if (qFuzzyCompare(m_durationSeconds + 1.0, value.toDouble() + 1.0)) return;
        m_durationSeconds = value.toDouble();
        emitChanged();
        return;
    }
    Cue::setField(key, value);
}

QJsonObject WaitCue::toPayload() const
{
    auto o = Cue::toPayload();
    o.insert(QStringLiteral("durationSeconds"), m_durationSeconds);
    return o;
}

void WaitCue::fromPayload(const QJsonObject &payload)
{
    Cue::fromPayload(payload);
    m_durationSeconds = payload.value(QStringLiteral("durationSeconds")).toDouble(m_durationSeconds);
}

} // namespace quewi::cues
