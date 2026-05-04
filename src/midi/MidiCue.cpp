#include "midi/MidiCue.h"

#include <QJsonObject>

namespace quewi::midi {

// ----- MidiCue --------------------------------------------------------

MidiCue::MidiCue(QObject *parent) : cues::Cue(parent) {}
MidiCue::~MidiCue() = default;

QVariant MidiCue::field(const QString &key) const
{
    if (key == QLatin1String("portName")) return m_portName;
    if (key == QLatin1String("bytes"))    return m_bytes.toHex(' ');
    return Cue::field(key);
}

void MidiCue::setField(const QString &key, const QVariant &value)
{
    if (key == QLatin1String("portName")) {
        const auto v = value.toString();
        if (m_portName == v) return;
        m_portName = v;
        emitChanged();
        return;
    }
    if (key == QLatin1String("bytes")) {
        // Accept hex-string from the Inspector; trims spaces.
        const auto raw = value.toString().remove(QChar(' ')).remove(QChar(',')).remove(QChar('\n'));
        const auto v = QByteArray::fromHex(raw.toLatin1());
        if (m_bytes == v) return;
        m_bytes = v;
        emitChanged();
        return;
    }
    Cue::setField(key, value);
}

QJsonObject MidiCue::toPayload() const
{
    auto o = Cue::toPayload();
    o.insert(QStringLiteral("portName"), m_portName);
    o.insert(QStringLiteral("bytes"),    QString::fromLatin1(m_bytes.toHex()));
    return o;
}

void MidiCue::fromPayload(const QJsonObject &payload)
{
    Cue::fromPayload(payload);
    m_portName = payload.value(QStringLiteral("portName")).toString();
    m_bytes    = QByteArray::fromHex(payload.value(QStringLiteral("bytes")).toString().toLatin1());
}

// ----- MscCue ---------------------------------------------------------

MscCue::MscCue(QObject *parent) : cues::Cue(parent) {}
MscCue::~MscCue() = default;

QVariant MscCue::field(const QString &key) const
{
    if (key == QLatin1String("portName"))      return m_portName;
    if (key == QLatin1String("deviceId"))      return m_deviceId;
    if (key == QLatin1String("commandFormat")) return m_commandFormat;
    if (key == QLatin1String("command"))       return m_command;
    if (key == QLatin1String("qNumber"))       return m_qNumber;
    if (key == QLatin1String("qList"))         return m_qList;
    if (key == QLatin1String("qPath"))         return m_qPath;
    return Cue::field(key);
}

void MscCue::setField(const QString &key, const QVariant &value)
{
    auto trySetString = [&](QString &target) {
        const auto v = value.toString();
        if (target == v) return false;
        target = v;
        emitChanged();
        return true;
    };
    auto trySetInt = [&](int &target) {
        const auto v = value.toInt();
        if (target == v) return false;
        target = v;
        emitChanged();
        return true;
    };
    if (key == QLatin1String("portName"))      { trySetString(m_portName); return; }
    if (key == QLatin1String("qNumber"))       { trySetString(m_qNumber);  return; }
    if (key == QLatin1String("qList"))         { trySetString(m_qList);    return; }
    if (key == QLatin1String("qPath"))         { trySetString(m_qPath);    return; }
    if (key == QLatin1String("deviceId"))      { trySetInt(m_deviceId);    return; }
    if (key == QLatin1String("commandFormat")) { trySetInt(m_commandFormat); return; }
    if (key == QLatin1String("command"))       { trySetInt(m_command);     return; }
    Cue::setField(key, value);
}

QJsonObject MscCue::toPayload() const
{
    auto o = Cue::toPayload();
    o.insert(QStringLiteral("portName"),      m_portName);
    o.insert(QStringLiteral("deviceId"),      m_deviceId);
    o.insert(QStringLiteral("commandFormat"), m_commandFormat);
    o.insert(QStringLiteral("command"),       m_command);
    o.insert(QStringLiteral("qNumber"),       m_qNumber);
    o.insert(QStringLiteral("qList"),         m_qList);
    o.insert(QStringLiteral("qPath"),         m_qPath);
    return o;
}

void MscCue::fromPayload(const QJsonObject &payload)
{
    Cue::fromPayload(payload);
    m_portName      = payload.value(QStringLiteral("portName")).toString();
    m_deviceId      = payload.value(QStringLiteral("deviceId")).toInt(m_deviceId);
    m_commandFormat = payload.value(QStringLiteral("commandFormat")).toInt(m_commandFormat);
    m_command       = payload.value(QStringLiteral("command")).toInt(m_command);
    m_qNumber       = payload.value(QStringLiteral("qNumber")).toString();
    m_qList         = payload.value(QStringLiteral("qList")).toString();
    m_qPath         = payload.value(QStringLiteral("qPath")).toString();
}

QByteArray MscCue::buildPayload() const
{
    // Q_number / Q_list / Q_path are ASCII strings separated by 0x00.
    // Empty fields are sent as just a separator.
    QByteArray out;
    out.append(m_qNumber.toLatin1());
    if (!m_qList.isEmpty() || !m_qPath.isEmpty()) {
        out.append(static_cast<char>(0x00));
        out.append(m_qList.toLatin1());
    }
    if (!m_qPath.isEmpty()) {
        out.append(static_cast<char>(0x00));
        out.append(m_qPath.toLatin1());
    }
    return out;
}

} // namespace quewi::midi
