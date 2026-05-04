#include "osc/OscCue.h"

#include <QJsonObject>
#include <QStringList>

namespace quewi::osc {

OscCue::OscCue(QObject *parent) : cues::Cue(parent) {}
OscCue::~OscCue() = default;

QVariant OscCue::field(const QString &key) const
{
    if (key == QLatin1String("address"))   return m_address;
    if (key == QLatin1String("host"))      return m_host;
    if (key == QLatin1String("port"))      return m_port;
    if (key == QLatin1String("transport")) return m_transport;
    if (key == QLatin1String("rawArgs"))   return m_rawArgs;
    return cues::Cue::field(key);
}

void OscCue::setField(const QString &key, const QVariant &value)
{
    if (key == QLatin1String("address")) {
        if (m_address == value.toString()) return;
        m_address = value.toString();
        emitChanged();
        return;
    }
    if (key == QLatin1String("host")) {
        if (m_host == value.toString()) return;
        m_host = value.toString();
        emitChanged();
        return;
    }
    if (key == QLatin1String("port")) {
        const auto p = static_cast<quint16>(value.toInt());
        if (m_port == p) return;
        m_port = p;
        emitChanged();
        return;
    }
    if (key == QLatin1String("transport")) {
        if (m_transport == value.toInt()) return;
        m_transport = value.toInt();
        emitChanged();
        return;
    }
    if (key == QLatin1String("rawArgs")) {
        if (m_rawArgs == value.toString()) return;
        m_rawArgs = value.toString();
        emitChanged();
        return;
    }
    cues::Cue::setField(key, value);
}

QJsonObject OscCue::toPayload() const
{
    auto obj = cues::Cue::toPayload();
    obj.insert(QStringLiteral("address"),   m_address);
    obj.insert(QStringLiteral("host"),      m_host);
    obj.insert(QStringLiteral("port"),      m_port);
    obj.insert(QStringLiteral("transport"), m_transport);
    obj.insert(QStringLiteral("rawArgs"),   m_rawArgs);
    return obj;
}

void OscCue::fromPayload(const QJsonObject &payload)
{
    cues::Cue::fromPayload(payload);
    m_address   = payload.value(QStringLiteral("address")).toString(m_address);
    m_host      = payload.value(QStringLiteral("host")).toString(m_host);
    m_port      = static_cast<quint16>(payload.value(QStringLiteral("port")).toInt(m_port));
    m_transport = payload.value(QStringLiteral("transport")).toInt(m_transport);
    m_rawArgs   = payload.value(QStringLiteral("rawArgs")).toString(m_rawArgs);
}

namespace {

// Auto-type a token from rawArgs. Order: bool/nil keywords → int → float
// → quoted-string strip → bare string.
Argument parseToken(QStringView token)
{
    const auto trimmed = token.trimmed();
    if (trimmed.compare(QStringLiteral("true"),  Qt::CaseInsensitive) == 0) return Argument::T();
    if (trimmed.compare(QStringLiteral("false"), Qt::CaseInsensitive) == 0) return Argument::F();
    if (trimmed.compare(QStringLiteral("nil"),   Qt::CaseInsensitive) == 0) return Argument::N();
    if (trimmed.compare(QStringLiteral("inf"),   Qt::CaseInsensitive) == 0) return Argument::I();

    bool ok = false;
    const qint64 i = trimmed.toLongLong(&ok);
    if (ok && trimmed.indexOf(QChar('.')) < 0) {
        if (i >= INT32_MIN && i <= INT32_MAX) return Argument::i(static_cast<qint32>(i));
        return Argument::h(i);
    }
    const double d = trimmed.toDouble(&ok);
    if (ok) return Argument::f(static_cast<float>(d));

    // Strip surrounding quotes if present.
    if (trimmed.size() >= 2 && trimmed.front() == QChar('"') && trimmed.back() == QChar('"'))
        return Argument::s(trimmed.mid(1, trimmed.size() - 2).toString());
    return Argument::s(trimmed.toString());
}

} // namespace

Message OscCue::buildMessage() const
{
    Message m;
    m.address = m_address;
    if (m_rawArgs.isEmpty()) return m;

    // Naive split on commas. Quoted strings with embedded commas would
    // need a smarter tokenizer; revisit when the typed-arg editor lands.
    const auto tokens = m_rawArgs.split(QChar(','), Qt::SkipEmptyParts);
    for (const auto &t : tokens) m.args.push_back(parseToken(t));
    return m;
}

OscCue::DestinationView OscCue::destination() const
{
    return { id().toString(), name(), m_host, m_port, m_transport };
}

} // namespace quewi::osc
