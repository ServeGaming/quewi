#include "core/MatrixModel.h"

#include <QHash>
#include <QJsonArray>
#include <QSet>

#include <algorithm>
#include <climits>

namespace quewi::core::matrix {

namespace {

QString modeKey(ManualPlacement::Mode m)
{
    switch (m) {
    case ManualPlacement::Mode::With:  return QStringLiteral("with");
    case ManualPlacement::Mode::After: return QStringLiteral("after");
    case ManualPlacement::Mode::Start: return QStringLiteral("start");
    }
    return QStringLiteral("with");
}

ManualPlacement::Mode modeFromKey(const QString &k)
{
    if (k == QLatin1String("after")) return ManualPlacement::Mode::After;
    if (k == QLatin1String("start")) return ManualPlacement::Mode::Start;
    return ManualPlacement::Mode::With;
}

void putIfSet(QJsonObject &o, const char *key, const QString &v)
{
    if (!v.isEmpty()) o.insert(QLatin1String(key), v);
}

void putIfSet(QJsonObject &o, const char *key, double v)
{
    if (v >= 0.0) o.insert(QLatin1String(key), v);
}

double secondsOr(const QJsonObject &o, const char *key)
{
    const auto v = o.value(QLatin1String(key));
    return v.isDouble() ? v.toDouble() : -1.0;
}

QString cueKey(const QString &list, const QString &number)
{
    return normalNumber(list) + QLatin1Char('/') + normalNumber(number);
}

} // namespace

QString normalNumber(const QString &n)
{
    QString s = n.trimmed();
    bool ok = false;
    s.toDouble(&ok);
    if (!ok) return s;
    if (s.contains(QLatin1Char('.'))) {
        while (s.endsWith(QLatin1Char('0'))) s.chop(1);
        if (s.endsWith(QLatin1Char('.'))) s.chop(1);
    }
    while (s.size() > 1 && s.startsWith(QLatin1Char('0')) && s.at(1) != QLatin1Char('.')) s.remove(0, 1);
    return s;
}

// ── DeskCue ─────────────────────────────────────────────────────────────

QJsonObject DeskCue::toJson() const
{
    QJsonObject o{{QStringLiteral("list"), list}, {QStringLiteral("number"), number}};
    if (part > 0) o.insert(QStringLiteral("part"), part);
    putIfSet(o, "uid", uid);
    putIfSet(o, "label", label);
    putIfSet(o, "notes", notes);
    putIfSet(o, "scene", scene);
    if (sceneEnd) o.insert(QStringLiteral("sceneEnd"), true);
    putIfSet(o, "up", upSeconds);
    putIfSet(o, "down", downSeconds);
    putIfSet(o, "follow", followSeconds);
    putIfSet(o, "hang", hangSeconds);
    if (partCount > 0) o.insert(QStringLiteral("parts"), partCount);
    putIfSet(o, "upDelay", upDelaySeconds);
    putIfSet(o, "focus", focusSeconds);
    putIfSet(o, "colour", colourSeconds);
    putIfSet(o, "beam", beamSeconds);
    putIfSet(o, "link", link);
    if (loop >= 0) o.insert(QStringLiteral("loop"), loop);
    putIfSet(o, "mark", mark);
    putIfSet(o, "block", block);
    putIfSet(o, "assert", assertFlag);
    if (allFade) o.insert(QStringLiteral("allFade"), true);
    if (preheat) o.insert(QStringLiteral("preheat"), true);
    if (!curve.isEmpty() && curve != QLatin1String("0")) o.insert(QStringLiteral("curve"), curve);
    if (rate != 100) o.insert(QStringLiteral("rate"), rate);
    putIfSet(o, "timecode", timecode);
    if (!effects.isEmpty()) o.insert(QStringLiteral("effects"), QJsonArray::fromStringList(effects));
    if (!actions.isEmpty()) o.insert(QStringLiteral("actions"), QJsonArray::fromStringList(actions));
    return o;
}

DeskCue DeskCue::fromJson(const QJsonObject &o)
{
    DeskCue c;
    c.list = o.value(QStringLiteral("list")).toString();
    c.number = o.value(QStringLiteral("number")).toString();
    c.part = o.value(QStringLiteral("part")).toInt();
    c.uid = o.value(QStringLiteral("uid")).toString();
    c.label = o.value(QStringLiteral("label")).toString();
    c.notes = o.value(QStringLiteral("notes")).toString();
    c.scene = o.value(QStringLiteral("scene")).toString();
    c.sceneEnd = o.value(QStringLiteral("sceneEnd")).toBool();
    c.upSeconds = secondsOr(o, "up");
    c.downSeconds = secondsOr(o, "down");
    c.followSeconds = secondsOr(o, "follow");
    c.hangSeconds = secondsOr(o, "hang");
    c.partCount = o.value(QStringLiteral("parts")).toInt();
    c.upDelaySeconds = secondsOr(o, "upDelay");
    c.focusSeconds = secondsOr(o, "focus");
    c.colourSeconds = secondsOr(o, "colour");
    c.beamSeconds = secondsOr(o, "beam");
    c.link = o.value(QStringLiteral("link")).toString();
    c.loop = o.value(QStringLiteral("loop")).toInt(-1);
    c.mark = o.value(QStringLiteral("mark")).toString();
    c.block = o.value(QStringLiteral("block")).toString();
    c.assertFlag = o.value(QStringLiteral("assert")).toString();
    c.allFade = o.value(QStringLiteral("allFade")).toBool();
    c.preheat = o.value(QStringLiteral("preheat")).toBool();
    c.curve = o.value(QStringLiteral("curve")).toString();
    c.rate = o.value(QStringLiteral("rate")).toInt(100);
    c.timecode = o.value(QStringLiteral("timecode")).toString();
    for (const auto v : o.value(QStringLiteral("effects")).toArray()) c.effects << v.toString();
    for (const auto v : o.value(QStringLiteral("actions")).toArray()) c.actions << v.toString();
    return c;
}

// ── ManualPlacement ─────────────────────────────────────────────────────

QJsonObject ManualPlacement::toJson() const
{
    QJsonObject o{{QStringLiteral("list"), list},
                  {QStringLiteral("number"), number},
                  {QStringLiteral("mode"), modeKey(mode)}};
    if (part > 0) o.insert(QStringLiteral("part"), part);
    putIfSet(o, "uid", uid);
    putIfSet(o, "label", label);
    if (mode != Mode::Start && !anchor.isNull())
        o.insert(QStringLiteral("anchor"), anchor.toString(QUuid::WithoutBraces));
    return o;
}

ManualPlacement ManualPlacement::fromJson(const QJsonObject &o)
{
    ManualPlacement p;
    p.list = o.value(QStringLiteral("list")).toString();
    p.number = o.value(QStringLiteral("number")).toString();
    p.part = o.value(QStringLiteral("part")).toInt();
    p.uid = o.value(QStringLiteral("uid")).toString();
    p.label = o.value(QStringLiteral("label")).toString();
    p.mode = modeFromKey(o.value(QStringLiteral("mode")).toString());
    p.anchor = QUuid(o.value(QStringLiteral("anchor")).toString());
    return p;
}

bool ManualPlacement::sameCue(const DeskCue &c) const
{
    if (normalNumber(list) != normalNumber(c.list)) return false;
    if (!uid.isEmpty() && !c.uid.isEmpty()) return uid == c.uid;
    return normalNumber(number) == normalNumber(c.number) && part == c.part;
}

// ── Config ──────────────────────────────────────────────────────────────

QJsonObject Config::toJson() const
{
    QJsonObject o{{QStringLiteral("version"), kVersion},
                  {QStringLiteral("deskList"), deskList}};
    if (!sourceList.isNull())
        o.insert(QStringLiteral("sourceList"), sourceList.toString(QUuid::WithoutBraces));
    QJsonArray pl;
    for (const auto &p : placements) pl.append(p.toJson());
    if (!pl.isEmpty()) o.insert(QStringLiteral("placements"), pl);
    QJsonArray cache;
    for (const auto &c : deskCache) cache.append(c.toJson());
    if (!collapsedScenes.isEmpty())
        o.insert(QStringLiteral("collapsedScenes"), QJsonArray::fromStringList(collapsedScenes));
    if (!cache.isEmpty()) {
        o.insert(QStringLiteral("deskCache"), cache);
        if (deskCachedAt.isValid())
            o.insert(QStringLiteral("deskCachedAt"), deskCachedAt.toString(Qt::ISODate));
    }
    return o;
}

Config Config::fromJson(const QJsonObject &o)
{
    Config c;
    c.sourceList = QUuid(o.value(QStringLiteral("sourceList")).toString());
    const QString dl = o.value(QStringLiteral("deskList")).toString();
    if (!dl.isEmpty()) c.deskList = dl;
    for (const auto v : o.value(QStringLiteral("placements")).toArray())
        if (v.isObject()) c.placements.push_back(ManualPlacement::fromJson(v.toObject()));
    for (const auto v : o.value(QStringLiteral("deskCache")).toArray())
        if (v.isObject()) c.deskCache.push_back(DeskCue::fromJson(v.toObject()));
    c.deskCachedAt = QDateTime::fromString(o.value(QStringLiteral("deskCachedAt")).toString(),
                                           Qt::ISODate);
    for (const auto v : o.value(QStringLiteral("collapsedScenes")).toArray())
        c.collapsedScenes << v.toString();
    return c;
}

void Config::place(const DeskCue &cue, ManualPlacement::Mode mode, const QUuid &anchor)
{
    unplace(cue.list, cue.number, cue.part);
    ManualPlacement p;
    p.list = cue.list;
    p.number = cue.number;
    p.part = cue.part;
    p.uid = cue.uid;
    p.label = cue.label;
    p.mode = mode;
    if (mode != ManualPlacement::Mode::Start) p.anchor = anchor;
    placements.push_back(p);
}

bool Config::unplace(const QString &list, const QString &number, int part)
{
    const auto before = placements.size();
    placements.erase(std::remove_if(placements.begin(), placements.end(),
                                    [&](const ManualPlacement &p) {
                                        return normalNumber(p.list) == normalNumber(list)
                                            && normalNumber(p.number) == normalNumber(number)
                                            && p.part == part;
                                    }),
                     placements.end());
    return placements.size() != before;
}

// ── intermesh ───────────────────────────────────────────────────────────

Result intermesh(const std::vector<QuewiCue> &quewi, const std::vector<DeskCue> &desk,
                 const Config &config, bool deskKnown)
{
    Result r;
    const QString deskList = normalNumber(config.deskList);

    // The desk's cues, parts folded into their cue, in desk order.
    std::vector<DeskCell> bases;
    QHash<QString, int> baseOf;                     // "list/number" → bases index
    for (const auto &c : desk) {
        if (normalNumber(c.list) != deskList) continue;
        const QString k = cueKey(c.list, c.number);
        const auto it = baseOf.constFind(k);
        if (it == baseOf.constEnd()) {
            DeskCell cell;
            cell.cue = c;
            cell.deskIndex = int(bases.size());
            baseOf.insert(k, int(bases.size()));
            bases.push_back(cell);
            continue;
        }
        auto &cell = bases[size_t(*it)];
        if (c.part == 0 && cell.cue.part > 0) {      // the cue arrived after a part of it
            cell.parts.insert(cell.parts.begin(), cell.cue);
            cell.cue = c;
        } else {
            cell.parts.push_back(c);
        }
    }

    QHash<QUuid, int> quewiIndex;
    for (size_t i = 0; i < quewi.size(); ++i) quewiIndex.insert(quewi[i].id, int(i));

    // What quewi fires: "list/number" ("/number" = any list) → first firer.
    // A fire at GO beats a hit later in a song.
    struct Firer { int q = -1; DeskFire fire; };
    QHash<QString, Firer> fired;
    QStringList firedOrder;
    for (size_t q = 0; q < quewi.size(); ++q) {
        for (const auto &f : quewi[q].fires) {
            const QString fl = normalNumber(f.list);
            if (!fl.isEmpty() && fl != deskList) continue;
            const QString k = cueKey(fl, f.number);
            const auto it = fired.find(k);
            if (it == fired.end()) {
                fired.insert(k, {int(q), f});
                firedOrder << k;
            } else if (it->fire.atSeconds >= 0.0 && f.atSeconds < 0.0) {
                *it = {int(q), f};
            }
        }
    }
    auto firerFor = [&](const DeskCue &c) -> const Firer * {
        auto it = fired.constFind(cueKey(c.list, c.number));
        if (it != fired.constEnd()) return &*it;
        it = fired.constFind(cueKey(QString(), c.number));
        return it != fired.constEnd() ? &*it : nullptr;
    };

    const size_t n = quewi.size();
    std::vector<std::vector<DeskCell>> with(n), hits(n), after(n + 1);   // after[0] = top
    std::vector<bool> used(config.placements.size(), false);
    QSet<QString> firedSeen;
    size_t lastSlot = 0;

    for (auto &cell : bases) {
        const auto &c = cell.cue;
        // 1. By hand.
        bool placed = false;
        for (size_t pi = 0; pi < config.placements.size() && !placed; ++pi) {
            const auto &p = config.placements[pi];
            if (used[pi] || !p.sameCue(c)) continue;
            used[pi] = true;
            if (!p.uid.isEmpty() && p.uid == c.uid
                && (normalNumber(p.number) != normalNumber(c.number) || p.part != c.part))
                r.renumbered.push_back({int(pi), c.number, c.part});
            if (p.mode == ManualPlacement::Mode::Start) {
                cell.how = How::Manual;
                after[0].push_back(cell);
                lastSlot = 0;
                placed = true;
                break;
            }
            const int q = quewiIndex.value(p.anchor, -1);
            if (q < 0) {
                r.orphaned.push_back(int(pi));       // its quewi cue is gone: automatic
                break;
            }
            cell.how = How::Manual;
            if (p.mode == ManualPlacement::Mode::With) with[size_t(q)].push_back(cell);
            else after[size_t(q) + 1].push_back(cell);
            lastSlot = size_t(q) + 1;
            placed = true;
        }
        if (const auto *f = firerFor(c)) {
            firedSeen.insert(cueKey(f->fire.list, f->fire.number));
            if (!placed) {
                if (f->fire.atSeconds < 0.0) {
                    cell.how = How::Fired;
                    with[size_t(f->q)].push_back(cell);
                } else {
                    cell.how = How::Trigger;
                    cell.atSeconds = f->fire.atSeconds;
                    cell.triggerName = f->fire.triggerName;
                    hits[size_t(f->q)].push_back(cell);
                }
                lastSlot = size_t(f->q) + 1;
                placed = true;
            }
        }
        if (placed) continue;
        // 4. By order: after whatever the desk cue before it is tied to.
        cell.how = How::Order;
        after[lastSlot].push_back(cell);
    }

    // Hand placements whose cue isn't on the desk: kept, where they were.
    for (size_t pi = 0; pi < config.placements.size(); ++pi) {
        const auto &p = config.placements[pi];
        if (used[pi] || normalNumber(p.list) != deskList) continue;
        DeskCell cell;
        cell.cue.list = p.list;
        cell.cue.number = p.number;
        cell.cue.part = p.part;
        cell.cue.uid = p.uid;
        cell.cue.label = p.label;
        cell.how = How::Manual;
        cell.missing = deskKnown;
        const int q = quewiIndex.value(p.anchor, -1);
        if (p.mode == ManualPlacement::Mode::Start || q < 0) {
            if (p.mode != ManualPlacement::Mode::Start) r.orphaned.push_back(int(pi));
            after[0].push_back(cell);
        } else if (p.mode == ManualPlacement::Mode::With) {
            with[size_t(q)].push_back(cell);
        } else {
            after[size_t(q) + 1].push_back(cell);
        }
    }

    // Cues quewi fires that the desk doesn't have.
    if (deskKnown) {
        for (const auto &k : std::as_const(firedOrder)) {
            if (firedSeen.contains(k)) continue;
            const auto &f = fired[k];
            DeskCell cell;
            cell.cue.list = f.fire.list.isEmpty() ? config.deskList : f.fire.list;
            cell.cue.number = f.fire.number;
            cell.missing = true;
            cell.atSeconds = f.fire.atSeconds;
            cell.triggerName = f.fire.triggerName;
            if (f.fire.atSeconds < 0.0) {
                cell.how = How::Fired;
                with[size_t(f.q)].push_back(cell);
            } else {
                cell.how = How::Trigger;
                hits[size_t(f.q)].push_back(cell);
            }
        }
    }

    // Desk order within a slot (missing ones last, as added); hits by time.
    auto byDesk = [](const DeskCell &a, const DeskCell &b) {
        const int x = a.deskIndex < 0 ? INT_MAX : a.deskIndex;
        const int y = b.deskIndex < 0 ? INT_MAX : b.deskIndex;
        return x < y;
    };
    for (auto &v : with) std::stable_sort(v.begin(), v.end(), byDesk);
    for (auto &v : after) std::stable_sort(v.begin(), v.end(), byDesk);
    for (auto &v : hits)
        std::stable_sort(v.begin(), v.end(), [](const DeskCell &a, const DeskCell &b) {
            return a.atSeconds < b.atSeconds;
        });

    auto deskRows = [&r](std::vector<DeskCell> &cells, Row::Kind kind, int q) {
        for (auto &c : cells) {
            Row row;
            row.kind = kind;
            row.quewiIndex = q;
            row.desk.push_back(std::move(c));
            r.rows.push_back(std::move(row));
        }
    };
    deskRows(after[0], Row::Kind::Desk, -1);
    for (size_t q = 0; q < n; ++q) {
        Row row;
        row.kind = Row::Kind::Quewi;
        row.quewiIndex = int(q);
        row.desk = std::move(with[q]);
        r.rows.push_back(std::move(row));
        deskRows(hits[q], Row::Kind::Hit, int(q));
        deskRows(after[q + 1], Row::Kind::Desk, int(q));
    }
    addScenes(r);
    return r;
}

bool isSceneEndText(const QString &scene)
{
    const QString s = scene.trimmed();
    return s.compare(QLatin1String("End"), Qt::CaseInsensitive) == 0
        || s.startsWith(QLatin1String("End of "), Qt::CaseInsensitive)
        || s.startsWith(QLatin1String("End: "), Qt::CaseInsensitive)
        || s.startsWith(QLatin1String("End - "), Qt::CaseInsensitive);
}

void addScenes(Result &r)
{
    // Pass 1: which row each scene starts and ends on.
    struct Span { QString name; int from = -1, to = -1; QString first, last; };
    std::vector<Span> spans;
    int open = -1;                              // index into spans
    for (size_t i = 0; i < r.rows.size(); ++i) {
        for (const auto &c : r.rows[i].desk) {
            if (c.missing) continue;
            const QString text = c.cue.scene.trimmed();
            const bool starts = !text.isEmpty() && !isSceneEndText(text);
            const bool ends = c.cue.sceneEnd || (!text.isEmpty() && isSceneEndText(text));
            if (starts) {
                // A new scene: the open one (if any) ends on the row before.
                if (open >= 0 && spans[size_t(open)].to < 0)
                    spans[size_t(open)].to = int(i) - 1;
                spans.push_back({text, int(i), -1, c.cue.number, c.cue.number});
                open = int(spans.size()) - 1;
            } else if (open >= 0 && spans[size_t(open)].to < 0) {
                spans[size_t(open)].last = c.cue.number;
            }
            if (ends && open >= 0 && spans[size_t(open)].to < 0)
                spans[size_t(open)].to = int(i);
        }
    }
    if (open >= 0 && spans[size_t(open)].to < 0) spans[size_t(open)].to = int(r.rows.size()) - 1;
    if (spans.empty()) return;

    // Pass 2: rebuild the rows with a header in front of each scene.
    std::vector<Row> out;
    out.reserve(r.rows.size() + spans.size());
    size_t s = 0;
    for (size_t i = 0; i < r.rows.size(); ++i) {
        while (s < spans.size() && spans[s].from == int(i)) {
            Row h;
            h.kind = Row::Kind::Scene;
            h.quewiIndex = out.empty() ? -1 : out.back().quewiIndex;
            h.scene = int(r.scenes.size());
            Scene sc;
            sc.name = spans[s].name;
            sc.firstCue = spans[s].first;
            sc.lastCue = spans[s].last;
            sc.header = int(out.size());
            sc.rows = std::max(0, spans[s].to - spans[s].from + 1);
            r.scenes.push_back(sc);
            out.push_back(std::move(h));
            ++s;
        }
        Row row = std::move(r.rows[i]);
        // The innermost scene that covers this row.
        for (int k = int(r.scenes.size()) - 1; k >= 0; --k)
            if (int(i) >= spans[size_t(k)].from && int(i) <= spans[size_t(k)].to) { row.scene = k; break; }
        out.push_back(std::move(row));
    }
    r.rows = std::move(out);
}

} // namespace quewi::core::matrix
