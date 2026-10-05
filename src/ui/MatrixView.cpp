#include "ui/MatrixView.h"

#include "core/CueList.h"
#include "core/UndoCommands.h"
#include "core/Workspace.h"
#include "osc/EosCueLists.h"
#include "osc/EosFeedback.h"
#include "ui/ShowModeView.h"
#include "ui/Theme.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMenu>
#include <QMimeData>
#include <QPainter>
#include <QPushButton>
#include <QScrollBar>
#include <QSettings>
#include <QStackedWidget>
#include <QTableView>
#include <QTimer>
#include <QUndoStack>
#include <QVBoxLayout>

#include <cmath>

namespace quewi::ui {

namespace m = core::matrix;

namespace {

// "5", "2.5", "1:30" — a desk time the way the desk shows it.
QString secondsText(double s)
{
    if (s < 0.0) return {};
    if (s >= 60.0) {
        const int whole = int(std::floor(s));
        QString t = QStringLiteral("%1:%2").arg(whole / 60).arg(whole % 60, 2, 10, QLatin1Char('0'));
        const double frac = s - whole;
        if (frac > 0.05) t += QString::number(frac, 'f', 1).mid(1);
        return t;
    }
    QString t = QString::number(s, 'f', 2);
    while (t.endsWith(QLatin1Char('0'))) t.chop(1);
    if (t.endsWith(QLatin1Char('.'))) t.chop(1);
    return t;
}

QString clockText(double s)
{
    const int whole = int(std::floor(std::max(0.0, s) + 1e-6));
    return QStringLiteral("%1:%2").arg(whole / 60).arg(whole % 60, 2, 10, QLatin1Char('0'));
}

QString cellLine(const m::DeskCell &c)
{
    QString t = matrixDeskName(c.cue);
    if (!c.cue.label.isEmpty()) t += QStringLiteral("  ") + c.cue.label;
    if (!c.parts.empty())
        t += c.parts.size() == 1 ? MatrixTableModel::tr("  (+1 part)")
                                 : MatrixTableModel::tr("  (+%1 parts)").arg(c.parts.size());
    if (c.missing) t += MatrixTableModel::tr("  — not on the desk");
    return t;
}

QString cellTime(const m::DeskCell &c)
{
    QString t = secondsText(c.cue.upSeconds);
    const QString down = secondsText(c.cue.downSeconds);
    if (!down.isEmpty() && down != t) t += QStringLiteral(" / ") + down;
    if (c.cue.followSeconds >= 0.0) t += MatrixTableModel::tr("  F %1").arg(secondsText(c.cue.followSeconds));
    if (c.cue.hangSeconds >= 0.0) t += MatrixTableModel::tr("  H %1").arg(secondsText(c.cue.hangSeconds));
    return t;
}

// What the desk cue does by itself, inline: follow / hang chains, links,
// loops, and the B / A badges. Empty when nothing.
QString flagsText(const m::DeskCell &c)
{
    QStringList f;
    const auto &d = c.cue;
    const QString next = d.link.isEmpty() ? MatrixTableModel::tr("the next cue")
                                          : MatrixTableModel::tr("LX %1").arg(d.link);
    if (d.followSeconds >= 0.0)
        f << MatrixTableModel::tr("→ runs on to %1 in %2 s").arg(next, secondsText(d.followSeconds));
    else if (d.hangSeconds >= 0.0)
        f << MatrixTableModel::tr("→ runs on to %1 %2 s after it ends").arg(next, secondsText(d.hangSeconds));
    if (!d.link.isEmpty()) f << MatrixTableModel::tr("↪ links to LX %1").arg(d.link);
    if (d.loop > 0) f << MatrixTableModel::tr("⟲ loops ×%1").arg(d.loop);
    else if (d.loop == 0) f << MatrixTableModel::tr("⟲ loops");
    if (!d.block.isEmpty()) f << QStringLiteral("[%1]").arg(d.block.toUpper() == QLatin1String("B")
                                                         ? QStringLiteral("B") : d.block.toUpper());
    if (!d.assertFlag.isEmpty()) f << QStringLiteral("[A]");
    return f.join(QStringLiteral("   "));
}

// Everything else the desk records for a cue (the Details view).
QString detailsText(const m::DeskCell &c)
{
    const auto &d = c.cue;
    QStringList t;
    auto add = [&t](const QString &name, double s) {
        if (s >= 0.0) t << MatrixTableModel::tr("%1 %2").arg(name, secondsText(s));
    };
    add(MatrixTableModel::tr("Up"), d.upSeconds);
    add(MatrixTableModel::tr("delay"), d.upDelaySeconds);
    add(MatrixTableModel::tr("Down"), d.downSeconds);
    add(MatrixTableModel::tr("Focus"), d.focusSeconds);
    add(MatrixTableModel::tr("Colour"), d.colourSeconds);
    add(MatrixTableModel::tr("Beam"), d.beamSeconds);
    add(MatrixTableModel::tr("Follow"), d.followSeconds);
    add(MatrixTableModel::tr("Hang"), d.hangSeconds);
    QStringList flags;
    if (!d.mark.isEmpty()) flags << MatrixTableModel::tr("Mark (%1)").arg(d.mark);
    if (!d.block.isEmpty()) flags << MatrixTableModel::tr("Block (%1)").arg(d.block);
    if (!d.assertFlag.isEmpty()) flags << MatrixTableModel::tr("Assert (%1)").arg(d.assertFlag);
    if (d.allFade) flags << MatrixTableModel::tr("All fade");
    if (d.preheat) flags << MatrixTableModel::tr("Preheat");
    if (!d.curve.isEmpty() && d.curve != QLatin1String("0")) flags << MatrixTableModel::tr("Curve %1").arg(d.curve);
    if (d.rate != 100) flags << MatrixTableModel::tr("Rate %1 %").arg(d.rate);
    if (!d.timecode.isEmpty()) flags << MatrixTableModel::tr("Timecode %1").arg(d.timecode);
    if (d.partCount > 0) flags << MatrixTableModel::tr("%1 parts").arg(d.partCount);
    if (!d.effects.isEmpty()) flags << MatrixTableModel::tr("Effects %1").arg(d.effects.join(QStringLiteral(", ")));
    if (!d.actions.isEmpty()) flags << MatrixTableModel::tr("Runs %1").arg(d.actions.join(QStringLiteral(", ")));
    if (!d.scene.isEmpty()) flags << (d.sceneEnd ? MatrixTableModel::tr("Scene ends: %1") : MatrixTableModel::tr("Scene: %1")).arg(d.scene);
    QString out = t.join(QStringLiteral(" · "));
    if (!flags.isEmpty()) out += (out.isEmpty() ? QString() : QStringLiteral("\n")) + flags.join(QStringLiteral(" · "));
    return out.isEmpty() ? MatrixTableModel::tr("(nothing else recorded)") : out;
}

QString deskKey(const m::DeskCue &c)
{
    return QStringLiteral("%1/%2/%3").arg(m::normalNumber(c.list), m::normalNumber(c.number)).arg(c.part);
}

// The "how it's lined up" line for one lighting cue.
QString placedText(const m::DeskCell &c, const QString &q)
{
    QString t;
    switch (c.how) {
    case m::How::Manual:  t = MatrixTableModel::tr("◆ Lined up by hand"); break;
    case m::How::Fired:   t = MatrixTableModel::tr("Fired by Q%1's GO").arg(q); break;
    case m::How::Trigger: t = MatrixTableModel::tr("Hit %1 into Q%2").arg(clockText(c.atSeconds), q); break;
    case m::How::Order:   t = MatrixTableModel::tr("Follows the desk's order"); break;
    }
    if (c.missing) t += MatrixTableModel::tr(" · not on the desk");
    return t;
}

QString howTip(const m::DeskCell &c, const QString &q)
{
    switch (c.how) {
    case m::How::Manual:
        return c.missing ? MatrixTableModel::tr("You lined this up by hand. The desk doesn't have this cue (any more) — "
                                                "right-click to forget it.")
                         : MatrixTableModel::tr("You lined this up by hand. Right-click → Unlink to let quewi place it again.");
    case m::How::Fired:
        return MatrixTableModel::tr("Quewi cue %1 sends the desk to this cue when it's GO'd, so they share a row.").arg(q);
    case m::How::Trigger:
        return MatrixTableModel::tr("A lighting trigger %1 into quewi cue %2's song fires this cue (\"%3\").")
            .arg(clockText(c.atSeconds), q, c.triggerName);
    case m::How::Order:
        return MatrixTableModel::tr("Nothing in quewi fires this cue, so it follows the desk cue before it. "
                                    "Drag it onto a quewi cue (or a quewi cue onto it) to line them up.");
    }
    return {};
}

} // namespace

// ── MatrixTableModel ────────────────────────────────────────────────────

MatrixTableModel::MatrixTableModel(QObject *parent) : QAbstractTableModel(parent) {}

void MatrixTableModel::setBuild(const MatrixBuild &b)
{
    beginResetModel();
    m_build = b;
    computeChains();
    endResetModel();
}

void MatrixTableModel::computeChains()
{
    // A desk cue with a follow or hang runs on by itself into the next cue
    // of the list (or the one it links to): join their rows with a line.
    const auto &rows = m_build.result.rows;
    m_chain.assign(rows.size(), 0);
    QHash<QString, int> rowOf;                  // desk cue number → first row
    for (size_t r = 0; r < rows.size(); ++r)
        for (const auto &c : rows[r].desk)
            if (!c.missing && c.cue.part == 0 && !rowOf.contains(m::normalNumber(c.cue.number)))
                rowOf.insert(m::normalNumber(c.cue.number), int(r));
    std::vector<const m::DeskCue *> order;      // the desk list's cues, in order
    for (const auto &c : m_build.deskCues)
        if (c.part == 0) order.push_back(&c);
    for (size_t i = 0; i < order.size(); ++i) {
        const auto &c = *order[i];
        if (!c.autoRuns()) continue;
        const QString target = !c.link.isEmpty() ? m::normalNumber(c.link)
                             : (i + 1 < order.size() ? m::normalNumber(order[i + 1]->number) : QString());
        const int a = rowOf.value(m::normalNumber(c.number), -1);
        const int b = rowOf.value(target, -1);
        if (a < 0 || b < 0 || b <= a) continue;  // backwards links: shown as text only
        m_chain[size_t(a)] |= ChainBottom | ChainMember;
        m_chain[size_t(b)] |= ChainTop | ChainMember;
        for (int r = a + 1; r < b; ++r) m_chain[size_t(r)] |= ChainTop | ChainBottom;
    }
}

void MatrixTableModel::setDetails(bool on)
{
    if (on == m_details) return;
    beginResetModel();
    m_details = on;
    endResetModel();
}

bool MatrixTableModel::rowDetailsOpen(int row) const
{
    if (row < 0 || row >= rowCount()) return false;
    for (const auto &c : m_build.result.rows[size_t(row)].desk)
        if (m_open.contains(deskKey(c.cue))) return true;
    return false;
}

void MatrixTableModel::toggleRowDetails(int row)
{
    if (row < 0 || row >= rowCount()) return;
    const auto &desk = m_build.result.rows[size_t(row)].desk;
    if (desk.empty()) return;
    const bool open = rowDetailsOpen(row);
    for (const auto &c : desk) {
        if (open) m_open.remove(deskKey(c.cue));
        else m_open.insert(deskKey(c.cue));
    }
    emit dataChanged(index(row, 0), index(row, ColCount - 1));
    emit layoutChanged();                       // the row's height changes
}

void MatrixTableModel::setCollapsedScenes(const QStringList &names)
{
    m_collapsed = names;
    for (int r = 0; r < rowCount(); ++r)
        if (m_build.result.rows[size_t(r)].kind == m::Row::Kind::Scene)
            emit dataChanged(index(r, 0), index(r, ColCount - 1));
}

QString MatrixTableModel::lightsText(int r) const
{
    const auto &row = m_build.result.rows[size_t(r)];
    const bool details = m_details || rowDetailsOpen(r);
    QStringList lines;
    for (const auto &c : row.desk) {
        lines << cellLine(c);
        if (c.missing) continue;
        const QString f = flagsText(c);
        if (!f.isEmpty()) lines << QStringLiteral("    ") + f;
        if (details)
            for (const auto &l : detailsText(c).split(QLatin1Char('\n')))
                lines << QStringLiteral("    ") + l;
    }
    return lines.join(QLatin1Char('\n'));
}

void MatrixTableModel::setLive(const MatrixLive &live)
{
    if (live == m_live) return;
    m_live = live;
    if (rowCount() > 0)
        emit dataChanged(index(0, 0), index(rowCount() - 1, ColCount - 1));
}

int MatrixTableModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_build.result.rows.size());
}

int MatrixTableModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColCount;
}

QVariant MatrixTableModel::headerData(int section, Qt::Orientation o, int role) const
{
    if (o != Qt::Horizontal) return {};
    if (role == Qt::DisplayRole) {
        switch (section) {
        case ColState:  return tr("NOW");
        case ColNumber: return tr("Q");
        case ColCue:    return tr("SOUND & VIDEO  (quewi)");
        case ColLights: return tr("LIGHTS  (desk cue)");
        case ColTime:   return tr("DESK TIME");
        case ColPlaced: return tr("LINED UP");
        case ColNotes:  return tr("NOTES & SCENE");
        }
    }
    if (role == Qt::ToolTipRole) {
        switch (section) {
        case ColState:  return tr("What's happening on this row now: quewi's STANDBY (what GO fires next) and "
                                  "PLAYING cues, the desk's LX LIVE cue, and LX NEXT (what GO Lights fires).");
        case ColNumber: return tr("The quewi cue number.");
        case ColCue:    return tr("quewi's cue. Its order is GO order: the matrix never changes it. "
                                  "Indented rows are lighting cues hit part-way through the song above.");
        case ColLights: return tr("The lighting desk's cue on this row: number, label, and how many parts it has.");
        case ColTime:   return tr("The desk cue's up / down time, follow (F) and hang (H), in seconds.");
        case ColPlaced: return tr("Why the lighting cue is on this row: lined up by you (◆), fired by a quewi cue's GO, "
                                  "hit inside a song, or following the desk's order.");
        case ColNotes:  return tr("quewi's notes for the cue, then the desk cue's scene and notes.");
        }
    }
    return {};
}

QString MatrixTableModel::stateText(int r) const
{
    const auto &row = m_build.result.rows[size_t(r)];
    QStringList parts;
    if (row.kind == m::Row::Kind::Quewi && row.quewiIndex >= 0) {
        const auto &id = m_build.quewi[size_t(row.quewiIndex)].id;
        if (id == m_live.standby) parts << tr("STANDBY");
        if (m_live.running.contains(id)) parts << tr("PLAYING");
    }
    for (const auto &c : row.desk) {
        if (c.missing) { parts << QStringLiteral("⚠"); continue; }
        if (matrixDeskIs(c.cue, m_live.deskActiveList, m_live.deskActiveCue)) parts << tr("LX LIVE");
        else if (matrixDeskIs(c.cue, m_live.deskPendingList, m_live.deskPendingCue)) parts << tr("LX NEXT");
    }
    parts.removeDuplicates();
    return parts.join(QStringLiteral(" · "));
}

QColor MatrixTableModel::rowEdge(int r) const
{
    const auto &row = m_build.result.rows[size_t(r)];
    const auto rc = showRegionColours();
    if (row.kind == m::Row::Kind::Quewi && row.quewiIndex >= 0) {
        const auto &id = m_build.quewi[size_t(row.quewiIndex)].id;
        if (id == m_live.standby) return rc.standby;
        if (m_live.running.contains(id)) return rc.running;
    }
    for (const auto &c : row.desk)
        if (!c.missing && (matrixDeskIs(c.cue, m_live.deskActiveList, m_live.deskActiveCue)
                           || matrixDeskIs(c.cue, m_live.deskPendingList, m_live.deskPendingCue)))
            return rc.desk;
    if (row.kind == m::Row::Kind::Hit) return showMix(Theme::tokens().bgRow, rc.hits, 0.55);
    return {};
}

QColor MatrixTableModel::rowTint(int r) const
{
    const auto &row = m_build.result.rows[size_t(r)];
    const auto &tk = Theme::tokens();
    const auto rc = showRegionColours();
    const QColor base = tk.bgPanel;
    if (row.kind == m::Row::Kind::Quewi && row.quewiIndex >= 0) {
        const auto &id = m_build.quewi[size_t(row.quewiIndex)].id;
        if (id == m_live.standby) return showMix(base, rc.standby, 0.26);
        if (m_live.running.contains(id)) return showMix(base, rc.running, 0.24);
    }
    for (const auto &c : row.desk)
        if (!c.missing && matrixDeskIs(c.cue, m_live.deskActiveList, m_live.deskActiveCue))
            return showMix(base, rc.desk, 0.34);
    for (const auto &c : row.desk)
        if (!c.missing && matrixDeskIs(c.cue, m_live.deskPendingList, m_live.deskPendingCue))
            return showMix(base, rc.desk, 0.15);
    // A hit sits a shade deeper, so it reads as part of the song above.
    if (row.kind == m::Row::Kind::Hit) return showMix(base, tk.bgDeep, 0.6);
    return {};
}

QVariant MatrixTableModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= rowCount()) return {};
    const auto &row = m_build.result.rows[size_t(index.row())];
    const m::QuewiCue *q = row.quewiIndex >= 0 ? &m_build.quewi[size_t(row.quewiIndex)] : nullptr;
    const auto rc = showRegionColours();
    const auto &tk = Theme::tokens();
    const QString qNum = q ? q->number : QString();
    bool linked = false;
    for (const auto &c : row.desk) linked |= c.how == m::How::Manual;

    if (row.kind == m::Row::Kind::Scene) {
        const m::Scene *sc = row.scene >= 0 && size_t(row.scene) < m_build.result.scenes.size()
                                 ? &m_build.result.scenes[size_t(row.scene)] : nullptr;
        const bool shut = sc && m_collapsed.contains(sc->name);
        switch (role) {
        case KindRole:  return int(row.kind);
        case SceneRole: return row.scene;
        case Qt::DisplayRole:
            if (index.column() != 0 || !sc) return {};
            return tr("%1   SCENE  %2      LX %3 – %4  ·  %5")
                .arg(shut ? QStringLiteral("▸") : QStringLiteral("▾"), sc->name, sc->firstCue, sc->lastCue,
                     sc->rows == 1 ? tr("1 row") : tr("%1 rows").arg(sc->rows));
        case Qt::ToolTipRole:
            return sc ? tr("Desk scene \"%1\": LX %2 to %3. Click to %4 it.")
                            .arg(sc->name, sc->firstCue, sc->lastCue, shut ? tr("open") : tr("fold"))
                      : QVariant();
        case Qt::ForegroundRole: return tk.ink100;
        case Qt::FontRole: { QFont f; f.setBold(true); return f; }
        default: return {};
        }
    }

    switch (role) {
    case KindRole:   return int(row.kind);
    case ChainRole:  return size_t(index.row()) < m_chain.size() ? m_chain[size_t(index.row())] : 0;
    case SceneRole:  return -1;
    case EdgeRole:   { const QColor c = rowEdge(index.row()); return c.isValid() ? QVariant(c) : QVariant(); }
    case LinkedRole: return linked;
    case Qt::BackgroundRole: {
        const QColor c = rowTint(index.row());
        return c.isValid() ? QVariant(c) : QVariant();
    }
    case Qt::TextAlignmentRole:
        return QVariant::fromValue(Qt::Alignment(Qt::AlignLeft | Qt::AlignVCenter));
    case Qt::ToolTipRole: {
        switch (index.column()) {
        case ColState: {
            const QString s = stateText(index.row());
            return s.isEmpty() ? QVariant() : QVariant(tr("STANDBY: quewi's GO fires this next · PLAYING: quewi cue running · "
                                                          "LX LIVE: running on the desk · LX NEXT: what GO Lights fires · "
                                                          "⚠: not on the desk"));
        }
        case ColNumber:
        case ColCue:
            if (q && row.kind == m::Row::Kind::Quewi)
                return tr("%1 cue %2. Double-click to make it the standby. Drag it onto a lighting cue "
                          "to line that cue up with it.").arg(q->type, q->number);
            if (row.kind == m::Row::Kind::Hit)
                return tr("A lighting cue fired part-way through quewi cue %1's song.").arg(qNum);
            return tr("A lighting cue on its own row. Drag a quewi cue onto it to line them up.");
        case ColLights:
        case ColPlaced: {
            QStringList t;
            for (const auto &c : row.desk) t << matrixDeskName(c.cue) + QStringLiteral(": ") + howTip(c, qNum);
            return t.isEmpty() ? QVariant(tr("No lighting cue on this row. Drag one onto it to line it up."))
                               : QVariant(t.join(QLatin1Char('\n')));
        }
        case ColTime:  return tr("Desk time: up / down, follow (F) and hang (H), in seconds.");
        case ColNotes: return data(index, Qt::DisplayRole);
        }
        return {};
    }
    case Qt::ForegroundRole:
        switch (index.column()) {
        case ColState: {
            const QString s = stateText(index.row());
            if (s.contains(tr("STANDBY"))) return rc.standby;
            if (s.contains(tr("PLAYING"))) return rc.running;
            if (s.contains(QStringLiteral("⚠"))) return tk.err;
            return rc.desk;
        }
        case ColNumber:
        case ColCue:
            if (row.kind == m::Row::Kind::Hit) return rc.hits;
            return {};
        case ColLights:
            for (const auto &c : row.desk) if (c.missing) return tk.err;
            return row.kind == m::Row::Kind::Hit ? rc.hits : rc.desk;
        case ColPlaced:
            for (const auto &c : row.desk) if (c.missing) return tk.err;
            return linked ? tk.ink100 : tk.ink60;
        case ColTime:
        case ColNotes:
            return row.kind == m::Row::Kind::Quewi ? QVariant() : QVariant(tk.ink60);
        }
        return {};
    case Qt::FontRole: {
        QFont f;
        if (index.column() == ColState) { f.setBold(true); f.setPointSizeF(f.pointSizeF() * 0.9); return f; }
        if (row.kind == m::Row::Kind::Quewi && q && q->id == m_live.standby) { f.setBold(true); return f; }
        if (index.column() == ColPlaced && linked) { f.setBold(true); return f; }
        if (index.column() == ColNumber && row.kind == m::Row::Kind::Quewi) { f.setBold(true); return f; }
        return {};
    }
    case Qt::DisplayRole:
        break;
    default:
        return {};
    }

    switch (index.column()) {
    case ColState:
        return stateText(index.row());
    case ColNumber:
        return row.kind == m::Row::Kind::Quewi && q ? q->number : QString();
    case ColCue:
        if (row.kind == m::Row::Kind::Quewi && q) return q->name;
        if (row.kind == m::Row::Kind::Hit && !row.desk.empty()) {
            const auto &c = row.desk.front();
            QString t = tr("└─  hit at %1").arg(clockText(c.atSeconds));
            if (!c.triggerName.isEmpty()) t += QStringLiteral("  ·  ") + c.triggerName;
            return t;
        }
        return QString();
    case ColLights:
        return lightsText(index.row());
    case ColTime: {
        QStringList lines;
        for (const auto &c : row.desk) lines << cellTime(c);
        return lines.join(QLatin1Char('\n')).trimmed();
    }
    case ColPlaced: {
        QStringList lines;
        for (const auto &c : row.desk) lines << placedText(c, qNum);
        return lines.join(QLatin1Char('\n'));
    }
    case ColNotes: {
        QStringList parts;
        if (q && row.kind == m::Row::Kind::Quewi && !q->notes.isEmpty()) parts << q->notes.simplified();
        for (const auto &c : row.desk)
            if (!c.cue.notes.isEmpty()) parts << c.cue.notes.simplified();
        return parts.join(QStringLiteral("  ·  "));
    }
    }
    return {};
}

