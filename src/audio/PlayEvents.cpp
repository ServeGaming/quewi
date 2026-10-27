#include "audio/PlayEvents.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <cmath>

namespace quewi::audio {

namespace {
struct KindInfo { PlayEvent::Kind k; const char *key; };
const KindInfo kKinds[] = {
    {PlayEvent::Kind::Vamp, "vamp"}, {PlayEvent::Kind::Hold, "hold"},
    {PlayEvent::Kind::Skip, "skip"}, {PlayEvent::Kind::Marker, "marker"},
};
} // namespace

bool PlayEvent::operator==(const PlayEvent &o) const
{
    return id == o.id && kind == o.kind && name == o.name && start == o.start
        && end == o.end && count == o.count && exit == o.exit;
}

QString PlayEvent::kindKey(Kind k)
{
    for (const auto &i : kKinds) if (i.k == k) return QString::fromLatin1(i.key);
    return QStringLiteral("vamp");
}

PlayEvent::Kind PlayEvent::kindFromKey(const QString &k, bool *ok)
{
    for (const auto &i : kKinds)
        if (k.compare(QLatin1String(i.key), Qt::CaseInsensitive) == 0) {
            if (ok) *ok = true;
            return i.k;
        }
    // Stage|Tracks / musicians' words for the same things.
    if (k.compare(QLatin1String("caesura"), Qt::CaseInsensitive) == 0
        || k.compare(QLatin1String("fermata"), Qt::CaseInsensitive) == 0) {
        if (ok) *ok = true;
        return Kind::Hold;
    }
    if (k.compare(QLatin1String("repeat"), Qt::CaseInsensitive) == 0) {
        if (ok) *ok = true;
        return Kind::Vamp;
    }
    if (ok) *ok = false;
    return Kind::Vamp;
}

QString PlayEvent::exitKey(Exit e)
{
    switch (e) {
    case Exit::NextBar: return QStringLiteral("nextBar");
    case Exit::Now:     return QStringLiteral("now");
    case Exit::EndOfPass: break;
    }
    return QStringLiteral("endOfPass");
}

PlayEvent::Exit PlayEvent::exitFromKey(const QString &k)
{
    if (k.compare(QLatin1String("nextBar"), Qt::CaseInsensitive) == 0) return Exit::NextBar;
    if (k.compare(QLatin1String("now"), Qt::CaseInsensitive) == 0)     return Exit::Now;
    return Exit::EndOfPass;
}

QString PlayEvent::kindName() const
{
    switch (kind) {
    case Kind::Vamp:
        return count > 0 ? QCoreApplication::translate("PlayEvent", "Repeat ×%1").arg(count)
                         : QCoreApplication::translate("PlayEvent", "Vamp");
    case Kind::Hold:   return QCoreApplication::translate("PlayEvent", "Hold");
    case Kind::Skip:   return QCoreApplication::translate("PlayEvent", "Skip");
    case Kind::Marker: return QCoreApplication::translate("PlayEvent", "Marker");
    }
    return {};
}

QJsonObject PlayEvent::toJson() const
{
    QJsonObject o{
        {QStringLiteral("id"),    id.toString(QUuid::WithoutBraces)},
        {QStringLiteral("kind"),  kindKey(kind)},
        {QStringLiteral("start"), start},
    };
    if (!name.isEmpty()) o.insert(QStringLiteral("name"), name);
    if (isRange()) o.insert(QStringLiteral("end"), end);
    if (kind == Kind::Vamp) {
        if (count > 0) o.insert(QStringLiteral("count"), count);
        if (exit != Exit::EndOfPass) o.insert(QStringLiteral("exit"), exitKey(exit));
    }
    return o;
}

PlayEvent PlayEvent::fromJson(const QJsonObject &o, bool *ok)
{
    PlayEvent e;
    bool kindOk = true;
    e.kind  = kindFromKey(o.value(QStringLiteral("kind")).toString(QStringLiteral("vamp")), &kindOk);
    e.id    = QUuid::fromString(o.value(QStringLiteral("id")).toString());
    e.name  = o.value(QStringLiteral("name")).toString();
    e.start = std::max(0.0, o.value(QStringLiteral("start")).toDouble());
    e.end   = e.isRange() ? o.value(QStringLiteral("end")).toDouble(e.start) : e.start;
    if (e.kind == Kind::Vamp) {
        e.count = std::clamp(o.value(QStringLiteral("count")).toInt(0), 0, 999);
        if (e.count == 1) e.count = 2;       // "play it once" isn't a repeat
        e.exit  = exitFromKey(o.value(QStringLiteral("exit")).toString());
    }
    if (ok) *ok = kindOk && std::isfinite(e.start) && std::isfinite(e.end);
    return e;
}

PlayEvents normalizePlayEvents(PlayEvents events)
{
    for (auto &e : events) {
        if (e.id.isNull()) e.id = QUuid::createUuid();
        if (!e.isRange()) e.end = e.start;
        else if (e.end < e.start) std::swap(e.start, e.end);
        if (e.kind != PlayEvent::Kind::Vamp) { e.count = 0; e.exit = PlayEvent::Exit::EndOfPass; }
    }
    events.erase(std::remove_if(events.begin(), events.end(), [](const PlayEvent &e) {
        return e.isRange() && e.length() < kMinPlayEventLength;
    }), events.end());
    std::stable_sort(events.begin(), events.end(),
                     [](const PlayEvent &a, const PlayEvent &b) { return a.start < b.start; });
    // Vamps can't overlap: playback can only be looping one at a time.
    PlayEvents out;
    double vampEnd = -1.0;
    for (auto &e : events) {
        if (e.kind == PlayEvent::Kind::Vamp) {
            if (e.start < vampEnd) continue;
            vampEnd = e.end;
        }
        out.push_back(std::move(e));
    }
    return out;
}

QJsonArray playEventsToJson(const PlayEvents &events)
{
    QJsonArray a;
    for (const auto &e : events) a.append(e.toJson());
    return a;
}

PlayEvents playEventsFromVariant(const QVariant &v, bool *ok)
{
    QJsonArray arr;
    bool good = true;
    if (v.typeId() == QMetaType::QJsonArray) {
        arr = v.toJsonArray();
    } else if (v.typeId() == QMetaType::QString) {
        const auto doc = QJsonDocument::fromJson(v.toString().toUtf8());
        if (doc.isArray()) arr = doc.array();
        else good = v.toString().trimmed().isEmpty();
    } else if (v.canConvert<QVariantList>()) {
        arr = QJsonArray::fromVariantList(v.toList());
    } else if (v.isValid()) {
        good = false;
    }
    PlayEvents out;
    for (const auto &x : arr) {
        if (!x.isObject()) { good = false; continue; }
        bool one = true;
        auto e = PlayEvent::fromJson(x.toObject(), &one);
        if (!one) { good = false; continue; }
        out.push_back(std::move(e));
    }
    if (ok) *ok = good;
    return normalizePlayEvents(std::move(out));
}

const PlayEvent *playEventAt(const PlayEvents &events, double seconds, PlayEvent::Kind kind)
{
    for (const auto &e : events)
        if (e.kind == kind && e.isRange() && seconds >= e.start && seconds < e.end) return &e;
    return nullptr;
}

} // namespace quewi::audio
