#include "ui/MatrixSource.h"

#include "audio/AudioCue.h"
#include "audio/LightTrigger.h"
#include "core/CueList.h"
#include "core/Workspace.h"
#include "cues/Cue.h"
#include "midi/MidiCue.h"
#include "osc/EosCueLists.h"
#include "osc/OscCue.h"
#include "video/VideoCue.h"

#include <QJsonArray>
#include <QRegularExpression>

#include <algorithm>

namespace quewi::ui {

namespace m = core::matrix;

namespace {

// An Eos "fire this cue" in OSC: /eos/cue/<list>/<cue>/fire or
// /eos/cue/<cue>/fire (the desk's default list), or a typed command line.
bool eosFireFromOsc(const QString &address, const QString &args, QString *list, QString *number)
{
    static const QRegularExpression fireRe(
        QStringLiteral(R"(^/eos/cue/(?:([0-9.]+)/)?([0-9.]+)/fire/?$)"),
        QRegularExpression::CaseInsensitiveOption);
    if (const auto mt = fireRe.match(address.trimmed()); mt.hasMatch()) {
        *list = mt.captured(1);
        *number = mt.captured(2);
        return true;
    }
    static const QRegularExpression cmdAddr(QStringLiteral(R"(^/eos/(new)?cmd/?$)"),
                                            QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression goTo(
        QStringLiteral(R"(Go_?To_?Cue\s+(?:([0-9.]+)\s*/\s*)?([0-9.]+))"),
        QRegularExpression::CaseInsensitiveOption);
    if (cmdAddr.match(address.trimmed()).hasMatch()) {
        if (const auto mt = goTo.match(args); mt.hasMatch()) {
            *list = mt.captured(1);
            *number = mt.captured(2);
            return true;
        }
    }
    return false;
}

bool eosFireFromAction(const audio::TriggerAction &a, QString *list, QString *number)
{
    using K = audio::TriggerAction::Kind;
    if (a.kind == K::Desk && a.deskDo == audio::TriggerAction::DeskDo::GoToCue
        && !a.number.trimmed().isEmpty()) {
        *list = QString::number(a.list);
        *number = a.number.trimmed();
        return true;
    }
    if (a.kind == K::Osc) return eosFireFromOsc(a.address, a.args, list, number);
    return false;
}

} // namespace

QString matrixCueNumber(double n)
{
    QString s = QString::number(n, 'f', 3);
    while (s.endsWith(QLatin1Char('0'))) s.chop(1);
    if (s.endsWith(QLatin1Char('.'))) s.chop(1);
    return s;
}

std::vector<m::DeskFire> deskFiresOf(cues::Cue *cue)
{
    std::vector<m::DeskFire> out;
    if (!cue) return out;
    QString list, number;

    if (auto *ac = video::VideoCue::audioOf(cue)) {
        const double in = ac->trimInSeconds();
        auto afterGo = [&](double at) {
            const double s = at - in - audio::cutSecondsBetween(ac->cuts(), in, at);
            return s <= 0.05 ? -1.0 : s;      // right at the top = with the GO
        };
        for (const auto &t : ac->lightTriggers()) {
            if (!t.enabled) continue;
            if (eosFireFromAction(t.enter, &list, &number))
                out.push_back({list, number, afterGo(t.start), t.name});
            if (t.isRange() && eosFireFromAction(t.exit, &list, &number))
                out.push_back({list, number, afterGo(t.end), t.name});
        }
    }
    if (auto *oc = qobject_cast<osc::OscCue *>(cue)) {
        if (eosFireFromOsc(oc->field(QStringLiteral("address")).toString(),
                           oc->field(QStringLiteral("rawArgs")).toString(), &list, &number))
            out.push_back({list, number, -1.0, {}});
    }
    if (auto *msc = qobject_cast<midi::MscCue *>(cue)) {
        // GO (0x01) to a lighting command format (0x01..0x0F) with a Q number.
        if (msc->command() == 0x01 && msc->commandFormat() >= 0x01 && msc->commandFormat() <= 0x0F
            && !msc->qNumber().trimmed().isEmpty())
            out.push_back({msc->qList().trimmed(), msc->qNumber().trimmed(), -1.0, {}});
    }
    return out;
}

std::vector<m::QuewiCue> matrixQuewiCues(const core::CueList *list)
{
    std::vector<m::QuewiCue> out;
    if (!list) return out;
    out.reserve(size_t(list->cueCount()));
    for (int r = 0; r < list->cueCount(); ++r) {
        auto *c = list->cueAt(r);
        if (!c) continue;
        m::QuewiCue q;
        q.id = c->id();
        q.number = matrixCueNumber(c->number());
        q.type = c->typeName();
        q.name = c->name().isEmpty() ? q.type : c->name();
        q.notes = c->notes().trimmed();
        q.colour = c->color();
        q.fires = deskFiresOf(c);
        out.push_back(std::move(q));
    }
    return out;
}

core::CueList *matrixSourceList(const core::Workspace *ws, const core::CueList *matrix)
{
    if (!ws) return nullptr;
    const QUuid want = matrix ? matrix->matrixConfig().sourceList : QUuid();
    core::CueList *first = nullptr;
    for (const auto &l : ws->cueLists()) {
        if (l->kind() != core::CueList::Kind::Normal) continue;
        if (!first) first = l.get();
        if (!want.isNull() && l->id() == want) return l.get();
    }
    return first;
}

m::DeskCue toDeskCue(const osc::EosCue &c)
{
    m::DeskCue d;
    d.list = c.list;
    d.number = c.number;
    d.part = c.part;
    d.uid = c.uid;
    d.label = c.label;
    d.notes = c.notes;
    d.scene = c.scene;
    d.sceneEnd = c.sceneEnd;
    d.upSeconds = c.upMs >= 0 ? c.upMs / 1000.0 : -1.0;
    d.downSeconds = c.downMs >= 0 ? c.downMs / 1000.0 : -1.0;
    d.followSeconds = c.followMs >= 0 ? c.followMs / 1000.0 : -1.0;
    d.hangSeconds = c.hangMs >= 0 ? c.hangMs / 1000.0 : -1.0;
    d.partCount = c.partCount;
    return d;
}

std::vector<m::DeskCue> toDeskCues(const QVector<osc::EosCue> &cues)
{
    std::vector<m::DeskCue> out;
    out.reserve(size_t(cues.size()));
    for (const auto &c : cues) out.push_back(toDeskCue(c));
    return out;
}

QString matrixDeskName(const m::DeskCue &c)
{
    return c.part > 0 ? QStringLiteral("LX %1 P%2").arg(c.number).arg(c.part)
                      : QStringLiteral("LX %1").arg(c.number);
}

bool matrixDeskIs(const m::DeskCue &c, const QString &list, const QString &cue)
{
    if (cue.isEmpty()) return false;
    if (!list.isEmpty() && m::normalNumber(list) != m::normalNumber(c.list)) return false;
    return m::normalNumber(cue) == m::normalNumber(c.number);
}

int MatrixBuild::rowOfQuewi(const QUuid &id) const
{
    if (id.isNull()) return -1;
    for (size_t i = 0; i < result.rows.size(); ++i) {
        const auto &r = result.rows[i];
        if (r.kind == m::Row::Kind::Quewi && r.quewiIndex >= 0
            && quewi[size_t(r.quewiIndex)].id == id) return int(i);
    }
    return -1;
}

int MatrixBuild::rowOfDesk(const QString &list, const QString &number) const
{
    for (size_t i = 0; i < result.rows.size(); ++i)
        for (const auto &c : result.rows[i].desk)
            if (!c.missing && matrixDeskIs(c.cue, list, number)) return int(i);
    return -1;
}

MatrixBuild buildMatrix(const core::Workspace *ws, const core::CueList *matrix,
                        const osc::EosCueLists *reader)
{
    MatrixBuild b;
    if (!matrix) return b;
    const auto &cfg = matrix->matrixConfig();
    b.source = matrixSourceList(ws, matrix);
    b.deskList = m::normalNumber(cfg.deskList);
    b.quewi = matrixQuewiCues(b.source);
    if (reader) {
        const auto st = reader->state(b.deskList);
        const auto live = reader->cues(b.deskList);
        if (st == osc::EosCueLists::State::Ready || st == osc::EosCueLists::State::Partial
            || !live.isEmpty()) {
            b.desk = MatrixBuild::Desk::Live;
            b.deskCues = toDeskCues(live);
        }
    }
    if (b.desk == MatrixBuild::Desk::None
        && (!cfg.deskCache.empty() || cfg.deskCachedAt.isValid())) {
        b.desk = MatrixBuild::Desk::Cached;
        for (const auto &c : cfg.deskCache)
            if (m::normalNumber(c.list) == b.deskList) b.deskCues.push_back(c);
    }
    b.result = m::intermesh(b.quewi, b.deskCues, cfg, b.desk != MatrixBuild::Desk::None);
    return b;
}

namespace {
QString howKey(m::How h)
{
    switch (h) {
    case m::How::Manual:  return QStringLiteral("manual");
    case m::How::Fired:   return QStringLiteral("fired");
    case m::How::Trigger: return QStringLiteral("hit");
    case m::How::Order:   return QStringLiteral("order");
    }
    return {};
}
QString rowKindKey(m::Row::Kind k)
{
    switch (k) {
    case m::Row::Kind::Quewi: return QStringLiteral("quewi");
    case m::Row::Kind::Hit:   return QStringLiteral("hit");
    case m::Row::Kind::Desk:  return QStringLiteral("desk");
    }
    return {};
}
} // namespace

QJsonObject matrixJson(const core::CueList *matrix, const MatrixBuild &b, const MatrixLive &live,
                       int from, int count)
{
    const int total = int(b.result.rows.size());
    from = std::clamp(from, 0, total);
    const int to = count < 0 ? total : std::min(total, from + count);
    QJsonArray rows;
    for (int i = from; i < to; ++i) {
        const auto &r = b.result.rows[size_t(i)];
        QJsonObject o{{QStringLiteral("row"), i}, {QStringLiteral("kind"), rowKindKey(r.kind)}};
        if (r.quewiIndex >= 0 && r.kind != m::Row::Kind::Desk) {
            const auto &q = b.quewi[size_t(r.quewiIndex)];
            QJsonObject cue{{QStringLiteral("id"), q.id.toString()},
                            {QStringLiteral("number"), q.number},
                            {QStringLiteral("name"), q.name},
                            {QStringLiteral("type"), q.type}};
            if (!q.notes.isEmpty()) cue.insert(QStringLiteral("notes"), q.notes);
            o.insert(QStringLiteral("cue"), cue);
            if (r.kind == m::Row::Kind::Quewi) {
                o.insert(QStringLiteral("standby"), q.id == live.standby);
                o.insert(QStringLiteral("running"), live.running.contains(q.id));
            }
        } else if (r.kind == m::Row::Kind::Desk && r.quewiIndex >= 0) {
            o.insert(QStringLiteral("after"), b.quewi[size_t(r.quewiIndex)].number);
        }
        QJsonArray lights;
        for (const auto &c : r.desk) {
            QJsonObject l = c.cue.toJson();
            l.insert(QStringLiteral("how"), howKey(c.how));
            if (c.missing) l.insert(QStringLiteral("missing"), true);
            if (c.atSeconds >= 0.0) l.insert(QStringLiteral("at"), c.atSeconds);
            if (!c.triggerName.isEmpty()) l.insert(QStringLiteral("trigger"), c.triggerName);
            if (!c.parts.empty()) l.insert(QStringLiteral("partsListed"), int(c.parts.size()));
            l.insert(QStringLiteral("active"), !c.missing && matrixDeskIs(c.cue, live.deskActiveList, live.deskActiveCue));
            l.insert(QStringLiteral("pending"), !c.missing && matrixDeskIs(c.cue, live.deskPendingList, live.deskPendingCue));
            lights.append(l);
        }
        if (!lights.isEmpty()) o.insert(QStringLiteral("lights"), lights);
        rows.append(o);
    }
    QString desk = QStringLiteral("none");
    if (b.desk == MatrixBuild::Desk::Live) desk = QStringLiteral("live");
    else if (b.desk == MatrixBuild::Desk::Cached) desk = QStringLiteral("cached");
    return QJsonObject{
        {QStringLiteral("list"), matrix ? matrix->name() : QString()},
        {QStringLiteral("id"), matrix ? matrix->id().toString() : QString()},
        {QStringLiteral("source"), b.source ? b.source->name() : QString()},
        {QStringLiteral("deskList"), b.deskList},
        {QStringLiteral("desk"), desk},
        {QStringLiteral("total"), total},
        {QStringLiteral("from"), from},
        {QStringLiteral("standbyRow"), b.rowOfQuewi(live.standby)},
        {QStringLiteral("deskActiveRow"), b.rowOfDesk(live.deskActiveList, live.deskActiveCue)},
        {QStringLiteral("deskPendingRow"), b.rowOfDesk(live.deskPendingList, live.deskPendingCue)},
        {QStringLiteral("rows"), rows},
    };
}

} // namespace quewi::ui