Qt::ItemFlags MatrixTableModel::flags(const QModelIndex &index) const
{
    Qt::ItemFlags f = QAbstractTableModel::flags(index);
    if (m_locked) return f;
    if (!index.isValid()) return f | Qt::ItemIsDropEnabled;
    f |= Qt::ItemIsDropEnabled;
    const auto &row = m_build.result.rows[size_t(index.row())];
    if (row.kind == m::Row::Kind::Scene) return f;
    if (!row.desk.empty() || row.kind == m::Row::Kind::Quewi) f |= Qt::ItemIsDragEnabled;
    return f;
}

QStringList MatrixTableModel::mimeTypes() const
{
    return {QString::fromLatin1(kDeskMime), QString::fromLatin1(kQuewiMime)};
}

Qt::DropActions MatrixTableModel::supportedDropActions() const { return Qt::MoveAction | Qt::CopyAction; }
Qt::DropActions MatrixTableModel::supportedDragActions() const { return Qt::MoveAction; }

QMimeData *MatrixTableModel::mimeData(const QModelIndexList &indexes) const
{
    QSet<int> rows;
    for (const auto &i : indexes) rows.insert(i.row());
    QList<int> sorted(rows.begin(), rows.end());
    std::sort(sorted.begin(), sorted.end());
    if (sorted.isEmpty()) return nullptr;
    // Dragging a quewi row carries that quewi cue (to line lighting cues up
    // with it); dragging lighting rows carries their desk cues.
    const auto &first = m_build.result.rows[size_t(sorted.front())];
    if (sorted.size() == 1 && first.kind == m::Row::Kind::Quewi && first.quewiIndex >= 0 && first.desk.empty()) {
        auto *md = new QMimeData;
        md->setData(QString::fromLatin1(kQuewiMime),
                    m_build.quewi[size_t(first.quewiIndex)].id.toString().toUtf8());
        return md;
    }
    QJsonArray cues;
    for (int r : sorted) {
        if (r < 0 || r >= rowCount()) continue;
        for (const auto &c : m_build.result.rows[size_t(r)].desk) cues.append(c.cue.toJson());
    }
    auto *md = new QMimeData;
    if (!cues.isEmpty())
        md->setData(QString::fromLatin1(kDeskMime), QJsonDocument(cues).toJson(QJsonDocument::Compact));
    // A quewi row that also has lighting: either kind of drop makes sense.
    if (sorted.size() == 1 && first.kind == m::Row::Kind::Quewi && first.quewiIndex >= 0)
        md->setData(QString::fromLatin1(kQuewiMime),
                    m_build.quewi[size_t(first.quewiIndex)].id.toString().toUtf8());
    return md;
}

bool MatrixTableModel::dropTarget(int row, bool onto, m::ManualPlacement::Mode *mode, QUuid *anchor) const
{
    const int n = rowCount();
    *anchor = {};
    if (onto && row >= 0 && row < n) {
        const auto &r = m_build.result.rows[size_t(row)];
        if (r.kind == m::Row::Kind::Quewi && r.quewiIndex >= 0) {
            *mode = m::ManualPlacement::Mode::With;
            *anchor = m_build.quewi[size_t(r.quewiIndex)].id;
            return true;
        }
        ++row;                      // onto a lighting row = just after it
    }
    if (row < 0 || row > n) row = n;
    const int q = row <= 0 ? -1 : m_build.result.rows[size_t(row - 1)].quewiIndex;
    if (q < 0) {
        *mode = m::ManualPlacement::Mode::Start;
        return true;
    }
    *mode = m::ManualPlacement::Mode::After;
    *anchor = m_build.quewi[size_t(q)].id;
    return true;
}

std::vector<m::DeskCue> MatrixTableModel::lightsAt(int row, QString *why) const
{
    if (row < 0 || row >= rowCount()) {
        if (why) *why = tr("Drop a quewi cue onto a lighting cue's row to line them up.");
        return {};
    }
    const auto &r = m_build.result.rows[size_t(row)];
    if (r.kind == m::Row::Kind::Scene) {
        if (why) *why = tr("That's a scene heading. Drop a quewi cue onto a lighting cue's row to line them up.");
        return {};
    }
    if (r.kind == m::Row::Kind::Quewi) {
        if (why) *why = tr("quewi's cue order is GO order, so the matrix won't move quewi cues. "
                           "Drop it onto a lighting cue's row instead, or reorder cues in the cue list.");
        return {};
    }
    std::vector<m::DeskCue> out;
    for (const auto &c : r.desk) out.push_back(c.cue);
    return out;
}

bool MatrixTableModel::canDropMimeData(const QMimeData *data, Qt::DropAction, int row, int,
                                       const QModelIndex &parent) const
{
    if (m_locked || !data || !m_list) return false;
    if (data->hasFormat(QString::fromLatin1(kDeskMime))) return true;
    if (data->hasFormat(QString::fromLatin1(kQuewiMime)))
        return parent.isValid() && !lightsAt(parent.row()).empty();
    Q_UNUSED(row);
    return false;
}

bool MatrixTableModel::dropMimeData(const QMimeData *data, Qt::DropAction action, int row,
                                    int column, const QModelIndex &parent)
{
    if (m_locked || !data || !m_list) return false;
    const bool onto = parent.isValid();
    const int at = onto ? parent.row() : row;

    // A quewi cue onto a lighting row: the lighting moves to the quewi cue.
    // (Preferred when the drag carries both and lands on a lighting row.)
    if (data->hasFormat(QString::fromLatin1(kQuewiMime))) {
        const QUuid q(QString::fromUtf8(data->data(QString::fromLatin1(kQuewiMime))));
        QString why;
        const auto lights = onto ? lightsAt(at, &why) : std::vector<m::DeskCue>{};
        if (!lights.empty()) {
            QStringList names;
            for (const auto &c : lights) names << matrixDeskName(c);
            place(lights, m::ManualPlacement::Mode::With, q,
                  tr("Line up %1 with Q%2").arg(names.join(QStringLiteral(", ")), quewiNumber(q)));
            return false;
        }
        if (!data->hasFormat(QString::fromLatin1(kDeskMime))) {
            emit refused(why.isEmpty() ? tr("Drop a quewi cue onto a lighting cue's row to line them up.") : why);
            return false;
        }
    }
    if (!canDropMimeData(data, action, row, column, parent)) return false;
    std::vector<m::DeskCue> cues;
    for (const auto v : QJsonDocument::fromJson(data->data(QString::fromLatin1(kDeskMime))).array())
        cues.push_back(m::DeskCue::fromJson(v.toObject()));
    if (cues.empty()) return false;
    m::ManualPlacement::Mode mode;
    QUuid anchor;
    if (!dropTarget(at, onto, &mode, &anchor)) return false;
    place(cues, mode, anchor);
    // false: the view mustn't try to remove the dragged rows — the drop
    // re-places cues, it doesn't move model rows.
    return false;
}

QString MatrixTableModel::quewiNumber(const QUuid &id) const
{
    for (const auto &q : m_build.quewi)
        if (q.id == id) return q.number;
    return QStringLiteral("?");
}

