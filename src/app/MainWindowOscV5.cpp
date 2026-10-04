// Remote API v5 — lighting triggers, video soundtracks / conversion, and the
// soundboard's "send to mic". Split out of MainWindow.cpp, which already
// carries v1–v4. The address scheme is documented in
// docs/osc-control/reference.md (HeliOSC is the main client).
//
//   /quewi/cue/<num>/triggers/list                  → /quewi/reply/cue/triggers <json s>
//   /quewi/cue/<num>/triggers/set   <json-array s>    replace them all (undoable)
//   /quewi/cue/<num>/triggers/add   <start f> [<end f>] [<name s>]
//                                   or <json-object s> → /quewi/reply/cue/triggers/added <id s> <index i>
//   /quewi/cue/<num>/triggers/clear
//   /quewi/cue/<num>/trigger/<ref>/set/<field> <value>  (name, start, end, enabled,
//                                   enter / exit as JSON, enter.<field>, exit.<field>)
//   /quewi/cue/<num>/trigger/<ref>/remove
//   /quewi/cue/<num>/trigger/<ref>/test [<"exit" s>]
//       <ref> = 0-based index, trigger id, or trigger name
//   /quewi/triggers/armed [<T/F | i>]                (no arg: just reply)
//   /quewi/query/triggers/armed                      → /quewi/reply/triggers/armed <T/F>
//   /quewi/query/lightingDesk                        → /quewi/reply/lightingDesk <json s>
//   /quewi/lightingDesk/set <json s>                  merge fields into the desk
//   /quewi/lightingDesk/<field> <value>               type (eos|ma3|ma2msc), host, port,
//                                   ma3Prefix, midiPort, mscDeviceId, eosFaderBank
//   /quewi/cue/<num>/convert                          video ↔ audio cue (undoable)
//   /quewi/soundboard/mic/{device,input} <s>          name or id; "" = off
//   /quewi/soundboard/mic/{gain,inputGain} <dB f>
//   /quewi/soundboard/mic/{monitor,passthrough} <T/F | i>
//   /quewi/query/soundboard/mic                      → /quewi/reply/soundboard/mic <json s>
// Notifications:
//   /quewi/notify/trigger/fired <cueId s> <num d> <triggerId s> <name s> <"enter"|"exit" s>
//   /quewi/notify/triggers/armed <T/F>

#include "MainWindow.h"

#include "GoEngine.h"
#include "audio/AudioCue.h"
#include "audio/DeskCommands.h"
#include "core/LightingDesk.h"
#include "core/CueList.h"
#include "core/UndoCommands.h"
#include "core/Workspace.h"
#include "osc/OscEngine.h"
#include "ui/MicRouting.h"
#include "video/VideoCue.h"

#include <QAudioDevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMediaDevices>
#include <QStatusBar>
#include <QUndoStack>
#include <QUrl>

#include <cmath>
#include <optional>

