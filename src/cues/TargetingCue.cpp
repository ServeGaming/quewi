#include "cues/TargetingCue.h"

#include <QJsonObject>

namespace quewi::cues {

TargetingCue::TargetingCue(QObject *parent) : Cue(parent) {}
TargetingCue::~TargetingCue() = default;

QVariant TargetingCue::field(const QString &key) const
{
    if (key == QLatin1String("targetId")) return m_targetId;
    return Cue::field(key);
}

void TargetingCue::setField(const QString &key, const QVariant &value)
{
    if (key == QLatin1String("targetId")) {
        const auto v = value.toUuid();
        if (m_targetId == v) return;
        m_targetId = v;
        emitChanged();
        return;
    }
    Cue::setField(key, value);
}

QJsonObject TargetingCue::toPayload() const
{
    auto o = Cue::toPayload();
    o.insert(QStringLiteral("targetId"), m_targetId.toString());
    return o;
}

void TargetingCue::fromPayload(const QJsonObject &payload)
{
    Cue::fromPayload(payload);
    m_targetId = QUuid(payload.value(QStringLiteral("targetId")).toString());
}

} // namespace quewi::cues