void MatrixTableModel::commit(const m::Config &next, const QString &text)
{
    if (!m_list) return;
    const auto before = m_list->matrixConfig().placements;
    if (before == next.placements) return;
    if (m_undo) {
        m_undo->push(new core::SetMatrixPlacementsCommand(m_list, before, next.placements, text));
    } else {
        auto cfg = m_list->matrixConfig();
        cfg.placements = next.placements;
        m_list->setMatrixConfig(cfg);
    }
    emit placementsEdited();
}

void MatrixTableModel::place(const std::vector<m::DeskCue> &cues, m::ManualPlacement::Mode mode,
                             const QUuid &anchor, const QString &text)
{
    if (!m_list || cues.empty()) return;
    auto cfg = m_list->matrixConfig();
    for (const auto &c : cues) cfg.place(c, mode, anchor);
    QString t = text;
    if (t.isEmpty()) {
        QStringList names;
        for (const auto &c : cues) names << matrixDeskName(c);
        const QString n = names.join(QStringLiteral(", "));
        t = mode == m::ManualPlacement::Mode::With  ? tr("Line up %1 with Q%2").arg(n, quewiNumber(anchor))
          : mode == m::ManualPlacement::Mode::After ? tr("Put %1 after Q%2").arg(n, quewiNumber(anchor))
                                                    : tr("Put %1 at the top").arg(n);
    }
    commit(cfg, t);
}

void MatrixTableModel::lineUp(const m::DeskCue &cue, const QUuid &quewiCue)
{
    place({cue}, m::ManualPlacement::Mode::With, quewiCue);
}

void MatrixTableModel::unlink(const std::vector<m::DeskCue> &cues)
{
    if (!m_list || cues.empty()) return;
    auto cfg = m_list->matrixConfig();
    QStringList names;
    for (const auto &c : cues) {
        cfg.unplace(c.list, c.number, c.part);
        names << matrixDeskName(c);
    }
    commit(cfg, tr("Unlink %1").arg(names.join(QStringLiteral(", "))));
}

// ── MatrixRowDelegate ───────────────────────────────────────────────────

void MatrixRowDelegate::paint(QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &index) const
{
    const auto &tk = Theme::tokens();
    const auto rc = showRegionColours();
    if (index.data(MatrixTableModel::KindRole).toInt() == int(m::Row::Kind::Scene)) {
        // A scene: a band across the table, like the desk's own cue list.
        const QRect r = opt.rect;
        p->fillRect(r, showMix(tk.bgPanel, tk.bgDeep, 0.65));
        p->fillRect(QRect(r.left(), r.top(), r.width(), 2), showMix(tk.bgPanel, rc.desk, 0.6));
        QFont f = opt.font;
        f.setBold(true);
        f.setLetterSpacing(QFont::PercentageSpacing, 104);
        p->setFont(f);
        p->setPen(tk.ink100);
        p->drawText(r.adjusted(14, 2, -10, 0), Qt::AlignVCenter | Qt::AlignLeft,
                    index.data(Qt::DisplayRole).toString());
        return;
    }
    QStyleOptionViewItem o(opt);
    initStyleOption(&o, index);
    // The row tint fills the whole row, selected or not, so the region colour
    // stays readable on the selected row too.
    const QVariant bg = index.data(Qt::BackgroundRole);
    if (bg.isValid()) {
        p->fillRect(o.rect, bg.value<QColor>());
        if (o.state & QStyle::State_Selected) {
            QColor sel = Theme::tokens().bgRowSelected;
            sel.setAlpha(110);
            p->fillRect(o.rect, sel);
            o.state &= ~QStyle::State_Selected;
        }
        o.backgroundBrush = Qt::NoBrush;
    }
    // No per-cell focus box: the row is the unit here.
    o.state &= ~QStyle::State_HasFocus;
    o.rect.adjust(4, 0, -2, 0);             // breathing room either side
    const bool lightsCol = index.column() == MatrixTableModel::ColLights;
    if (lightsCol) o.rect.adjust(14, 0, 0, 0);   // room for the follow line
    QStyledItemDelegate::paint(p, o, index);
    // Auto-follow chains: a line joining cues that run on from each other.
    if (lightsCol) {
        const int bits = index.data(MatrixTableModel::ChainRole).toInt();
        if (bits) {
            const QColor line = rc.desk;
            const int x = opt.rect.left() + 9;
            const int mid = opt.rect.top() + std::min(opt.rect.height() / 2, 19);
            if (bits & MatrixTableModel::ChainTop) p->fillRect(QRect(x - 1, opt.rect.top(), 2, mid - opt.rect.top()), line);
            if (bits & MatrixTableModel::ChainBottom) p->fillRect(QRect(x - 1, mid, 2, opt.rect.bottom() - mid + 1), line);
            if (bits & MatrixTableModel::ChainMember) {
                p->save();
                p->setRenderHint(QPainter::Antialiasing);
                p->setPen(Qt::NoPen);
                p->setBrush(line);
                p->drawEllipse(QPointF(x, mid), 3.5, 3.5);
                p->restore();
            }
        }
    }
    // The region colour down the left edge of the row (like Show Mode's cards).
    if (index.column() == 0) {
        const QVariant edge = index.data(MatrixTableModel::EdgeRole);
        if (edge.isValid()) p->fillRect(QRect(opt.rect.left(), opt.rect.top(), 5, opt.rect.height()),
                                        edge.value<QColor>());
    }
}

QSize MatrixRowDelegate::sizeHint(const QStyleOptionViewItem &opt, const QModelIndex &index) const
{
    if (index.data(MatrixTableModel::KindRole).toInt() == int(m::Row::Kind::Scene))
        return QSize(40, 34);
    QSize s = QStyledItemDelegate::sizeHint(opt, index);
    if (index.column() == MatrixTableModel::ColLights) s.rwidth() += 14;
    s.rheight() = std::max(s.height() + 12, 38);
    s.rwidth() += 12;
    return s;
}

// ── MatrixView ──────────────────────────────────────────────────────────

