#include "ui/MixGridModel.h"

#include "core/CueList.h"
#include "mix/MixCue.h"
#include "mix/MixShow.h"
#include "ui/Theme.h"

#include <QColor>
#include <QFont>
#include <algorithm>

using quewi::mix::MixCue;
using quewi::mix::MixShow;

namespace quewi::ui {

MixGridModel::MixGridModel(QObject *parent) : QAbstractTableModel(parent) {}
MixGridModel::~MixGridModel() = default;

void MixGridModel::setCueList(core::CueList *list)
{
    if (m_list == list) return;
    beginResetModel();
    if (m_list) m_list->disconnect(this);
    m_list = list;
    if (m_list) {
        // Any structural change reshapes the grid, and because each row is
        // painted relative to the one above it, inserting a cue restyles every
        // row below. Simplest correct thing is a full refresh.
        connect(m_list, &core::CueList::cueInserted, this, &MixGridModel::onListChanged);
        connect(m_list, &core::CueList::cueRemoved,  this, &MixGridModel::onListChanged);
        connect(m_list, &core::CueList::cueChanged,  this, &MixGridModel::onListChanged);
    }
    endResetModel();
}

void MixGridModel::setMixShow(MixShow *show)
{
    if (m_show == show) return;
    beginResetModel();
    if (m_show) m_show->disconnect(this);
    m_show = show;
    if (m_show) {
        // Channel renames change every cell's text; the DCA count changes the
        // column count. Both need a reset, not a dataChanged.
        connect(m_show, &MixShow::channelsChanged,  this, &MixGridModel::onListChanged);
        connect(m_show, &MixShow::ensemblesChanged, this, &MixGridModel::onListChanged);
        connect(m_show, &MixShow::dcaCountChanged,  this, &MixGridModel::onListChanged);
    }
    endResetModel();
}

void MixGridModel::onListChanged()
{
    beginResetModel();
    endResetModel();
}

MixCue *MixGridModel::cueAt(int row) const
{
    if (!m_list || row < 0 || row >= m_list->cueCount()) return nullptr;
    return qobject_cast<MixCue *>(m_list->cueAt(row));
}

int MixGridModel::rowOfCue(const MixCue *cue) const
{
    if (!cue || !m_list) return -1;
    for (int r = 0; r < m_list->cueCount(); ++r)
        if (m_list->cueAt(r) == cue) return r;
    return -1;
}

MixCue *MixGridModel::liveCue() const { return qobject_cast<MixCue *>(m_liveCue.data()); }

void MixGridModel::setLiveCue(MixCue *cue)
{
    if (m_liveCue == cue) return;
    const int oldRow = rowOfCue(qobject_cast<MixCue *>(m_liveCue.data()));
    m_liveCue = cue;
    const int newRow = rowOfCue(cue);

    // Repaint only the two affected rows rather than resetting the model —
    // the live marker moves on every GO, and a full reset would collapse the
    // operator's selection and scroll position mid-show.
    const int cols = columnCount();
    for (int row : {oldRow, newRow})
        if (row >= 0)
            emit dataChanged(index(row, 0), index(row, cols - 1));
}

int MixGridModel::dcaForColumn(int column) const
{
    if (column < kFixedCols) return 0;
    const int dca = column - kFixedCols + 1;
    return (m_show && dca <= m_show->dcaCount()) ? dca : 0;
}

int MixGridModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid() || !m_list) return 0;
    return m_list->cueCount();
}

int MixGridModel::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return kFixedCols + (m_show ? m_show->dcaCount() : 0);
}

QString MixGridModel::cellText(int row, int dca) const
{
    auto *cue = cueAt(row);
    if (!cue || !m_show) return {};

    // Show what the operator wrote, not what it resolves to: an ensemble reads
    // as "Ensemble Women", not as twenty channel names that would blow the
    // column width apart. The resolved strips are what goes to the console.
    QStringList parts = cue->dcaEnsembles(dca);

    QVector<int> strips(cue->dcaStrips(dca).begin(), cue->dcaStrips(dca).end());
    std::sort(strips.begin(), strips.end());
    for (int strip : strips) {
        const auto ch = m_show->channel(strip);
        // A strip with no channel still shows — as a bare number — rather than
        // vanishing. Silently hiding programming because someone deleted a
        // channel is how you lose a mic in a scene.
        parts.push_back(ch.name.isEmpty() ? QString::number(strip) : ch.name);
    }
    return parts.join(QStringLiteral(", "));
}

