#include "cues/GroupCue.h"

#include <QJsonArray>
#include <QJsonObject>

namespace quewi::cues {

GroupCue::GroupCue(QObject *parent) : Cue(parent) {}
GroupCue::~GroupCue() = default;

QVariant GroupCue::field(const QString &key) const
{
    if (key == QLatin1String("mode")) return static_cast<int>(m_mode);
    if (key == QLatin1String("stepInterval")) return m_stepInterval;
    if (key == QLatin1String("childIds")) {
        QStringList ids;
        for (const auto &id : m_childIds) ids << id.toString();
        return ids;
    }
    if (key == QLatin1String("childOffsets")) {
        QVariantList vs;
        for (double d : m_childOffsets) vs << d;
        return vs;
    }
    return Cue::field(key);
}

void GroupCue::setField(const QString &key, const QVariant &value)
{
    if (key == QLatin1String("mode")) {
        const auto v = static_cast<Mode>(value.toInt());
        if (m_mode == v) return;
        m_mode = v;
        emitChanged();
        return;
    }
    if (key == QLatin1String("stepInterval")) {
        if (qFuzzyCompare(m_stepInterval + 1.0, value.toDouble() + 1.0)) return;
        m_stepInterval = value.toDouble();
        emitChanged();
        return;
    }
    if (key == QLatin1String("childIds")) {
        QList<core::CueId> ids;
        for (const auto &s : value.toStringList()) ids.append(QUuid(s));
        if (ids == m_childIds) return;
        m_childIds = ids;
        emitChanged();
        return;
    }
    if (key == QLatin1String("childOffsets")) {
        QList<double> ds;
        for (const auto &v : value.toList()) ds.append(v.toDouble());
        if (ds == m_childOffsets) return;
        m_childOffsets = ds;
        emitChanged();
        return;
    }
    Cue::setField(key, value);
}

QJsonObject GroupCue::toPayload() const
{
    auto o = Cue::toPayload();
    o.insert(QStringLiteral("mode"),         static_cast<int>(m_mode));
    o.insert(QStringLiteral("stepInterval"), m_stepInterval);
    QJsonArray a;
    for (const auto &id : m_childIds) a.append(id.toString());
    o.insert(QStringLiteral("childIds"), a);
    QJsonArray b;
    for (double d : m_childOffsets) b.append(d);
    o.insert(QStringLiteral("childOffsets"), b);
    return o;
}

void GroupCue::fromPayload(const QJsonObject &payload)
{
    Cue::fromPayload(payload);
    m_mode = static_cast<Mode>(payload.value(QStringLiteral("mode")).toInt(static_cast<int>(m_mode)));
    m_stepInterval = payload.value(QStringLiteral("stepInterval")).toDouble(m_stepInterval);
    m_childIds.clear();
    for (const auto &v : payload.value(QStringLiteral("childIds")).toArray()) {
        m_childIds.append(QUuid(v.toString()));
    }
    m_childOffsets.clear();
    for (const auto &v : payload.value(QStringLiteral("childOffsets")).toArray()) {
        m_childOffsets.append(v.toDouble());
    }
}

} // namespace quewi::cues