MatrixView::MatrixView(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("matrixView"));
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(14, 12, 14, 10);
    root->setSpacing(8);

    // ── Header: title + what it's showing, GO Lights on the right ──
    auto *head = new QHBoxLayout;
    head->setSpacing(16);
    auto *headText = new QVBoxLayout;
    headText->setSpacing(4);
    m_title = new QLabel(this);
    m_title->setObjectName(QStringLiteral("matrixTitle"));
    m_title->setTextFormat(Qt::RichText);
    m_title->setToolTip(tr("A Matrix List: quewi's sound and video cues and the lighting desk's cues "
                           "in one running order. Nothing here changes quewi's cue order or the desk's cues."));
    headText->addWidget(m_title);
    m_summary = new QLabel(this);
    m_summary->setObjectName(QStringLiteral("matrixSummary"));
    m_summary->setTextFormat(Qt::RichText);
    m_summary->setWordWrap(true);
    headText->addWidget(m_summary);
    head->addLayout(headText, 1);

    // The lights' transport: GO Lights (the obvious one), then Back and Stop,
    // smaller and neutral, on their own line below it.
    auto *goCol = new QVBoxLayout;
    goCol->setSpacing(5);
    m_goLights = new QPushButton(tr("GO Lights"), this);
    m_goLights->setObjectName(QStringLiteral("matrixGoLights"));
    m_goLights->setFocusPolicy(Qt::NoFocus);      // Space stays quewi's GO
    m_goLights->setCursor(Qt::PointingHandCursor);
    m_goLights->setFixedWidth(300);
    goCol->addWidget(m_goLights);
    auto *small = new QHBoxLayout;
    small->setSpacing(6);
    m_lightsBack = new QPushButton(tr("◀ Back"), this);
    m_lightsBack->setObjectName(QStringLiteral("matrixLightsBack"));
    m_lightsBack->setFocusPolicy(Qt::NoFocus);
    m_lightsStop = new QPushButton(tr("■ Stop"), this);
    m_lightsStop->setObjectName(QStringLiteral("matrixLightsStop"));
    m_lightsStop->setFocusPolicy(Qt::NoFocus);
    m_lightsStop->setFixedWidth(92);
    small->addWidget(m_lightsBack, 1);
    small->addWidget(m_lightsStop, 0);
    goCol->addLayout(small);
    m_goLightsHint = new QLabel(this);
    m_goLightsHint->setObjectName(QStringLiteral("matrixGoLightsHint"));
    m_goLightsHint->setAlignment(Qt::AlignCenter);
    m_goLightsHint->setWordWrap(true);
    m_goLightsHint->setFixedWidth(300);
    goCol->addWidget(m_goLightsHint);
    head->addLayout(goCol, 0);
    head->setAlignment(goCol, Qt::AlignTop);
    root->addLayout(head);

    // ── Settings ──
    auto *bar = new QHBoxLayout;
    bar->setSpacing(8);
    auto *srcLabel = new QLabel(tr("Cue list"), this);
    srcLabel->setObjectName(QStringLiteral("matrixFieldLabel"));
    bar->addWidget(srcLabel);
    m_source = new QComboBox(this);
    m_source->setObjectName(QStringLiteral("matrixSource"));
    m_source->setToolTip(tr("The quewi cue list to line up with the desk. GO runs this list while the Matrix List is up."));
    bar->addWidget(m_source);
    bar->addSpacing(12);
    auto *deskLabel = new QLabel(tr("Desk cue list"), this);
    deskLabel->setObjectName(QStringLiteral("matrixFieldLabel"));
    bar->addWidget(deskLabel);
    m_deskList = new QComboBox(this);
    m_deskList->setObjectName(QStringLiteral("matrixDeskList"));
    m_deskList->setEditable(true);
    m_deskList->setMinimumContentsLength(8);
    m_deskList->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    m_deskList->setMaximumWidth(280);
    m_deskList->setToolTip(tr("The desk's cue list to read. The menu lists the desk's cue lists once it's "
                              "connected; you can also type a number."));
    bar->addWidget(m_deskList);
    m_refresh = new QPushButton(tr("Read the desk again"), this);
    m_refresh->setObjectName(QStringLiteral("matrixRefresh"));
    m_refresh->setToolTip(tr("Ask the desk for this cue list again (quewi also re-reads cues the desk says changed)."));
    bar->addWidget(m_refresh);
    bar->addSpacing(12);
    m_details = new QCheckBox(tr("Details"), this);
    m_details->setObjectName(QStringLiteral("matrixDetails"));
    m_details->setToolTip(tr("Show everything the desk records for each lighting cue (times, mark, block, "
                             "effects, macros…). Double-click one lighting row to open just that one."));
    bar->addWidget(m_details);
    m_collapseAll = new QPushButton(tr("Fold scenes"), this);
    m_collapseAll->setObjectName(QStringLiteral("matrixCollapseAll"));
    m_collapseAll->setToolTip(tr("Fold every desk scene down to its heading. Click a heading to open it."));
    bar->addWidget(m_collapseAll);
    m_expandAll = new QPushButton(tr("Open scenes"), this);
    m_expandAll->setObjectName(QStringLiteral("matrixExpandAll"));
    m_expandAll->setToolTip(tr("Open every desk scene."));
    bar->addWidget(m_expandAll);
    bar->addStretch(1);
    m_follow = new QCheckBox(tr("Follow the show"), this);
    m_follow->setObjectName(QStringLiteral("matrixFollow"));
    m_follow->setChecked(true);
    m_follow->setToolTip(tr("Keep standby (or the desk's live cue) in view as the show runs."));
    bar->addWidget(m_follow);
    root->addLayout(bar);

    // ── Key to the colours and marks ──
    m_legend = new QLabel(this);
    m_legend->setObjectName(QStringLiteral("matrixLegend"));
    m_legend->setTextFormat(Qt::RichText);
    m_legend->setWordWrap(true);
    m_legend->setToolTip(tr("Drag a lighting cue onto a quewi cue — or a quewi cue onto a lighting cue — "
                            "to line them up. Right-click a row for Line up with… and Unlink. Ctrl+Z undoes."));
    root->addWidget(m_legend);

    // ── A banner when the desk isn't live ──
    m_bannerRow = new QWidget(this);
    m_bannerRow->setObjectName(QStringLiteral("matrixBannerRow"));
    {
        auto *h = new QHBoxLayout(m_bannerRow);
        h->setContentsMargins(12, 8, 8, 8);
        h->setSpacing(10);
        m_banner = new QLabel(m_bannerRow);
        m_banner->setObjectName(QStringLiteral("matrixBanner"));
        m_banner->setWordWrap(true);
        h->addWidget(m_banner, 1);
        m_bannerDesk = new QPushButton(tr("Lighting Desk…"), m_bannerRow);
        m_bannerDesk->setObjectName(QStringLiteral("matrixBannerDesk"));
        m_bannerDesk->setToolTip(tr("Set up the lighting desk quewi reads (Tools → Lighting Desk…)."));
        h->addWidget(m_bannerDesk);
        connect(m_bannerDesk, &QPushButton::clicked, this, &MatrixView::deskSettingsRequested);
    }
    root->addWidget(m_bannerRow);

    // ── The table, or what to do when there's nothing to show ──
    m_body = new QStackedWidget(this);
    m_model = new MatrixTableModel(this);
    m_table = new QTableView(m_body);
    m_table->setObjectName(QStringLiteral("matrixTable"));
    m_table->setModel(m_model);
    m_table->setItemDelegate(new MatrixRowDelegate(m_table));
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_table->setDragEnabled(true);
    m_table->setAcceptDrops(true);
    m_table->setDropIndicatorShown(true);
    m_table->setDragDropMode(QAbstractItemView::DragDrop);
    m_table->setDefaultDropAction(Qt::MoveAction);
    m_table->setDragDropOverwriteMode(false);
    m_table->setWordWrap(true);
    m_table->setShowGrid(false);
    m_table->setAlternatingRowColors(false);
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    // Per-pixel, three rows a wheel notch (the app's SmoothScroll glides it;
    // per-item it moved ~60 rows a notch). Touchpads scroll by their pixels.
    m_table->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_table->setProperty("smoothScrollRows", 3);
    m_table->verticalScrollBar()->setSingleStep(20);
    m_table->verticalHeader()->setVisible(false);
    m_table->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_table->verticalHeader()->setMinimumSectionSize(38);
    auto *hh = m_table->horizontalHeader();
    hh->setSectionResizeMode(QHeaderView::Interactive);
    hh->setStretchLastSection(true);
    hh->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    hh->setHighlightSections(false);
    m_table->setColumnWidth(MatrixTableModel::ColState, 116);
    m_table->setColumnWidth(MatrixTableModel::ColNumber, 52);
    m_table->setColumnWidth(MatrixTableModel::ColTime, 100);
    m_table->setColumnWidth(MatrixTableModel::ColPlaced, 180);
    // The three text columns share whatever width there is, so a narrow
    // window never scrolls sideways or cuts the notes off.
    for (int c : {int(MatrixTableModel::ColCue), int(MatrixTableModel::ColLights), int(MatrixTableModel::ColNotes)})
        hh->setSectionResizeMode(c, QHeaderView::Stretch);
    m_body->addWidget(m_table);

    auto *empty = new QWidget(m_body);
    empty->setObjectName(QStringLiteral("matrixEmpty"));
    {
        auto *v = new QVBoxLayout(empty);
        v->addStretch(1);
        m_emptyTitle = new QLabel(empty);
        m_emptyTitle->setObjectName(QStringLiteral("matrixEmptyTitle"));
        m_emptyTitle->setAlignment(Qt::AlignCenter);
        v->addWidget(m_emptyTitle);
        m_emptyText = new QLabel(empty);
        m_emptyText->setObjectName(QStringLiteral("matrixEmptyText"));
        m_emptyText->setAlignment(Qt::AlignCenter);
        m_emptyText->setWordWrap(true);
        m_emptyText->setMaximumWidth(620);
        m_emptyText->setMinimumWidth(480);
        auto *row = new QHBoxLayout;
        row->addStretch(1);
        row->addWidget(m_emptyText, 0);
        row->addStretch(1);
        v->addLayout(row);
        auto *h = new QHBoxLayout;
        h->addStretch(1);
        m_emptyDesk = new QPushButton(tr("Set up the lighting desk…"), empty);
        m_emptyDesk->setObjectName(QStringLiteral("matrixEmptyDesk"));
        h->addWidget(m_emptyDesk);
        h->addStretch(1);
        v->addLayout(h);
        v->addStretch(2);
        connect(m_emptyDesk, &QPushButton::clicked, this, &MatrixView::deskSettingsRequested);
    }
    m_body->addWidget(empty);
    root->addWidget(m_body, 1);

    m_rebuildTimer = new QTimer(this);
    m_rebuildTimer->setSingleShot(true);
    m_rebuildTimer->setInterval(30);
    connect(m_rebuildTimer, &QTimer::timeout, this, &MatrixView::rebuildNow);
    m_liveTimer = new QTimer(this);
    m_liveTimer->setInterval(250);
    connect(m_liveTimer, &QTimer::timeout, this, &MatrixView::pollLive);

    connect(m_model, &MatrixTableModel::placementsEdited, this, [this] {
        // On the undo stack when there is one (that marks the show unsaved);
        // without one, say the show changed.
        if (!m_ws) emit modified();
        emit statusMessage(tr("Lined up — saved with the show (Ctrl+Z undoes)"));
    });
    connect(m_model, &MatrixTableModel::refused, this, &MatrixView::statusMessage);
    connect(m_table, &QTableView::doubleClicked, this, [this](const QModelIndex &i) {
        if (!i.isValid()) return;
        const auto &row = build().result.rows[size_t(i.row())];
        if (row.kind == m::Row::Kind::Quewi && row.quewiIndex >= 0)
            emit standbyRequested(build().quewi[size_t(row.quewiIndex)].id);
        else if (!row.desk.empty())
            m_model->toggleRowDetails(i.row());
    });
    connect(m_table, &QWidget::customContextMenuRequested, this, &MatrixView::contextMenuAt);
    connect(m_goLights, &QPushButton::clicked, this, &MatrixView::goLightsRequested);
    connect(m_lightsBack, &QPushButton::clicked, this, &MatrixView::lightsBackRequested);
    connect(m_lightsStop, &QPushButton::clicked, this, &MatrixView::lightsStopRequested);
    {
        QSettings s(QStringLiteral("ServeGaming"), QStringLiteral("quewi"));
        m_details->setChecked(s.value(QStringLiteral("matrix/details"), false).toBool());
        m_model->setDetails(m_details->isChecked());
    }
    connect(m_details, &QCheckBox::toggled, this, [this](bool on) {
        m_model->setDetails(on);
        QSettings(QStringLiteral("ServeGaming"), QStringLiteral("quewi")).setValue(QStringLiteral("matrix/details"), on);
        applyCollapse();
    });
    connect(m_collapseAll, &QPushButton::clicked, this, [this] { setAllScenesCollapsed(true); });
    connect(m_expandAll, &QPushButton::clicked, this, [this] { setAllScenesCollapsed(false); });
    connect(m_table, &QTableView::clicked, this, [this](const QModelIndex &i) {
        if (!i.isValid()) return;
        const int sc = i.data(MatrixTableModel::SceneRole).toInt();
        if (sc >= 0) toggleScene(sc);
    });
    connect(m_source, &QComboBox::activated, this, [this](int i) {
        if (m_updatingCombos) return;
        const QUuid id = m_source->itemData(i).toUuid();
        editConfig([id](m::Config &c) { c.sourceList = id; });
    });
    auto deskPicked = [this] {
        if (m_updatingCombos) return;
        QString n = m_deskList->currentData().toString();
        if (n.isEmpty() || m_deskList->currentText() != m_deskList->itemText(m_deskList->currentIndex()))
            n = m_deskList->currentText().section(QLatin1Char(' '), 0, 0).trimmed();
        n = m::normalNumber(n);
        bool ok = false;
        n.toDouble(&ok);
        if (!ok) { refreshCombos(); return; }
        editConfig([n](m::Config &c) { c.deskList = n; });
        watchDeskList();
    };
    connect(m_deskList, &QComboBox::activated, this, deskPicked);
    connect(m_deskList->lineEdit(), &QLineEdit::editingFinished, this, deskPicked);
    connect(m_refresh, &QPushButton::clicked, this, [this] {
        if (!m_reader || !m_list) return;
        m_reader->refresh();
        m_reader->refreshList(m::normalNumber(m_list->matrixConfig().deskList));
        updateStatus();
    });
    restyle();
    updateStatus();
}

MatrixView::~MatrixView() = default;