MixGridModel::CellChange MixGridModel::changeFor(int row, int dca) const
{
    if (!m_show) return CellChange::Unchanged;
    auto *cue = cueAt(row);
    if (!cue) return CellChange::Unchanged;

    const auto now = m_show->resolve(cue->dcaStrips(dca), cue->dcaEnsembles(dca));

    // The first cue has nothing to differ from, so anything on it is arriving.
    QSet<int> before;
    if (row > 0) {
        if (auto *prev = cueAt(row - 1))
            before = m_show->resolve(prev->dcaStrips(dca), prev->dcaEnsembles(dca));
    }

    if (now == before) return CellChange::Unchanged;
    if (now.isEmpty())    return CellChange::Removed;
    if (before.isEmpty()) return CellChange::Assigned;

    // Anything arriving counts as Assigned even if something also left: a mic
    // becoming live is the thing the operator must not miss, and it outranks
    // a mic going away.
    for (int strip : now)
        if (!before.contains(strip)) return CellChange::Assigned;
    return CellChange::Modified;
}

QVariant MixGridModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid()) return {};
    auto *cue = cueAt(index.row());
    if (!cue) return {};

    const int dca = dcaForColumn(index.column());
    const auto &t = Theme::tokens();

    const bool isLive = (m_liveCue.data() == cue);

    switch (role) {
    case Qt::DisplayRole:
    case Qt::EditRole:
        if (index.column() == kColNumber)
            // 'f',2 to match the cue list and everywhere else a cue number is
            // shown; 'g',6 made mix cues read differently from the set list.
            return cue->number() > 0 ? QString::number(cue->number(), 'f', 2) : QString();
        if (index.column() == kColName) return cue->name();
        if (dca > 0) {
            const QString text = cellText(index.row(), dca);
            // A DCA that just emptied shows a dim dash, not blank — a departing
            // mic must be visible as a change, or the operator can't tell "this
            // DCA cleared" from "this DCA was never used".
            if (text.isEmpty() && changeFor(index.row(), dca) == CellChange::Removed)
                return QStringLiteral("—");
            return text;
        }
        return {};

    case Qt::FontRole:
        // The live row is bold — one more channel for "this is on the desk"
        // beyond colour, so it survives a colour-blind operator and a bad
        // booth monitor.
        if (isLive) { QFont f; f.setBold(true); return f; }
        return {};

    case Qt::ForegroundRole:
        if (index.column() < kFixedCols) return QColor(t.ink100);
        switch (changeFor(index.row(), dca)) {
        // The colour vocabulary is TheatreMix's, mapped onto our palette: the
        // people who'd use this already read these meanings, and inventing our
        // own would be a gratuitous thing to relearn.
        case CellChange::Assigned:  return QColor(t.ink100);       // brightest: mic arriving
        case CellChange::Modified:  return QColor(t.warn);         // partial change
        case CellChange::Removed:   return QColor(t.ink40);        // gone
        case CellChange::Unchanged: return QColor(t.ink60);        // carried over: recede
        }
        return QColor(t.ink60);

    case Qt::BackgroundRole: {
        // The live row gets a calm amber wash across its whole width — the
        // standby answer to "what's on the desk right now?", which after a
        // GO-advance is no longer the selected row.
        if (isLive) { QColor c = t.accent; c.setAlpha(30); return c; }
        // Otherwise tint only the cells that change. A grid where everything
        // is coloured tells you nothing.
        if (dca > 0) {
            switch (changeFor(index.row(), dca)) {
            case CellChange::Assigned: { QColor c = t.running; c.setAlpha(46); return c; }
            case CellChange::Modified: { QColor c = t.warn;    c.setAlpha(38); return c; }
            default: break;
            }
        }
        return {};
    }

    case Qt::TextAlignmentRole:
        return int(index.column() == kColNumber ? (Qt::AlignRight | Qt::AlignVCenter)
                                                : (Qt::AlignLeft  | Qt::AlignVCenter));

    case Qt::ToolTipRole: {
        if (dca == 0) return {};
        const QString text = cellText(index.row(), dca);
        if (text.isEmpty()) return {};
        // Ensembles hide their membership in the cell, so the tooltip is where
        // "who is actually open right now" lives.
        if (m_show && !cue->dcaEnsembles(dca).isEmpty()) {
            const auto resolved = m_show->resolve(cue->dcaStrips(dca), cue->dcaEnsembles(dca));
            QStringList names;
            QVector<int> sorted(resolved.begin(), resolved.end());
            std::sort(sorted.begin(), sorted.end());
            for (int s : sorted) {
                const auto ch = m_show->channel(s);
                names.push_back(ch.name.isEmpty() ? QString::number(s) : ch.name);
            }
            return tr("DCA %1: %2\n%3").arg(dca).arg(text, names.join(QStringLiteral(", ")));
        }
        return tr("DCA %1: %2").arg(dca).arg(text);
    }

    case CellChangeRole:     return QVariant::fromValue(changeFor(index.row(), dca));
    case IsDcaColumnRole:    return dca > 0;
    case DcaNumberRole:      return dca;
    case CueRole:            return QVariant::fromValue(static_cast<void *>(cue));
    case IsLiveCueRole:      return isLive;
    default: return {};
    }
}

