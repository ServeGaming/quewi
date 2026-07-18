#include "cues/Cue.h"

#include "core/CueList.h"

namespace quewi::cues {

Cue::Cue(QObject *parent)
    : QObject(parent)
    , m_id(QUuid::createUuid())
{
}

Cue::~Cue() = default;

QVariant Cue::field(const QString &key) const
{
    if (key == QLatin1String("number"))       return m_number;
    if (key == QLatin1String("name"))         return m_name;
    if (key == QLatin1String("preWait"))      return m_preWait;
    if (key == QLatin1String("postWait"))     return m_postWait;
    if (key == QLatin1String("continueMode")) return static_cast<int>(m_continueMode);
    if (key == QLatin1String("notes"))        return m_notes;
    if (key == QLatin1String("armed"))        return m_armed;
    if (key == QLatin1String("color"))        return m_color;
    if (key == QLatin1String("linkedCueId"))  return QVariant::fromValue(m_linkedCueId);
    return {};
}

void Cue::setField(const QString &key, const QVariant &value)
{
    if (key == QLatin1String("number"))            m_number = value.toDouble();
    else if (key == QLatin1String("name"))         m_name = value.toString();
    else if (key == QLatin1String("preWait"))      m_preWait = value.toDouble();
    else if (key == QLatin1String("postWait"))     m_postWait = value.toDouble();
    else if (key == QLatin1String("continueMode")) m_continueMode = static_cast<ContinueMode>(value.toInt());
    else if (key == QLatin1String("notes"))        m_notes = value.toString();
    else if (key == QLatin1String("armed"))        m_armed = value.toBool();
    else if (key == QLatin1String("color"))        m_color = value.value<QColor>();
    else if (key == QLatin1String("linkedCueId"))  m_linkedCueId = value.value<QUuid>();
    else return;
    emitChanged();
}

QJsonObject Cue::toPayload() const
{
    QJsonObject o {
        { QStringLiteral("number"),       m_number },
        { QStringLiteral("name"),         m_name },
        { QStringLiteral("preWait"),      m_preWait },
        { QStringLiteral("postWait"),     m_postWait },
        { QStringLiteral("continueMode"), static_cast<int>(m_continueMode) },
        { QStringLiteral("notes"),        m_notes },
        { QStringLiteral("armed"),        m_armed },
    };
    if (m_color.isValid()) {
        o.insert(QStringLiteral("color"), m_color.name(QColor::HexArgb));
    }
    if (!m_linkedCueId.isNull()) {
        o.insert(QStringLiteral("linkedCueId"),
                 m_linkedCueId.toString(QUuid::WithoutBraces));
    }
    return o;
}

void Cue::fromPayload(const QJsonObject &payload)
{
    m_number       = payload.value(QStringLiteral("number")).toDouble();
    m_name         = payload.value(QStringLiteral("name")).toString();
    m_preWait      = payload.value(QStringLiteral("preWait")).toDouble();
    m_postWait     = payload.value(QStringLiteral("postWait")).toDouble();
    m_continueMode = static_cast<ContinueMode>(payload.value(QStringLiteral("continueMode")).toInt());
    m_notes        = payload.value(QStringLiteral("notes")).toString();
    m_armed        = payload.value(QStringLiteral("armed")).toBool(true);
    const auto colName = payload.value(QStringLiteral("color")).toString();
    m_color = colName.isEmpty() ? QColor() : QColor(colName);
    const auto linked = payload.value(QStringLiteral("linkedCueId")).toString();
    m_linkedCueId = linked.isEmpty() ? core::CueId() : core::CueId(linked);
}

void Cue::emitChanged()
{
    emit changed();

    // Bubble up to the owning CueList so models can route a row signal.
    if (auto *list = qobject_cast<core::CueList *>(parent())) {
        int row = list->rowOf(this);
        if (row >= 0) emit list->cueChanged(row);
    }
}

} // namespace quewi::cues
