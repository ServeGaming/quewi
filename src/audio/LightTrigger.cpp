#include "audio/LightTrigger.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QRegularExpression>

#include <algorithm>
#include <tuple>

namespace quewi::audio {

namespace {

QString tr(const char *s) { return QCoreApplication::translate("LightTrigger", s); }

QString mscCommandName(int c)
{
    switch (c) {
    case 0x01: return QStringLiteral("GO");
    case 0x02: return QStringLiteral("STOP");
    case 0x03: return QStringLiteral("RESUME");
    case 0x04: return QStringLiteral("TIMED GO");
    case 0x05: return QStringLiteral("LOAD");
    case 0x06: return QStringLiteral("SET");
    case 0x07: return QStringLiteral("FIRE");
    case 0x08: return QStringLiteral("ALL OFF");
    case 0x09: return QStringLiteral("RESTORE");
    case 0x0A: return QStringLiteral("RESET");
    case 0x0B: return QStringLiteral("GO OFF");
    default:   return QStringLiteral("0x%1").arg(c, 2, 16, QChar('0'));
    }
}

bool numberOk(const QVariant &v)
{
    bool ok = false;
    v.toDouble(&ok);
    return ok;
}

} // namespace

// ── TriggerAction ────────────────────────────────────────────────────────────

QString TriggerAction::kindKey(Kind k)
{
    switch (k) {
    case Kind::Osc:     return QStringLiteral("osc");
    case Kind::Midi:    return QStringLiteral("midi");
    case Kind::Msc:     return QStringLiteral("msc");
    case Kind::FireCue: return QStringLiteral("cue");
    case Kind::None:    break;
    }
    return QStringLiteral("none");
}

TriggerAction::Kind TriggerAction::kindFromKey(const QString &key)
{
    const QString k = key.trimmed().toLower();
    if (k == QLatin1String("osc"))  return Kind::Osc;
    if (k == QLatin1String("midi")) return Kind::Midi;
    if (k == QLatin1String("msc"))  return Kind::Msc;
    if (k == QLatin1String("cue") || k == QLatin1String("firecue")) return Kind::FireCue;
    return Kind::None;
}

QString TriggerAction::midiTypeKey(MidiType t)
{
    switch (t) {
    case MidiType::NoteOff:       return QStringLiteral("noteOff");
    case MidiType::ControlChange: return QStringLiteral("cc");
    case MidiType::ProgramChange: return QStringLiteral("program");
    case MidiType::Raw:           return QStringLiteral("raw");
    case MidiType::NoteOn:        break;
    }
    return QStringLiteral("noteOn");
}

TriggerAction::MidiType TriggerAction::midiTypeFromKey(const QString &key)
{
    const QString k = key.trimmed().toLower();
    if (k == QLatin1String("noteoff")) return MidiType::NoteOff;
    if (k == QLatin1String("cc") || k == QLatin1String("controlchange")) return MidiType::ControlChange;
    if (k == QLatin1String("program") || k == QLatin1String("programchange")) return MidiType::ProgramChange;
    if (k == QLatin1String("raw")) return MidiType::Raw;
    return MidiType::NoteOn;
}

QByteArray TriggerAction::midiBytes() const
{
    const auto ch = char(std::clamp(channel, 1, 16) - 1);
    const auto d1 = char(std::clamp(data1, 0, 127));
    const auto d2 = char(std::clamp(data2, 0, 127));
    QByteArray b;
    auto msg = [&](int status, bool twoData) {
        b.append(char(status | ch));
        b.append(d1);
        if (twoData) b.append(d2);
    };
    switch (midiType) {
    case MidiType::NoteOn:        msg(0x90, true);  break;
    case MidiType::NoteOff:       msg(0x80, true);  break;
    case MidiType::ControlChange: msg(0xB0, true);  break;
    case MidiType::ProgramChange: msg(0xC0, false); break;
    case MidiType::Raw: {
        static const QRegularExpression sep(QStringLiteral("[\\s,]+"));
        for (const auto &tok : rawHex.split(sep, Qt::SkipEmptyParts)) {
            bool ok = false;
            const int v = tok.toInt(&ok, 16);
            if (!ok || v < 0 || v > 255) return {};
            b.append(char(v));
        }
        break;
    }
    }
    return b;
}

QByteArray TriggerAction::mscPayload() const
{
    QByteArray out = qNumber.toLatin1();
    if (!qList.isEmpty()) {
        out.append(char(0x00));
        out.append(qList.toLatin1());
    }
    return out;
}

QString TriggerAction::summary() const
{
    switch (kind) {
    case Kind::None:
        return tr("nothing");
    case Kind::Osc: {
        QString s = QStringLiteral("OSC %1").arg(address.isEmpty() ? tr("(no address)") : address);
        if (!args.trimmed().isEmpty()) s += QLatin1Char(' ') + args.trimmed();
        return s + QStringLiteral(" → %1:%2").arg(host).arg(port);
    }
    case Kind::Midi:
        switch (midiType) {
        case MidiType::NoteOn:
            return tr("MIDI note on %1 vel %2 ch %3").arg(data1).arg(data2).arg(channel);
        case MidiType::NoteOff:
            return tr("MIDI note off %1 ch %2").arg(data1).arg(channel);
        case MidiType::ControlChange:
            return tr("MIDI CC %1 = %2 ch %3").arg(data1).arg(data2).arg(channel);
        case MidiType::ProgramChange:
            return tr("MIDI program %1 ch %2").arg(data1).arg(channel);
        case MidiType::Raw:
            return tr("MIDI %1").arg(rawHex.trimmed().toUpper());
        }
        break;
    case Kind::Msc: {
        QString s = QStringLiteral("MSC %1").arg(mscCommandName(command));
        if (!qNumber.isEmpty()) s += tr(" cue %1").arg(qNumber);
        if (!qList.isEmpty())   s += tr(" list %1").arg(qList);
        return s;
    }
    case Kind::FireCue:
        return tr("fire cue");
    }
    return {};
}

QJsonObject TriggerAction::toJson() const
{
    QJsonObject o{{QStringLiteral("kind"), kindKey(kind)}};
    switch (kind) {
    case Kind::None:
        break;
    case Kind::Osc:
        o.insert(QStringLiteral("host"), host);
        o.insert(QStringLiteral("port"), port);
        o.insert(QStringLiteral("transport"), transport);
        o.insert(QStringLiteral("address"), address);
        o.insert(QStringLiteral("args"), args);
        break;
    case Kind::Midi:
        o.insert(QStringLiteral("midiPort"), midiPort);
        o.insert(QStringLiteral("midiType"), midiTypeKey(midiType));
        o.insert(QStringLiteral("channel"), channel);
        o.insert(QStringLiteral("data1"), data1);
        o.insert(QStringLiteral("data2"), data2);
        if (midiType == MidiType::Raw) o.insert(QStringLiteral("rawHex"), rawHex);
        break;
    case Kind::Msc:
        o.insert(QStringLiteral("midiPort"), midiPort);
        o.insert(QStringLiteral("deviceId"), deviceId);
        o.insert(QStringLiteral("commandFormat"), commandFormat);
        o.insert(QStringLiteral("command"), command);
        o.insert(QStringLiteral("qNumber"), qNumber);
        o.insert(QStringLiteral("qList"), qList);
        break;
    case Kind::FireCue:
        o.insert(QStringLiteral("cueId"), cueId.toString(QUuid::WithoutBraces));
        break;
    }
    return o;
}

TriggerAction TriggerAction::fromJson(const QJsonObject &o)
{
    TriggerAction a;
    for (auto it = o.begin(); it != o.end(); ++it)
        a.setField(it.key(), it.value().toVariant());
    return a;
}

QVariant TriggerAction::field(const QString &key) const
{
    if (key == QLatin1String("kind"))          return kindKey(kind);
    if (key == QLatin1String("host"))          return host;
    if (key == QLatin1String("port"))          return port;
    if (key == QLatin1String("transport"))     return transport;
    if (key == QLatin1String("address"))       return address;
    if (key == QLatin1String("args"))          return args;
    if (key == QLatin1String("midiPort"))      return midiPort;
    if (key == QLatin1String("midiType"))      return midiTypeKey(midiType);
    if (key == QLatin1String("channel"))       return channel;
    if (key == QLatin1String("data1"))         return data1;
    if (key == QLatin1String("data2"))         return data2;
    if (key == QLatin1String("rawHex"))        return rawHex;
    if (key == QLatin1String("deviceId"))      return deviceId;
    if (key == QLatin1String("commandFormat")) return commandFormat;
    if (key == QLatin1String("command"))       return command;
    if (key == QLatin1String("qNumber"))       return qNumber;
    if (key == QLatin1String("qList"))         return qList;
    if (key == QLatin1String("cueId"))         return cueId.toString(QUuid::WithoutBraces);
    return {};
}

bool TriggerAction::setField(const QString &key, const QVariant &v)
{
    auto toInt = [&](int &dst, int lo, int hi) {
        if (!numberOk(v)) return false;
        dst = std::clamp(int(v.toDouble()), lo, hi);
        return true;
    };
    if (key == QLatin1String("kind"))      { kind = kindFromKey(v.toString()); return true; }
    if (key == QLatin1String("host"))      { host = v.toString().trimmed(); return true; }
    if (key == QLatin1String("port"))      return toInt(port, 1, 65535);
    if (key == QLatin1String("transport")) return toInt(transport, 0, 2);
    if (key == QLatin1String("address"))   { address = v.toString().trimmed(); return true; }
    if (key == QLatin1String("args"))      { args = v.toString(); return true; }
    if (key == QLatin1String("midiPort"))  { midiPort = v.toString(); return true; }
    if (key == QLatin1String("midiType"))  { midiType = midiTypeFromKey(v.toString()); return true; }
    if (key == QLatin1String("channel"))   return toInt(channel, 1, 16);
    if (key == QLatin1String("data1"))     return toInt(data1, 0, 127);
    if (key == QLatin1String("data2"))     return toInt(data2, 0, 127);
    if (key == QLatin1String("rawHex"))    { rawHex = v.toString(); return true; }
    if (key == QLatin1String("deviceId"))  return toInt(deviceId, 0, 0x7F);
    if (key == QLatin1String("commandFormat")) return toInt(commandFormat, 0, 0x7F);
    if (key == QLatin1String("command"))   return toInt(command, 0, 0x7F);
    if (key == QLatin1String("qNumber"))   { qNumber = v.toString().trimmed(); return true; }
    if (key == QLatin1String("qList"))     { qList = v.toString().trimmed(); return true; }
    if (key == QLatin1String("cueId")) {
        const QUuid id = QUuid::fromString(v.toString());
        if (id.isNull() && !v.toString().isEmpty()) return false;
        cueId = id;
        return true;
    }
    return false;
}

// ── LightTrigger ─────────────────────────────────────────────────────────────

QJsonObject LightTrigger::toJson() const
{
    QJsonObject o{
        {QStringLiteral("id"),      id.toString(QUuid::WithoutBraces)},
        {QStringLiteral("name"),    name},
        {QStringLiteral("start"),   start},
        {QStringLiteral("enabled"), enabled},
        {QStringLiteral("enter"),   enter.toJson()},
    };
    if (isRange()) {
        o.insert(QStringLiteral("end"), end);
        o.insert(QStringLiteral("exit"), exit.toJson());
    }
    return o;
}

LightTrigger LightTrigger::fromJson(const QJsonObject &o)
{
    LightTrigger t;
    const QUuid id = QUuid::fromString(o.value(QStringLiteral("id")).toString());
    if (!id.isNull()) t.id = id;
    t.name    = o.value(QStringLiteral("name")).toString();
    t.start   = std::max(0.0, o.value(QStringLiteral("start")).toDouble());
    t.end     = o.value(QStringLiteral("end")).toDouble(-1.0);
    t.enabled = o.value(QStringLiteral("enabled")).toBool(true);
    t.enter   = TriggerAction::fromJson(o.value(QStringLiteral("enter")).toObject());
    t.exit    = TriggerAction::fromJson(o.value(QStringLiteral("exit")).toObject());
    return t;
}

QVariant LightTrigger::field(const QString &key) const
{
    if (key == QLatin1String("id"))      return id.toString(QUuid::WithoutBraces);
    if (key == QLatin1String("name"))    return name;
    if (key == QLatin1String("start"))   return start;
    if (key == QLatin1String("end"))     return isRange() ? end : -1.0;
    if (key == QLatin1String("enabled")) return enabled;
    if (key == QLatin1String("enter"))
        return QString::fromUtf8(QJsonDocument(enter.toJson()).toJson(QJsonDocument::Compact));
    if (key == QLatin1String("exit"))
        return QString::fromUtf8(QJsonDocument(exit.toJson()).toJson(QJsonDocument::Compact));
    if (key.startsWith(QLatin1String("enter."))) return enter.field(key.mid(6));
    if (key.startsWith(QLatin1String("exit.")))  return exit.field(key.mid(5));
    return {};
}

bool LightTrigger::setField(const QString &key, const QVariant &v)
{
    if (key == QLatin1String("name"))    { name = v.toString(); return true; }
    if (key == QLatin1String("start")) {
        if (!numberOk(v)) return false;
        start = std::max(0.0, v.toDouble());
        return true;
    }
    if (key == QLatin1String("end")) {
        if (!numberOk(v)) return false;
        end = v.toDouble();
        return true;
    }
    if (key == QLatin1String("enabled")) { enabled = v.toBool(); return true; }
    if (key == QLatin1String("enter") || key == QLatin1String("exit")) {
        QJsonObject o;
        if (v.metaType() == QMetaType::fromType<QJsonObject>()) {
            o = v.toJsonObject();
        } else {
            const auto doc = QJsonDocument::fromJson(v.toString().toUtf8());
            if (!doc.isObject()) return false;
            o = doc.object();
        }
        (key == QLatin1String("enter") ? enter : exit) = TriggerAction::fromJson(o);
        return true;
    }
    if (key.startsWith(QLatin1String("enter."))) return enter.setField(key.mid(6), v);
    if (key.startsWith(QLatin1String("exit.")))  return exit.setField(key.mid(5), v);
    return false;
}

QJsonArray triggersToJson(const LightTriggers &t)
{
    QJsonArray a;
    for (const auto &x : t) a.append(x.toJson());
    return a;
}

LightTriggers triggersFromJson(const QJsonArray &a)
{
    LightTriggers out;
    out.reserve(size_t(a.size()));
    for (const auto &v : a)
        if (v.isObject()) out.push_back(LightTrigger::fromJson(v.toObject()));
    std::stable_sort(out.begin(), out.end(),
                     [](const LightTrigger &x, const LightTrigger &y) { return x.start < y.start; });
    return out;
}

LightTriggers triggersFromVariant(const QVariant &v, bool *ok)
{
    if (ok) *ok = true;
    if (v.metaType() == QMetaType::fromType<QJsonArray>())
        return triggersFromJson(v.toJsonArray());
    if (v.metaType() == QMetaType::fromType<QVariantList>())
        return triggersFromJson(QJsonArray::fromVariantList(v.toList()));
    const QString s = v.toString().trimmed();
    if (s.isEmpty()) return {};
    const auto doc = QJsonDocument::fromJson(s.toUtf8());
    if (!doc.isArray()) {
        if (ok) *ok = false;
        return {};
    }
    return triggersFromJson(doc.array());
}

// ── TriggerTracker ───────────────────────────────────────────────────────────

bool TriggerTracker::isInside(const QUuid &id) const
{
    return std::find(m_inside.begin(), m_inside.end(), id) != m_inside.end();
}

void TriggerTracker::setInside(const QUuid &id, bool in)
{
    const auto it = std::find(m_inside.begin(), m_inside.end(), id);
    if (in && it == m_inside.end()) m_inside.push_back(id);
    if (!in && it != m_inside.end()) m_inside.erase(it);
}

void TriggerTracker::crossed(const LightTriggers &t, double from, double to,
                             std::vector<TriggerEvent> &out)
{
    // Every edge in (from, to], in time order, so a short range crossed in
    // one tick still sends enter before exit.
    std::vector<std::tuple<double, int, bool>> edges;
    for (int i = 0; i < int(t.size()); ++i) {
        const auto &x = t[size_t(i)];
        if (!x.enabled) continue;
        if (x.start > from && x.start <= to) edges.emplace_back(x.start, i, false);
        if (x.isRange() && x.end > from && x.end <= to) edges.emplace_back(x.end, i, true);
    }
    std::stable_sort(edges.begin(), edges.end(),
                     [](const auto &a, const auto &b) { return std::get<0>(a) < std::get<0>(b); });
    for (const auto &[time, i, isExit] : edges) {
        const auto &x = t[size_t(i)];
        if (!x.isRange()) {
            out.push_back({i, false});
        } else if (!isExit && !isInside(x.id)) {
            setInside(x.id, true);
            out.push_back({i, false});
        } else if (isExit && isInside(x.id)) {
            setInside(x.id, false);
            out.push_back({i, true});
        }
    }
}

void TriggerTracker::reconcile(const LightTriggers &t, double pos,
                               std::vector<TriggerEvent> &out)
{
    // Leave the old section before entering the new one.
    for (int i = 0; i < int(t.size()); ++i) {
        const auto &x = t[size_t(i)];
        if (!x.isRange() || !isInside(x.id)) continue;
        if (!x.enabled || pos < x.start || pos >= x.end) {
            setInside(x.id, false);
            if (x.enabled) out.push_back({i, true});
        }
    }
    for (int i = 0; i < int(t.size()); ++i) {
        const auto &x = t[size_t(i)];
        if (!x.enabled || !x.isRange() || isInside(x.id)) continue;
        if (pos >= x.start && pos < x.end) {
            setInside(x.id, true);
            out.push_back({i, false});
        }
    }
}

std::vector<TriggerEvent> TriggerTracker::begin(const LightTriggers &t, double pos)
{
    std::vector<TriggerEvent> out;
    m_inside.clear();
    m_started = true;
    constexpr double kEps = 1e-6;
    crossed(t, pos - kEps, pos, out);   // anything sitting exactly on the start
    reconcile(t, pos, out);             // ranges already under way
    m_pos = pos;
    return out;
}

std::vector<TriggerEvent> TriggerTracker::advance(const LightTriggers &t, double pos,
                                                  double loopStart, double loopEnd,
                                                  double wallElapsed)
{
    if (!m_started) return begin(t, pos);
    std::vector<TriggerEvent> out;
    constexpr double kEps = 1e-6;
    const bool looping = loopEnd > loopStart;
    const double maxStep = std::max(kMaxStep, wallElapsed * 1.25 + 0.1);

    if (pos >= m_pos) {
        if (pos - m_pos <= maxStep) crossed(t, m_pos, pos, out);
        else                        reconcile(t, pos, out);
    } else if (looping && (loopEnd - m_pos) + (pos - loopStart) <= maxStep) {
        crossed(t, m_pos, loopEnd, out);         // the tail we just played
        reconcile(t, loopStart, out);            // back to the top of the loop
        crossed(t, loopStart - kEps, pos, out);  // and the head since then
    } else {
        reconcile(t, pos, out);                  // seek backwards
    }
    m_pos = pos;
    return out;
}

std::vector<TriggerEvent> TriggerTracker::stop(const LightTriggers &t)
{
    std::vector<TriggerEvent> out;
    for (int i = 0; i < int(t.size()); ++i) {
        const auto &x = t[size_t(i)];
        if (x.isRange() && isInside(x.id) && x.enabled) out.push_back({i, true});
    }
    m_inside.clear();
    m_started = false;
    return out;
}

} // namespace quewi::audio