void MatrixView::restyle()
{
    const auto &tk = Theme::tokens();
    const auto rc = showRegionColours();
    auto sw = [](const QColor &c, const QString &glyph = QStringLiteral("■")) {
        return QStringLiteral("<span style=\"color:%1\">%2</span>").arg(c.name(), glyph);
    };
    const QString sep = QStringLiteral("&nbsp;&nbsp;&nbsp;&nbsp;");
    m_legend->setText(
        QStringLiteral("<span style=\"color:%1\">").arg(tk.ink60.name())
        + sw(rc.standby) + tr(" Standby (GO fires next)") + sep
        + sw(rc.running) + tr(" Playing") + sep
        + sw(rc.desk) + tr(" LX LIVE on the desk") + sep
        + sw(showMix(tk.bgPanel, rc.desk, 0.45)) + tr(" LX NEXT (GO Lights fires it)") + sep
        + QStringLiteral("<b style=\"color:%1\">◆</b>").arg(tk.ink100.name()) + tr(" Lined up by hand") + sep
        + tr("plain: lined up automatically") + sep
        + sw(tk.err, QStringLiteral("⚠")) + tr(" Not on the desk") + sep
        + sw(rc.hits, QStringLiteral("└─")) + tr(" Hit inside a song")
        + QStringLiteral("</span>"));

    setStyleSheet(QStringLiteral(
        "QLabel#matrixTitle { color:%1; font-size:18px; font-weight:600; }"
        "QLabel#matrixSummary { color:%2; font-size:13px; }"
        "QLabel#matrixFieldLabel { color:%2; }"
        "QLabel#matrixLegend { color:%2; font-size:12px; padding:6px 10px; background:%3;"
        "  border:1px solid %4; border-radius:4px; }"
        "QWidget#matrixBannerRow { background:%5; border:1px solid %6; border-radius:4px; }"
        "QLabel#matrixBanner { color:%1; font-size:13px; }"
        "QLabel#matrixEmptyTitle { color:%1; font-size:20px; font-weight:600; }"
        "QLabel#matrixEmptyText { color:%2; font-size:14px; }"
        "QPushButton#matrixGoLights { background:%7; color:%8; border:1px solid transparent; border-radius:6px;"
        "  font-size:17px; font-weight:700; letter-spacing:0.03em; padding:10px 18px; min-height:40px; }"
        "QPushButton#matrixGoLights:pressed { background:%9; }"
        "QPushButton#matrixGoLights:disabled { background:%3; color:%10; border-color:%4; }"
        "QLabel#matrixGoLightsHint { color:%10; font-size:11px; }"
        "QPushButton#matrixLightsBack, QPushButton#matrixLightsStop { font-size:12px; padding:4px 10px; min-height:22px; }")
        .arg(tk.ink100.name(), tk.ink60.name(), tk.bgPanel.name(), tk.divider.name(),
             showMix(tk.bgPanel, tk.warn, 0.14).name(), showMix(tk.bgPanel, tk.warn, 0.55).name(),
             rc.desk.name(), tk.bgDeep.name(), tk.accentSoft.name())
        .arg(tk.ink40.name()));
}

void MatrixView::setWorkspace(core::Workspace *ws)
{
    if (m_ws == ws) return;
    if (m_ws) m_ws->disconnect(this);
    m_ws = ws;
    if (m_ws) {
        connect(m_ws, &core::Workspace::cueListsChanged, this, [this] {
            refreshCombos();
            scheduleRebuild();
        });
        setUndoStack(m_ws->undoStack());
    }
    setCueList(nullptr);
}

void MatrixView::setUndoStack(QUndoStack *stack)
{
    m_model->setUndoStack(stack);
}

void MatrixView::setCueList(core::CueList *matrix)
{
    if (m_list) m_list->disconnect(this);
    m_list = matrix;
    m_model->setMatrixList(matrix);
    if (m_list)
        connect(m_list, &core::CueList::matrixConfigChanged, this, [this] {
            refreshCombos();
            scheduleRebuild();
        });
    m_lastStandbyRow = m_lastDeskRow = -2;
    watchDeskList();
    refreshCombos();
    rebuildNow();
}

void MatrixView::setDesk(osc::EosCueLists *reader, osc::EosFeedback *feedback)
{
    if (m_reader) m_reader->disconnect(this);
    if (m_feedback) m_feedback->disconnect(this);
    m_reader = reader;
    m_feedback = feedback;
    if (m_reader) {
        connect(m_reader, &osc::EosCueLists::cuesChanged, this, &MatrixView::onDeskCues);
        connect(m_reader, &osc::EosCueLists::stateChanged, this, [this](const QString &l) {
            if (m_list && l == m::normalNumber(m_list->matrixConfig().deskList)) {
                updateStatus();
                scheduleRebuild();
            }
        });
        connect(m_reader, &osc::EosCueLists::cueListsChanged, this, &MatrixView::refreshCombos);
    }
    if (m_feedback) {
        connect(m_feedback, &osc::EosFeedback::linkChanged, this, &MatrixView::updateStatus);
        connect(m_feedback, &osc::EosFeedback::stateChanged, this, &MatrixView::pollLive);
    }
    watchDeskList();
    refreshCombos();
    rebuildNow();
}

void MatrixView::setDeskInfo(bool eosDesk, const QString &deskName)
{
    if (eosDesk == m_eosDesk && deskName == m_deskName) return;
    m_eosDesk = eosDesk;
    m_deskName = deskName;
    updateStatus();
}

void MatrixView::setLocked(bool locked)
{
    m_locked = locked;
    m_model->setLocked(locked);
    m_source->setEnabled(!locked);
    m_deskList->setEnabled(!locked);
    m_table->setDragEnabled(!locked);
    m_table->setAcceptDrops(!locked);
}

void MatrixView::watchDeskList()
{
    if (!m_reader || !m_list) return;
    const QString l = m::normalNumber(m_list->matrixConfig().deskList);
    QStringList w = m_reader->watched();
    if (!w.contains(l)) {
        w << l;
        m_reader->setWatched(w);
    }
}

void MatrixView::editConfig(const std::function<void(m::Config &)> &fn)
{
    if (!m_list) return;
    auto cfg = m_list->matrixConfig();
    fn(cfg);
    if (cfg == m_list->matrixConfig()) return;
    m_list->setMatrixConfig(cfg);
    emit modified();
}

void MatrixView::refreshCombos()
{
    m_updatingCombos = true;
    m_source->clear();
    const auto cfg = m_list ? m_list->matrixConfig() : m::Config{};
    auto *src = matrixSourceList(m_ws, m_list);
    if (m_ws)
        for (const auto &l : m_ws->cueLists()) {
            if (l->kind() != core::CueList::Kind::Normal) continue;
            m_source->addItem(l->name(), l->id());
            if (l.get() == src) m_source->setCurrentIndex(m_source->count() - 1);
        }

    m_deskList->clear();
    const QString want = m::normalNumber(cfg.deskList);
    bool found = false;
    if (m_reader)
        for (const auto &info : m_reader->cueLists()) {
            const QString text = info.label.isEmpty()
                ? info.number : QStringLiteral("%1 — %2").arg(info.number, info.label);
            m_deskList->addItem(text, info.number);
            if (info.number == want) {
                m_deskList->setCurrentIndex(m_deskList->count() - 1);
                found = true;
            }
        }
    if (!found) {
        m_deskList->addItem(want, want);
        m_deskList->setCurrentIndex(m_deskList->count() - 1);
    }
    const auto &tk = Theme::tokens();
    m_title->setText(QStringLiteral("%1&nbsp;&nbsp;<span style=\"color:%2; font-size:13px; font-weight:400\">%3</span>")
                         .arg((m_list ? m_list->name() : tr("Matrix List")).toHtmlEscaped(), tk.ink40.name(),
                              tr("Matrix List — sound, video and lights in one running order")));
    m_updatingCombos = false;
}

void MatrixView::scheduleRebuild()
{
    m_rebuildTimer->start();
}

void MatrixView::rebuildNow()
{
    m_rebuildTimer->stop();
    MatrixBuild b = buildMatrix(m_ws, m_list, m_reader);

    // The desk renumbered a cue placed by hand (same UID): follow it.
    if (m_list && !b.result.renumbered.empty()) {
        auto cfg = m_list->matrixConfig();
        for (const auto &r : b.result.renumbered)
            if (r.placement >= 0 && size_t(r.placement) < cfg.placements.size()) {
                cfg.placements[size_t(r.placement)].number = r.number;
                cfg.placements[size_t(r.placement)].part = r.part;
            }
        QSignalBlocker block(m_list);
        m_list->setMatrixConfig(cfg);
    }

    // Keep the source list's edits flowing in.
    if (m_sourceWatched != b.source) {
        for (const auto &c : std::as_const(m_listConns)) disconnect(c);
        m_listConns.clear();
        m_sourceWatched = b.source;
        if (b.source) {
            for (auto sig : {&core::CueList::cueInserted, &core::CueList::cueRemoved,
                             &core::CueList::cueChanged})
                m_listConns << connect(b.source, sig, this, &MatrixView::scheduleRebuild);
        }
    }

    // Keep the selection on the same quewi cue across a rebuild.
    QUuid keep;
    const auto cur = m_table->currentIndex();
    if (cur.isValid() && size_t(cur.row()) < build().result.rows.size()) {
        const auto &row = build().result.rows[size_t(cur.row())];
        if (row.quewiIndex >= 0 && size_t(row.quewiIndex) < build().quewi.size())
            keep = build().quewi[size_t(row.quewiIndex)].id;
    }
    m_model->setBuild(b);
    applyCollapse();
    if (!keep.isNull()) {
        const int r = b.rowOfQuewi(keep);
        if (r >= 0) m_table->setCurrentIndex(m_model->index(r, MatrixTableModel::ColCue));
    }
    m_lastStandbyRow = m_lastDeskRow = -2;
    pollLive();
    updateStatus();
}

void MatrixView::onDeskCues(const QString &list)
{
    if (!m_list || !m_reader || list != m::normalNumber(m_list->matrixConfig().deskList)) return;
    // Remember what the desk said, so the matrix still reads right offline.
    if (m_reader->state(list) == osc::EosCueLists::State::Ready) {
        auto cfg = m_list->matrixConfig();
        const auto now = toDeskCues(m_reader->cues(list));
        if (now != cfg.deskCache || !cfg.deskCachedAt.isValid()) {
            cfg.deskCache = now;
            cfg.deskCachedAt = QDateTime::currentDateTime();
            QSignalBlocker block(m_list);   // not an edit of yours: no "unsaved"
            m_list->setMatrixConfig(cfg);
        }
    }
    scheduleRebuild();
}