QVariant MixGridModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation == Qt::Vertical) {
        if (role == Qt::DisplayRole) return section + 1;
        return {};
    }
    if (role != Qt::DisplayRole) return {};
    if (section == kColNumber) return tr("Cue");
    if (section == kColName)   return tr("Name");
    const int dca = dcaForColumn(section);
    return dca > 0 ? tr("DCA %1").arg(dca) : QVariant();
}

Qt::ItemFlags MixGridModel::flags(const QModelIndex &index) const
{
    if (!index.isValid()) return Qt::NoItemFlags;
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable;
}

bool MixGridModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (role != Qt::EditRole || !index.isValid()) return false;
    auto *cue = cueAt(index.row());
    if (!cue) return false;

    if (index.column() == kColNumber) {
        bool ok = false;
        const double n = value.toDouble(&ok);
        if (!ok) return false;
        cue->setField(QStringLiteral("number"), n);
        emit cueEdited(cue);
        return true;
    }
    if (index.column() == kColName) {
        cue->setField(QStringLiteral("name"), value.toString());
        emit cueEdited(cue);
        return true;
    }

    const int dca = dcaForColumn(index.column());
    if (dca == 0 || !m_show) return false;

    // Accept whatever the operator types: channel names, strip numbers, or
    // ensemble names, comma-separated. Making them pick a syntax would be
    // making them do the computer's job.
    QSet<int>   strips;
    QStringList ensembles;
    const auto knownEnsembles = m_show->ensembleNames();

    for (const auto &raw : value.toString().split(u',', Qt::SkipEmptyParts)) {
        const QString token = raw.trimmed();
        if (token.isEmpty()) continue;

        bool isNumber = false;
        const int strip = token.toInt(&isNumber);
        if (isNumber && strip >= 1) { strips.insert(strip); continue; }

        // Ensemble name (case-insensitive, so nobody has to match our casing).
        bool matched = false;
        for (const auto &name : knownEnsembles) {
            if (name.compare(token, Qt::CaseInsensitive) == 0) {
                ensembles.push_back(name);
                matched = true;
                break;
            }
        }
        if (matched) continue;

        // Channel name.
        for (const auto &ch : m_show->channels()) {
            if (ch.name.compare(token, Qt::CaseInsensitive) == 0) {
                strips.insert(ch.strip);
                matched = true;
                break;
            }
        }
        // Unmatched tokens are dropped rather than invented into a strip
        // number — a typo must not silently open a mic.
    }

    cue->setDcaStrips(dca, strips);
    cue->setDcaEnsembles(dca, ensembles);

    // Every row below is painted relative to this one, so the change ripples.
    emit dataChanged(this->index(index.row(), 0),
                     this->index(rowCount() - 1, columnCount() - 1));
    emit cueEdited(cue);
    return true;
}

} // namespace quewi::ui