namespace quewi {

namespace {

bool sameCueNumber(double a, double b) { return std::abs(a - b) < 1e-6; }

// The sound a cue's triggers live on: the AudioCue itself, or a video cue's
// soundtrack (whether or not it plays — silent videos still run triggers).
audio::AudioCue *triggerSoundOf(cues::Cue *c)
{
    if (auto *vc = qobject_cast<video::VideoCue *>(c)) return vc->sound();
    return qobject_cast<audio::AudioCue *>(c);
}

QString triggerFieldOf(cues::Cue *c)
{
    return qobject_cast<video::VideoCue *>(c) ? QStringLiteral("sound.lightTriggers")
                                              : QStringLiteral("lightTriggers");
}

QString compact(const QJsonValue &v)
{
    return QString::fromUtf8(v.isArray() ? QJsonDocument(v.toArray()).toJson(QJsonDocument::Compact)
                                         : QJsonDocument(v.toObject()).toJson(QJsonDocument::Compact));
}

std::optional<bool> boolArg(const osc::Message &m)
{
    if (m.args.empty()) return std::nullopt;
    const auto &a = m.args.front();
    if (a.tag == osc::Argument::Tag::True)  return true;
    if (a.tag == osc::Argument::Tag::False) return false;
    if (const auto n = osc::toNumber(a)) return *n != 0.0;
    return std::nullopt;
}

std::optional<QString> stringArg(const osc::Message &m, size_t i = 0)
{
    if (m.args.size() <= i || m.args[i].tag != osc::Argument::Tag::String) return std::nullopt;
    return std::get<QString>(m.args[i].value);
}

QVariant variantArg(const osc::Argument &a)
{
    switch (a.tag) {
    case osc::Argument::Tag::Int32:   return std::get<qint32>(a.value);
    case osc::Argument::Tag::Int64:   return qlonglong(std::get<qint64>(a.value));
    case osc::Argument::Tag::Float32: return std::get<float>(a.value);
    case osc::Argument::Tag::Double:  return std::get<double>(a.value);
    case osc::Argument::Tag::String:  return std::get<QString>(a.value);
    case osc::Argument::Tag::True:    return true;
    case osc::Argument::Tag::False:   return false;
    default:                          return {};
    }
}

// A 0-based index, a trigger id, or a name (case-insensitive).
int findTrigger(const audio::LightTriggers &t, const QString &ref)
{
    bool isIndex = false;
    const int idx = ref.toInt(&isIndex);
    if (isIndex) return (idx >= 0 && idx < int(t.size())) ? idx : -1;
    const QUuid id = QUuid::fromString(ref);
    for (int i = 0; i < int(t.size()); ++i) {
        const auto &x = t[size_t(i)];
        if ((!id.isNull() && x.id == id)
            || x.name.compare(ref, Qt::CaseInsensitive) == 0) return i;
    }
    return -1;
}

// An audio device by id or by name (exact, then partial). Empty = none.
QByteArray findAudioDevice(bool output, const QString &query)
{
    const QString q = query.trimmed();
    if (q.isEmpty()) return {};
    const auto devices = output ? QMediaDevices::audioOutputs() : QMediaDevices::audioInputs();
    for (const auto &d : devices)
        if (QString::fromLatin1(d.id()) == q) return d.id();
    for (const auto &d : devices)
        if (d.description().compare(q, Qt::CaseInsensitive) == 0) return d.id();
    for (const auto &d : devices)
        if (d.description().contains(q, Qt::CaseInsensitive)) return d.id();
    return {};
}

QString deviceName(bool output, const QByteArray &id)
{
    if (id.isEmpty()) return {};
    const auto devices = output ? QMediaDevices::audioOutputs() : QMediaDevices::audioInputs();
    for (const auto &d : devices)
        if (d.id() == id) return d.description();
    return QString::fromLatin1(id);
}

} // namespace

cues::Cue *MainWindow::oscCueByNumber(double num) const
{
    auto *list = activeOscList();
    if (!list) return nullptr;
    for (int r = 0; r < list->cueCount(); ++r)
        if (auto *c = list->cueAt(r); c && sameCueNumber(c->number(), num)) return c;
    return nullptr;
}

QJsonObject MainWindow::triggersReplyJson(cues::Cue *c) const
{
    auto *sound = triggerSoundOf(c);
    QJsonArray arr;
    int i = 0;
    for (const auto &t : sound->lightTriggers()) {
        QJsonObject o = t.toJson();
        o.insert(QStringLiteral("index"), i++);
        o.insert(QStringLiteral("range"), t.isRange());
        // Each action gets a readable summary, and "fire cue" targets their
        // number, so a remote can show them without decoding.
        for (const auto &key : {QStringLiteral("enter"), QStringLiteral("exit")}) {
            if (!o.contains(key)) continue;
            const auto &a = key == QLatin1String("enter") ? t.enter : t.exit;
            QJsonObject act = o.value(key).toObject();
            act.insert(QStringLiteral("summary"), a.summary());
            if (a.kind == audio::TriggerAction::Kind::FireCue)
                for (const auto &list : m_workspace->cueLists())
                    for (int r = 0; r < list->cueCount(); ++r)
                        if (auto *x = list->cueAt(r); x && x->id() == a.cueId)
                            act.insert(QStringLiteral("cueNumber"), x->number());
            o.insert(key, act);
        }
        arr.append(o);
    }
    return QJsonObject{
        {QStringLiteral("cue"),      c->number()},
        {QStringLiteral("id"),       c->id().toString()},   // braced, like every other cue id
        {QStringLiteral("type"),     c->typeKey()},
        {QStringLiteral("armed"),    m_goEngine && m_goEngine->triggersArmed()},
        {QStringLiteral("triggers"), arr},
    };
}

void MainWindow::commitTriggers(cues::Cue *c, const audio::LightTriggers &next)
{
    const QString field = triggerFieldOf(c);
    const QVariant old = c->field(field);
    const QVariant now = audio::triggersToJson(next);
    if (old == now) return;
    // Wrapped so it never merges: EditCueFieldCommand merges every
    // consecutive edit of the same field (right for a slider drag), which
    // would fold a remote's add, edits and remove into one undo step.
    auto *step = new QUndoCommand(tr("Edit lighting triggers"));
    new core::EditCueFieldCommand(c, field, old, now, step);
    m_workspace->undoStack()->push(step);
}

void MainWindow::registerOscApiV5()
{
    auto sub = [this](const QString &pattern, auto &&fn) {
        m_oscEngine->subscribe(pattern, std::forward<decltype(fn)>(fn));
    };
    // Who sent the packet being handled — captured before queuing, since by
    // the time a queued lambda runs another peer may have sent something.
    auto sender = [this] {
        osc::Destination d;
        d.host = m_oscEngine->lastSenderHost();
        d.port = m_oscEngine->lastSenderPort();
        d.transport = osc::Destination::Udp;
        return d;
    };
    auto reply = [this](const osc::Destination &to, const QString &address,
                        std::vector<osc::Argument> args) {
        osc::Message msg;
        msg.address = address;
        msg.args = std::move(args);
        m_oscEngine->send(to, msg);
    };
    // Parse "/quewi/cue/<num>/…" → cue number, or nullopt.
    auto cueNum = [](const osc::Message &m) -> std::optional<double> {
        const auto parts = m.address.split(QChar('/'), Qt::SkipEmptyParts);
        bool ok = false;
        const double n = parts.value(2).toDouble(&ok);
        return ok ? std::optional<double>(n) : std::nullopt;
    };

    // ── Lighting triggers ───────────────────────────────────────────────
    sub("/quewi/cue/*/triggers/list", [=](const osc::Message &m) {
        const auto num = cueNum(m);
        if (!num) return;
        const auto to = sender();
        QMetaObject::invokeMethod(this, [=] {
            auto *c = oscCueByNumber(*num);
            if (!c || !triggerSoundOf(c)) return;
            reply(to, QStringLiteral("/quewi/reply/cue/triggers"),
                  { osc::Argument::s(compact(triggersReplyJson(c))) });
        }, Qt::QueuedConnection);
    });

    sub("/quewi/cue/*/triggers/set", [=](const osc::Message &m) {
        const auto num = cueNum(m);
        const auto json = stringArg(m);
        if (!num || !json) return;
        QMetaObject::invokeMethod(this, [=] {
            auto *c = oscCueByNumber(*num);
            if (!c || !triggerSoundOf(c)) return;
            bool ok = false;
            const auto next = audio::triggersFromVariant(*json, &ok);
            if (ok) commitTriggers(c, next);
        }, Qt::QueuedConnection);
    });

    sub("/quewi/cue/*/triggers/clear", [=](const osc::Message &m) {
        const auto num = cueNum(m);
        if (!num) return;
        QMetaObject::invokeMethod(this, [=] {
            if (auto *c = oscCueByNumber(*num); c && triggerSoundOf(c))
                commitTriggers(c, {});
        }, Qt::QueuedConnection);
    });

    sub("/quewi/cue/*/triggers/add", [=](const osc::Message &m) {
        const auto num = cueNum(m);
        if (!num || m.args.empty()) return;
        audio::LightTrigger t;
        if (const auto json = stringArg(m)) {
            const auto doc = QJsonDocument::fromJson(json->toUtf8());
            if (!doc.isObject()) return;
            QJsonObject o = doc.object();
            o.remove(QStringLiteral("id"));               // always a fresh one
            t = audio::LightTrigger::fromJson(o);
        } else {
            const auto start = osc::toNumber(m.args[0]);
            if (!start) return;
            t.start = std::max(0.0, *start);
            if (m.args.size() > 1)
                if (const auto end = osc::toNumber(m.args[1])) t.end = *end;
            for (size_t i = 1; i < m.args.size(); ++i)
                if (const auto name = stringArg(m, i)) t.name = *name;
        }
        const auto to = sender();
        QMetaObject::invokeMethod(this, [=] {
            auto *c = oscCueByNumber(*num);
            auto *sound = c ? triggerSoundOf(c) : nullptr;
            if (!sound) return;
            auto next = sound->lightTriggers();
            next.push_back(t);
            commitTriggers(c, next);
            // Stored sorted by start — report where it landed.
            const int index = findTrigger(sound->lightTriggers(), t.id.toString());
            reply(to, QStringLiteral("/quewi/reply/cue/triggers/added"),
                  { osc::Argument::s(t.id.toString(QUuid::WithoutBraces)),
                    osc::Argument::i(index) });
        }, Qt::QueuedConnection);
    });

    sub("/quewi/cue/*/trigger/*/set/*", [=](const osc::Message &m) {
        const auto parts = m.address.split(QChar('/'), Qt::SkipEmptyParts);
        const auto num = cueNum(m);
        if (!num || parts.size() < 7 || m.args.empty()) return;
        const QString ref = parts.value(4), field = parts.value(6);
        const QVariant v = variantArg(m.args.front());
        if (!v.isValid()) return;
        QMetaObject::invokeMethod(this, [=] {
            auto *c = oscCueByNumber(*num);
            auto *sound = c ? triggerSoundOf(c) : nullptr;
            if (!sound) return;
            auto next = sound->lightTriggers();
            const int i = findTrigger(next, ref);
            if (i < 0 || !next[size_t(i)].setField(field, v)) return;
            commitTriggers(c, next);
        }, Qt::QueuedConnection);
    });

    sub("/quewi/cue/*/trigger/*/remove", [=](const osc::Message &m) {
        const auto parts = m.address.split(QChar('/'), Qt::SkipEmptyParts);
        const auto num = cueNum(m);
        if (!num || parts.size() < 6) return;
        const QString ref = parts.value(4);
        QMetaObject::invokeMethod(this, [=] {
            auto *c = oscCueByNumber(*num);
            auto *sound = c ? triggerSoundOf(c) : nullptr;
            if (!sound) return;
            auto next = sound->lightTriggers();
            const int i = findTrigger(next, ref);
            if (i < 0) return;
            next.erase(next.begin() + i);
            commitTriggers(c, next);
        }, Qt::QueuedConnection);
    });

    // Groups: every trigger whose group is <name> at once. Spaces and other
    // characters an OSC address can't hold are percent-encoded (%20).
    auto groupOf = [](const QStringList &parts) {
        return QUrl::fromPercentEncoding(parts.value(5).toUtf8()).trimmed();
    };
    sub("/quewi/cue/*/triggers/group/*/set/*", [=](const osc::Message &m) {
        const auto parts = m.address.split(QChar('/'), Qt::SkipEmptyParts);
        const auto num = cueNum(m);
        if (!num || parts.size() < 8 || m.args.empty()) return;
        const QString group = groupOf(parts), field = parts.value(7);
        const QVariant v = variantArg(m.args.front());
        if (group.isEmpty() || !v.isValid()) return;
        QMetaObject::invokeMethod(this, [=] {
            auto *c = oscCueByNumber(*num);
            auto *sound = c ? triggerSoundOf(c) : nullptr;
            if (!sound) return;
            auto next = sound->lightTriggers();
            bool any = false;
            for (auto &t : next)
                if (t.group == group) any = t.setField(field, v) || any;
            if (any) commitTriggers(c, next);
        }, Qt::QueuedConnection);
    });
    sub("/quewi/cue/*/triggers/group/*/shift", [=](const osc::Message &m) {
        const auto parts = m.address.split(QChar('/'), Qt::SkipEmptyParts);
        const auto num = cueNum(m);
        if (!num || parts.size() < 7 || m.args.empty()) return;
        const QString group = groupOf(parts);
        const auto by = osc::toNumber(m.args.front());
        if (group.isEmpty() || !by) return;
        QMetaObject::invokeMethod(this, [=] {
            auto *c = oscCueByNumber(*num);
            auto *sound = c ? triggerSoundOf(c) : nullptr;
            if (!sound) return;
            auto next = sound->lightTriggers();
            double first = 1e300;
            for (const auto &t : next)
                if (t.group == group) first = std::min(first, t.start);
            if (first > 1e299) return;
            const double d = std::max(*by, -first);       // nothing before the song starts
            for (auto &t : next) {
                if (t.group != group) continue;
                const bool range = t.isRange();
                t.start += d;
                if (range) t.end += d;
            }
            commitTriggers(c, next);
        }, Qt::QueuedConnection);
    });
    sub("/quewi/cue/*/triggers/group/*/remove", [=](const osc::Message &m) {
        const auto parts = m.address.split(QChar('/'), Qt::SkipEmptyParts);
        const auto num = cueNum(m);
        if (!num || parts.size() < 7) return;
        const QString group = groupOf(parts);
        if (group.isEmpty()) return;
        QMetaObject::invokeMethod(this, [=] {
            auto *c = oscCueByNumber(*num);
            auto *sound = c ? triggerSoundOf(c) : nullptr;
            if (!sound) return;
            auto next = sound->lightTriggers();
            const auto n = next.size();
            next.erase(std::remove_if(next.begin(), next.end(),
                                      [&](const audio::LightTrigger &t) { return t.group == group; }),
                       next.end());
            if (next.size() != n) commitTriggers(c, next);
        }, Qt::QueuedConnection);
    });

    sub("/quewi/cue/*/trigger/*/test", [=](const osc::Message &m) {
        const auto parts = m.address.split(QChar('/'), Qt::SkipEmptyParts);
        const auto num = cueNum(m);
        if (!num || parts.size() < 6) return;
        const QString ref = parts.value(4);
        const bool exit = stringArg(m).value_or(QString()).compare(QLatin1String("exit"),
                                                                  Qt::CaseInsensitive) == 0;
        QMetaObject::invokeMethod(this, [=] {
            auto *c = oscCueByNumber(*num);
            auto *sound = c ? triggerSoundOf(c) : nullptr;
            if (!sound || !m_goEngine) return;
            const auto &t = sound->lightTriggers();
            const int i = findTrigger(t, ref);
            if (i < 0) return;
            m_goEngine->sendTriggerAction(exit ? t[size_t(i)].exit : t[size_t(i)].enter, c);
        }, Qt::QueuedConnection);
    });

    sub("/quewi/triggers/armed", [=](const osc::Message &m) {
        const auto on = boolArg(m);
        const auto to = sender();
        QMetaObject::invokeMethod(this, [=] {
            if (!m_goEngine) return;
            if (on) m_goEngine->setTriggersArmed(*on);
            reply(to, QStringLiteral("/quewi/reply/triggers/armed"),
                  { m_goEngine->triggersArmed() ? osc::Argument::T() : osc::Argument::F() });
        }, Qt::QueuedConnection);
    });
    sub("/quewi/query/triggers/armed", [=](const osc::Message &) {
        if (!m_goEngine) return;
        reply(sender(), QStringLiteral("/quewi/reply/triggers/armed"),
              { m_goEngine->triggersArmed() ? osc::Argument::T() : osc::Argument::F() });
    });

    // Live notifications. GoEngine lives as long as we do, so these are
    // wired once (not per workspace).
    connect(m_goEngine.get(), &GoEngine::triggerFired, this,
            [this](cues::Cue *owner, const QUuid &id, const QString &name, bool exit) {
                pushOscNotify(QStringLiteral("/quewi/notify/trigger/fired"),
                    { osc::Argument::s(owner ? owner->id().toString() : QString()),
                      osc::Argument::d(owner ? owner->number() : 0.0),
                      osc::Argument::s(id.toString(QUuid::WithoutBraces)),
                      osc::Argument::s(name),
                      osc::Argument::s(exit ? QStringLiteral("exit") : QStringLiteral("enter")) });
            });
    connect(m_goEngine.get(), &GoEngine::triggersArmedChanged, this, [this](bool on) {
        pushOscNotify(QStringLiteral("/quewi/notify/triggers/armed"),
                      { on ? osc::Argument::T() : osc::Argument::F() });
    });

    // ── Lighting desk (Preferences → Lighting) ──────────────────────────
    auto deskJson = [] {
        const auto desk = core::LightingDesk::load();
        QJsonObject o = desk.toJson();
        o.insert(QStringLiteral("name"), desk.typeName());
        o.insert(QStringLiteral("summary"), desk.summary());
        QJsonArray can;
        for (const auto d : {audio::TriggerAction::DeskDo::Go, audio::TriggerAction::DeskDo::GoList,
                             audio::TriggerAction::DeskDo::Stop,
                             audio::TriggerAction::DeskDo::Back, audio::TriggerAction::DeskDo::GoToCue,
                             audio::TriggerAction::DeskDo::SubLevel, audio::TriggerAction::DeskDo::SubBump,
                             audio::TriggerAction::DeskDo::FaderLevel, audio::TriggerAction::DeskDo::FaderBump,
                             audio::TriggerAction::DeskDo::Macro, audio::TriggerAction::DeskDo::Command})
            if (audio::deskSupports(desk.type, d))
                can.append(QJsonObject{{QStringLiteral("do"), audio::TriggerAction::deskDoKey(d)},
                                       {QStringLiteral("name"), audio::TriggerAction::deskDoName(d)}});
        o.insert(QStringLiteral("actions"), can);   // what simple-mode triggers can do here
        return o;
    };
    sub("/quewi/query/lightingDesk", [=](const osc::Message &) {
        reply(sender(), QStringLiteral("/quewi/reply/lightingDesk"),
              { osc::Argument::s(compact(deskJson())) });
    });
    sub("/quewi/lightingDesk/set", [=](const osc::Message &m) {
        const auto json = stringArg(m);
        if (!json) return;
        const auto doc = QJsonDocument::fromJson(json->toUtf8());
        if (!doc.isObject()) return;
        QMetaObject::invokeMethod(this, [=] {
            QJsonObject o = core::LightingDesk::load().toJson();
            const auto in = doc.object();
            for (auto it = in.begin(); it != in.end(); ++it) o.insert(it.key(), it.value());
            core::LightingDesk::fromJson(o).save();
        }, Qt::QueuedConnection);
    });
    sub("/quewi/lightingDesk/*", [=](const osc::Message &m) {
        const QString field = m.address.section(QLatin1Char('/'), -1);
        if (field == QLatin1String("set") || m.args.empty()) return;
        const QVariant v = variantArg(m.args.front());
        if (!v.isValid()) return;
        QMetaObject::invokeMethod(this, [=] {
            QJsonObject o = core::LightingDesk::load().toJson();
            if (!o.contains(field)) return;                      // unknown field
            o.insert(field, QJsonValue::fromVariant(v));
            core::LightingDesk::fromJson(o).save();
        }, Qt::QueuedConnection);
    });

    // ── Video ↔ audio conversion ────────────────────────────────────────
    sub("/quewi/cue/*/convert", [=](const osc::Message &m) {
        const auto num = cueNum(m);
        if (!num) return;
        QMetaObject::invokeMethod(this, [=] {
            if (auto *c = oscCueByNumber(*num)) convertCue(c);
        }, Qt::QueuedConnection);
    });

    // ── Soundboard → mic ────────────────────────────────────────────────
    auto *mic = ui::MicRouting::instance();
    sub("/quewi/soundboard/mic/device", [=](const osc::Message &m) {
        const auto q = stringArg(m);
        if (!q) return;
        QMetaObject::invokeMethod(this, [=] {
            const QByteArray id = findAudioDevice(true, *q);
            if (!q->trimmed().isEmpty() && id.isEmpty()) {
                statusBar()->showMessage(tr("Send to mic: no output device matches \"%1\"").arg(*q), 4000);
                return;
            }
            mic->setDeviceId(id);
        }, Qt::QueuedConnection);
    });
    sub("/quewi/soundboard/mic/input", [=](const osc::Message &m) {
        const auto q = stringArg(m);
        if (!q) return;
        QMetaObject::invokeMethod(this, [=] {
            const QByteArray id = findAudioDevice(false, *q);
            if (!q->trimmed().isEmpty() && id.isEmpty()) {
                statusBar()->showMessage(tr("Send to mic: no input device matches \"%1\"").arg(*q), 4000);
                return;
            }
            mic->setInputDeviceId(id);
        }, Qt::QueuedConnection);
    });
    sub("/quewi/soundboard/mic/gain", [=](const osc::Message &m) {
        if (const auto db = osc::firstNumber(m))
            QMetaObject::invokeMethod(this, [=] { mic->setSfxGainDb(*db); }, Qt::QueuedConnection);
    });
    sub("/quewi/soundboard/mic/inputGain", [=](const osc::Message &m) {
        if (const auto db = osc::firstNumber(m))
            QMetaObject::invokeMethod(this, [=] { mic->setInputGainDb(*db); }, Qt::QueuedConnection);
    });
    sub("/quewi/soundboard/mic/monitor", [=](const osc::Message &m) {
        if (const auto on = boolArg(m))
            QMetaObject::invokeMethod(this, [=] { mic->setMonitor(*on); }, Qt::QueuedConnection);
    });
    sub("/quewi/soundboard/mic/passthrough", [=](const osc::Message &m) {
        if (const auto on = boolArg(m))
            QMetaObject::invokeMethod(this, [=] { mic->setPassthrough(*on); }, Qt::QueuedConnection);
    });
    sub("/quewi/query/soundboard/mic", [=](const osc::Message &) {
        auto devices = [](bool output) {
            QJsonArray a;
            for (const auto &d : output ? QMediaDevices::audioOutputs() : QMediaDevices::audioInputs())
                a.append(QJsonObject{
                    {QStringLiteral("id"),      QString::fromLatin1(d.id())},
                    {QStringLiteral("name"),    d.description()},
                    {QStringLiteral("virtual"), ui::MicRouting::looksVirtual(d.description())},
                });
            return a;
        };
        const QJsonObject o{
            {QStringLiteral("enabled"),     mic->enabled()},
            {QStringLiteral("device"),      QString::fromLatin1(mic->deviceId())},
            {QStringLiteral("deviceName"),  deviceName(true, mic->deviceId())},
            {QStringLiteral("monitor"),     mic->monitor()},
            {QStringLiteral("gainDb"),      mic->sfxGainDb()},
            {QStringLiteral("passthrough"), mic->passthrough()},
            {QStringLiteral("input"),       QString::fromLatin1(mic->inputDeviceId())},
            {QStringLiteral("inputName"),   deviceName(false, mic->inputDeviceId())},
            {QStringLiteral("inputGainDb"), mic->inputGainDb()},
            {QStringLiteral("outputs"),     devices(true)},
            {QStringLiteral("inputs"),      devices(false)},
        };
        reply(sender(), QStringLiteral("/quewi/reply/soundboard/mic"), { osc::Argument::s(compact(o)) });
    });
    connect(mic, &ui::MicRouting::changed, this, [this] {
        pushOscNotify(QStringLiteral("/quewi/notify/soundboard/mic/changed"));
    });
}

} // namespace quewi