void MatrixView::updateStatus()
{
    const auto &tk = Theme::tokens();
    const auto rc = showRegionColours();
    if (!m_list) {
        m_summary->clear();
        m_statusText.clear();
        m_bannerRow->hide();
        return;
    }
    const auto cfg = m_list->matrixConfig();
    const QString list = m::normalNumber(cfg.deskList);
    const auto &b = build();
    using L = osc::EosFeedback::Link;
    const L link = m_feedback ? m_feedback->link() : L::Off;
    const bool linkLive = link == L::Live;
    const auto st = m_reader ? m_reader->state(list) : osc::EosCueLists::State::Idle;
    int lx = 0;
    for (const auto &c : b.deskCues) if (c.part == 0) ++lx;
    int missing = 0;
    for (const auto &r : b.result.rows) for (const auto &c : r.desk) if (c.missing) ++missing;

    // The desk list's label, when the desk told us.
    QString listLabel;
    if (m_reader)
        for (const auto &info : m_reader->cueLists())
            if (info.number == list) listLabel = info.label;
    const QString host = m_feedback ? m_feedback->host() : QString();
    const QString deskName = m_deskName.isEmpty() ? tr("ETC Eos") : m_deskName;

    // One plain sentence about the lights (also statusText(), for tests).
    QString lights;
    if (linkLive && st == osc::EosCueLists::State::Fetching) {
        const int got = m_reader->received(list), of = m_reader->expected(list);
        lights = of >= 0 ? tr("Reading desk cue list %1… %2 of %3").arg(list).arg(got).arg(of)
                         : tr("Reading desk cue list %1…").arg(list);
    } else if (b.desk == MatrixBuild::Desk::Live && st == osc::EosCueLists::State::Partial) {
        lights = tr("Read %1 of %2 cues from desk cue list %3 — the desk stopped answering. "
                    "Read the desk again to finish.").arg(m_reader->received(list)).arg(m_reader->expected(list)).arg(list);
    } else if (b.desk == MatrixBuild::Desk::Live && (linkLive || !m_feedback)) {
        lights = (lx == 1 ? tr("Live from the desk: 1 cue in cue list %1.")
                          : tr("Live from the desk: %1 cues in cue list %2.").arg(lx)).arg(list);
    } else if (b.desk != MatrixBuild::Desk::None) {
        lights = tr("The desk isn't connected — showing cue list %1 as last read (%2).")
                     .arg(list, QLocale().toString(cfg.deskCachedAt, QLocale::ShortFormat));
    } else {
        lights = tr("No lighting cues yet. Set up an ETC Eos desk in Tools → Lighting Desk… with "
                    "\"Read the desk's state back\" on, and quewi reads cue list %1 from it.").arg(list);
    }
    if (missing > 0)
        lights += QLatin1Char(' ') + (missing == 1 ? tr("1 cue marked ⚠ isn't on the desk.")
                                                   : tr("%1 cues marked ⚠ aren't on the desk.").arg(missing));
    if (!b.source) lights += QLatin1Char(' ') + tr("There's no quewi cue list to line up yet.");
    m_statusText = lights;

    // The header: "Sound & video from Main · Lights from desk list 2 'B/O' ·
    // 146 cues · ● Live from ETC Eos at 127.0.0.1".
    auto strong = [&tk](const QString &s) {
        return QStringLiteral("<b style=\"color:%1\">%2</b>").arg(tk.ink100.name(), s.toHtmlEscaped());
    };
    QStringList parts;
    parts << tr("Sound & video from %1").arg(b.source ? strong(b.source->name()) : strong(tr("(no cue list)")));
    QString lightsFrom = tr("Lights from desk list %1").arg(strong(list));
    if (!listLabel.isEmpty()) lightsFrom += QStringLiteral(" ") + strong(QStringLiteral("‘%1’").arg(listLabel));
    parts << lightsFrom;
    if (b.desk != MatrixBuild::Desk::None) parts << (lx == 1 ? tr("1 cue") : tr("%1 cues").arg(lx));
    QColor dot = tk.ink40;
    QString linkText;
    if (!m_eosDesk) {
        linkText = tr("the lighting desk isn't an ETC Eos — its cues can't be read");
    } else switch (link) {
    case L::Live:
        dot = rc.desk;
        linkText = host.isEmpty() ? tr("Live from %1").arg(deskName) : tr("Live from %1 at %2").arg(deskName, host);
        if (st == osc::EosCueLists::State::Fetching) linkText += tr(" · reading…");
        break;
    case L::Connecting: dot = tk.warn; linkText = tr("Connecting to %1…").arg(host.isEmpty() ? deskName : host); break;
    case L::Failed:     dot = tk.err;  linkText = tr("Can't reach the desk"); break;
    case L::Off:        linkText = tr("Desk not connected"); break;
    }
    if (b.desk != MatrixBuild::Desk::None && !linkLive && cfg.deskCachedAt.isValid())
        linkText += tr(" · showing cues read %1").arg(QLocale().toString(cfg.deskCachedAt, QLocale::ShortFormat));
    parts << QStringLiteral("<span style=\"color:%1\">●</span> %2").arg(dot.name(), linkText.toHtmlEscaped());
    m_summary->setText(parts.join(QStringLiteral("&nbsp;&nbsp;·&nbsp;&nbsp;")));
    m_summary->setToolTip(lights);

    // The banner: only when something needs doing.
    QString banner;
    bool deskButton = false;
    if (!m_eosDesk) {
        banner = tr("Only ETC Eos-family desks (Eos, Ion, Element, Nomad) can be read. Your quewi cues are "
                    "shown; to add lighting, choose an Eos desk in the Lighting Desk settings.");
        deskButton = true;
    } else if (link == L::Off) {
        banner = b.desk != MatrixBuild::Desk::None
            ? tr("Reading the desk is off. You're seeing desk cue list %1 as it was last read; turn on "
                 "\"Read the desk's state back\" in the Lighting Desk settings to keep it current.").arg(list)
            : tr("Reading the desk is off, so there are no lighting cues to line up. Turn on "
                 "\"Read the desk's state back\" in the Lighting Desk settings.");
        deskButton = true;
    } else if (link == L::Failed) {
        banner = tr("quewi can't reach the desk at %1 (%2). Check it's on and on the network; quewi keeps trying.")
                     .arg(host, m_feedback ? m_feedback->detail() : QString());
        deskButton = true;
    } else if (b.desk == MatrixBuild::Desk::Live && st == osc::EosCueLists::State::Partial) {
        banner = lights;
    } else if (linkLive && b.desk == MatrixBuild::Desk::Live && lx == 0) {
        banner = tr("Desk cue list %1 has no cues. Pick another list above.").arg(list);
    }
    // Nothing at all to show: say what to do instead of an empty grid (and
    // no banner on top of it saying the same).
    const bool empty = b.result.rows.empty();
    m_banner->setText(banner);
    m_bannerDesk->setVisible(deskButton);
    m_bannerRow->setVisible(!banner.isEmpty() && !empty);
    m_body->setCurrentIndex(empty ? 1 : 0);
    if (empty) {
        if (!b.source || b.quewi.empty()) {
            m_emptyTitle->setText(b.desk == MatrixBuild::Desk::None ? tr("Nothing to line up yet")
                                                                    : tr("No cues to line up"));
            m_emptyText->setText(b.desk == MatrixBuild::Desk::None
                ? tr("Add sound and video cues to %1, and set up your ETC Eos desk so quewi can read its "
                     "cue list. Their cues then appear here side by side, one row per moment.")
                      .arg(b.source ? b.source->name() : tr("a cue list"))
                : tr("Neither %1 nor desk cue list %2 has any cues yet.")
                      .arg(b.source ? b.source->name() : tr("the cue list"), list));
        }
        m_emptyDesk->setVisible(b.desk == MatrixBuild::Desk::None);
    }
    updateGoLights();
}

GoLightsTarget MatrixView::goLightsTarget() const
{
    const bool live = m_feedback && m_feedback->link() == osc::EosFeedback::Link::Live;
    return ui::goLightsTarget(build(), m_model->live(), m_eosDesk, live);
}

GoLightsTarget MatrixView::backTarget() const
{
    const bool live = m_feedback && m_feedback->link() == osc::EosFeedback::Link::Live;
    // Back is about what the DESK is running, not the GO Lights target.
    MatrixLive l = m_model->live();
    if (m_feedback) {
        l.deskActiveList = m_feedback->active().list;
        l.deskActiveCue = m_feedback->active().cue;
    }
    return ui::backLightsTarget(build(), l, m_eosDesk, live);
}

QString MatrixView::stopReason() const
{
    const bool live = m_feedback && m_feedback->link() == osc::EosFeedback::Link::Live;
    return ui::lightsStopReason(m_eosDesk, live);
}

void MatrixView::updateGoLights()
{
    const auto t = goLightsTarget();
    // Never cut off: a long label is shortened to fit, the full one is in the tooltip.
    {
        const QFontMetrics fm(m_goLights->font());
        m_goLights->setText(fm.elidedText(t.buttonText(), Qt::ElideRight, m_goLights->width() - 36));
    }
    const auto bt = backTarget();
    {
        QString text = bt.ok ? tr("◀ Back  %1").arg(bt.number) + (bt.label.isEmpty() ? QString() : QStringLiteral("  ") + bt.label)
                             : tr("◀ Back");
        const QFontMetrics fm(m_lightsBack->font());
        m_lightsBack->setText(fm.elidedText(text, Qt::ElideRight, std::max(60, m_lightsBack->width() - 24)));
        m_lightsBack->setEnabled(bt.ok && m_list);
        m_lightsBack->setToolTip(bt.ok ? tr("Go back to LX %1%2 in desk cue list %3 (fires it with its own time). "
                                            "Ctrl+Shift+B does the same.")
                                             .arg(bt.number, bt.label.isEmpty() ? QString() : QStringLiteral(" ") + bt.label, bt.list)
                                       : bt.reason);
    }
    const QString stopWhy = stopReason();
    m_lightsStop->setEnabled(stopWhy.isEmpty() && m_list);
    m_lightsStop->setToolTip(stopWhy.isEmpty() ? tr("Press the desk's Stop key: stops the running lighting cue "
                                                    "on the desk's main playback. (No shortcut, so it can't be hit by accident; "
                                                    "you can give it one in Tools → Shortcuts.)")
                                               : stopWhy);
    m_goLights->setEnabled(t.ok && m_list);
    const QString list = m_list ? m::normalNumber(m_list->matrixConfig().deskList) : QString();
    m_goLights->setToolTip(t.ok ? tr("Fire LX %1%2 on the desk now (desk cue list %3). Ctrl+Shift+G does the same.")
                                      .arg(t.number, t.label.isEmpty() ? QString() : QStringLiteral(" ") + t.label, list)
                                : t.reason);
    m_goLightsHint->setText(t.ok ? tr("Ctrl+Shift+G  ·  desk cue list %1").arg(list) : t.reason);
    emit goLightsStateChanged();
}

void MatrixView::pollLive()
{
    MatrixLive live = m_liveProvider ? m_liveProvider() : MatrixLive{};
    if (m_feedback) {
        live.deskActiveList = m_feedback->active().list;
        live.deskActiveCue = m_feedback->active().cue;
        live.deskPendingList = m_feedback->pending().list;
        live.deskPendingCue = m_feedback->pending().cue;
    }
    // LX NEXT is what GO Lights fires: the desk's pending cue when it's in
    // this list, else (computed) the next cue of this list.
    const bool linkLive = m_feedback && m_feedback->link() == osc::EosFeedback::Link::Live;
    const auto t = ui::goLightsTarget(build(), live, m_eosDesk, linkLive);
    if (t.ok) {
        live.deskPendingList = t.list;
        live.deskPendingCue = t.number;
    }
    m_model->setLive(live);
    updateGoLights();
    const auto &b = build();
    const int sb = b.rowOfQuewi(live.standby);
    const int dk = b.rowOfDesk(live.deskActiveList, live.deskActiveCue);
    if (sb != m_lastStandbyRow && sb >= 0) followTo(sb);
    else if (dk != m_lastDeskRow && dk >= 0) followTo(dk);
    m_lastStandbyRow = sb;
    m_lastDeskRow = dk;
}

void MatrixView::followTo(int row)
{
    if (!m_follow->isChecked() || row < 0 || row >= m_model->rowCount()) return;
    // Inside a folded scene: its heading is what's on screen.
    const int sc = build().result.rows[size_t(row)].scene;
    if (sc >= 0 && m_table->isRowHidden(row)) row = build().result.scenes[size_t(sc)].header;
    m_table->scrollTo(m_model->index(row, MatrixTableModel::ColCue), QAbstractItemView::PositionAtCenter);
}

namespace {
// A small searchable picker: "Line up with…".
int pickFrom(QWidget *parent, const QString &title, const QString &prompt,
             const QStringList &items, int preselect)
{
    QDialog d(parent);
    d.setWindowTitle(title);
    auto *v = new QVBoxLayout(&d);
    auto *label = new QLabel(prompt, &d);
    label->setWordWrap(true);
    v->addWidget(label);
    auto *filter = new QLineEdit(&d);
    filter->setPlaceholderText(QObject::tr("Type to filter by number or name"));
    v->addWidget(filter);
    auto *listW = new QListWidget(&d);
    listW->addItems(items);
    if (preselect >= 0 && preselect < items.size()) listW->setCurrentRow(preselect);
    v->addWidget(listW, 1);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &d);
    bb->button(QDialogButtonBox::Ok)->setText(QObject::tr("Line up"));
    v->addWidget(bb);
    QObject::connect(bb, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    QObject::connect(bb, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    QObject::connect(listW, &QListWidget::itemDoubleClicked, &d, &QDialog::accept);
    QObject::connect(filter, &QLineEdit::textChanged, &d, [listW](const QString &t) {
        for (int i = 0; i < listW->count(); ++i)
            listW->item(i)->setHidden(!listW->item(i)->text().contains(t, Qt::CaseInsensitive));
    });
    d.resize(460, 520);
    if (d.exec() != QDialog::Accepted || !listW->currentItem() || listW->currentItem()->isHidden()) return -1;
    return listW->currentRow();
}
} // namespace

void MatrixView::pickQuewiFor(const m::DeskCue &cue)
{
    const auto &b = build();
    QStringList items;
    int pre = -1;
    const int row = b.rowOfDesk(cue.list, cue.number);
    const int near = row >= 0 ? b.result.rows[size_t(row)].quewiIndex : -1;
    for (size_t i = 0; i < b.quewi.size(); ++i) {
        items << QStringLiteral("Q%1   %2").arg(b.quewi[i].number, b.quewi[i].name);
        if (int(i) == near) pre = int(i);
    }
    const int i = pickFrom(this, tr("Line up %1 with…").arg(matrixDeskName(cue)),
                           tr("Pick the quewi cue %1 should sit with. quewi's cue order doesn't change.")
                               .arg(matrixDeskName(cue)), items, pre);
    if (i >= 0) m_model->lineUp(cue, b.quewi[size_t(i)].id);
}

void MatrixView::pickLightsFor(const QUuid &quewiCue)
{
    const auto &b = build();
    QStringList items;
    std::vector<m::DeskCue> cues;
    int pre = -1;
    const int qRow = b.rowOfQuewi(quewiCue);
    for (size_t r = 0; r < b.result.rows.size(); ++r)
        for (const auto &c : b.result.rows[r].desk) {
            if (c.missing) continue;
            if (pre < 0 && int(r) > qRow) pre = int(cues.size());   // the next lighting cue below
            items << QStringLiteral("%1   %2").arg(matrixDeskName(c.cue), c.cue.label);
            cues.push_back(c.cue);
        }
    const QString q = m_model->quewiNumber(quewiCue);
    const int i = pickFrom(this, tr("Line up Q%1 with…").arg(q),
                           tr("Pick the lighting cue to put on Q%1's row. quewi's cue order doesn't change; "
                              "the lighting cue moves to Q%1.").arg(q), items, pre);
    if (i >= 0) m_model->lineUp(cues[size_t(i)], quewiCue);
}

void MatrixView::contextMenuAt(const QPoint &pos)
{
    const QModelIndex i = m_table->indexAt(pos);
    QMenu menu(this);
    if (i.isValid()) {
        const auto &row = build().result.rows[size_t(i.row())];
        if (row.kind == m::Row::Kind::Quewi && row.quewiIndex >= 0) {
            const QUuid id = build().quewi[size_t(row.quewiIndex)].id;
            const QString q = build().quewi[size_t(row.quewiIndex)].number;
            menu.addAction(tr("Make Q%1 the standby cue").arg(q), this, [this, id] { emit standbyRequested(id); });
            if (!m_locked)
                menu.addAction(tr("Line up Q%1 with a lighting cue…").arg(q), this, [this, id] { pickLightsFor(id); });
        }
        if (!row.desk.empty()) {
            const int r = i.row();
            menu.addAction(m_model->rowDetailsOpen(r) ? tr("Hide this row's details") : tr("Show this row's details"),
                           this, [this, r] { m_model->toggleRowDetails(r); });
        }
        if (!m_locked) {
            for (const auto &c : row.desk) {
                const auto cue = c.cue;
                if (!c.missing)
                    menu.addAction(tr("Line up %1 with a quewi cue…").arg(matrixDeskName(cue)), this,
                                   [this, cue] { pickQuewiFor(cue); });
                if (c.how == m::How::Manual)
                    menu.addAction(c.missing ? tr("Forget %1 (not on the desk)").arg(matrixDeskName(cue))
                                             : tr("Unlink %1 (let quewi place it)").arg(matrixDeskName(cue)),
                                   this, [this, cue] { m_model->unlink({cue}); });
            }
        }
    }
    if (!menu.isEmpty()) menu.addSeparator();
    auto *again = menu.addAction(tr("Read the desk again"), m_refresh, &QPushButton::click);
    again->setEnabled(m_reader != nullptr);
    menu.exec(m_table->viewport()->mapToGlobal(pos));
}

bool MatrixView::sceneCollapsed(int scene) const
{
    if (!m_list || scene < 0 || size_t(scene) >= build().result.scenes.size()) return false;
    return m_list->matrixConfig().collapsedScenes.contains(build().result.scenes[size_t(scene)].name);
}

void MatrixView::saveCollapsed(const QStringList &names)
{
    if (!m_list) return;
    auto cfg = m_list->matrixConfig();
    if (cfg.collapsedScenes == names) return;
    cfg.collapsedScenes = names;
    QSignalBlocker block(m_list);              // a view preference: no "unsaved", no rebuild
    m_list->setMatrixConfig(cfg);
    applyCollapse();
}

void MatrixView::toggleScene(int scene)
{
    if (!m_list || scene < 0 || size_t(scene) >= build().result.scenes.size()) return;
    QStringList names = m_list->matrixConfig().collapsedScenes;
    const QString n = build().result.scenes[size_t(scene)].name;
    if (names.contains(n)) names.removeAll(n);
    else names << n;
    saveCollapsed(names);
}

void MatrixView::setAllScenesCollapsed(bool collapsed)
{
    QStringList names;
    if (collapsed)
        for (const auto &s : build().result.scenes) names << s.name;
    saveCollapsed(names);
}

void MatrixView::applyCollapse()
{
    const auto &b = build();
    const QStringList shut = m_list ? m_list->matrixConfig().collapsedScenes : QStringList();
    m_model->setCollapsedScenes(shut);
    m_table->clearSpans();
    for (size_t r = 0; r < b.result.rows.size(); ++r) {
        const auto &row = b.result.rows[r];
        if (row.kind == m::Row::Kind::Scene) {
            m_table->setSpan(int(r), 0, 1, MatrixTableModel::ColCount);
            m_table->setRowHidden(int(r), false);
            continue;
        }
        const bool hide = row.scene >= 0 && shut.contains(b.result.scenes[size_t(row.scene)].name);
        m_table->setRowHidden(int(r), hide);
    }
    const bool any = !b.result.scenes.empty();
    m_collapseAll->setVisible(any);
    m_expandAll->setVisible(any);
}

void MatrixView::showEvent(QShowEvent *e)
{
    QWidget::showEvent(e);
    restyle();
    m_liveTimer->start();
    pollLive();
}

void MatrixView::hideEvent(QHideEvent *e)
{
    QWidget::hideEvent(e);
    m_liveTimer->stop();
}

} // namespace quewi::ui
