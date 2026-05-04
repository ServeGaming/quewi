#include "core/CueListModel.h"

#include "core/CueList.h"
#include "cues/Cue.h"

#include <QFont>
#include <QFontDatabase>

namespace quewi::core {

CueListModel::CueListModel(QObject *parent)
    : QAbstractItemModel(parent)
{
}

CueListModel::~CueListModel() = default;

void CueListModel::setCueList(CueList *list)
{
    if (m_list.data() == list) return;
    beginResetModel();
    disconnectList();
    m_list = list;
    connectList();
    endResetModel();
}

void CueListModel::connectList()
{
    if (!m_list) return;
    connect(m_list, &CueList::aboutToInsertCue, this, &CueListModel::onAboutToInsert);
    connect(m_list, &CueList::cueInserted,      this, &CueListModel::onInserted);
    connect(m_list, &CueList::aboutToRemoveCue, this, &CueListModel::onAboutToRemove);
    connect(m_list, &CueList::cueRemoved,       this, &CueListModel::onRemoved);
    connect(m_list, &CueList::cueChanged,       this, &CueListModel::onCueChanged);
}

void CueListModel::disconnectList()
{
    if (!m_list) return;
    disconnect(m_list, nullptr, this, nullptr);
}

cues::Cue *CueListModel::cueAt(const QModelIndex &index) const
{
    if (!m_list || !index.isValid()) return nullptr;
    return m_list->cueAt(index.row());
}

QModelIndex CueListModel::indexForCue(const cues::Cue *cue, int column) const
{
    if (!m_list || !cue) return {};
    int row = m_list->rowOf(cue);
    if (row < 0) return {};
    return index(row, column);
}

QModelIndex CueListModel::index(int row, int column, const QModelIndex &parent) const
{
    if (parent.isValid() || !m_list) return {};
    if (row < 0 || row >= m_list->cueCount()) return {};
    if (column < 0 || column >= ColumnCount) return {};
    return createIndex(row, column);
}

QModelIndex CueListModel::parent(const QModelIndex &) const
{
    return {};
}

int CueListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid() || !m_list) return 0;
    return m_list->cueCount();
}

int CueListModel::columnCount(const QModelIndex &) const
{
    return ColumnCount;
}

QVariant CueListModel::data(const QModelIndex &index, int role) const
{
    auto *cue = cueAt(index);
    if (!cue) return {};

    if (role == CueIdRole)      return QVariant::fromValue(cue->id());
    if (role == CuePointerRole) return QVariant::fromValue(static_cast<void *>(cue));

    if (role == Qt::DisplayRole || role == Qt::EditRole) {
        switch (index.column()) {
        case ColumnNumber:    return QString::number(cue->number(), 'f', 2);
        case ColumnType:      return cue->typeName();
        case ColumnName:      return cue->name();
        case ColumnPreWait:   return cue->preWait() > 0
                                  ? QString::number(cue->preWait(), 'f', 2)
                                  : QStringLiteral("—");
        case ColumnPostWait:  return cue->postWait() > 0
                                  ? QString::number(cue->postWait(), 'f', 2)
                                  : QStringLiteral("—");
        case ColumnNotes:     return cue->notes();
        default:              return {};
        }
    }

    if (role == Qt::TextAlignmentRole) {
        switch (index.column()) {
        case ColumnNumber:
        case ColumnPreWait:
        case ColumnPostWait:  return int(Qt::AlignRight | Qt::AlignVCenter);
        case ColumnType:      return int(Qt::AlignCenter);
        default:              return int(Qt::AlignLeft | Qt::AlignVCenter);
        }
    }

    if (role == Qt::FontRole) {
        if (index.column() == ColumnNumber
            || index.column() == ColumnPreWait
            || index.column() == ColumnPostWait) {
            QFont f = QFontDatabase::systemFont(QFontDatabase::FixedFont);
            f.setStyleHint(QFont::Monospace);
            if (index.column() == ColumnNumber) {
                f.setBold(true);
                f.setPointSizeF(f.pointSizeF() + 1.0);
            }
            return f;
        }
    }

    return {};
}

QVariant CueListModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return {};
    switch (section) {
    case ColumnNumber:    return tr("#");
    case ColumnType:      return tr("Type");
    case ColumnName:      return tr("Name");
    case ColumnPreWait:   return tr("Pre");
    case ColumnPostWait:  return tr("Post");
    case ColumnNotes:     return tr("Notes");
    default:              return {};
    }
}

Qt::ItemFlags CueListModel::flags(const QModelIndex &index) const
{
    if (!index.isValid()) return Qt::NoItemFlags;
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}

void CueListModel::onAboutToInsert(int row) { beginInsertRows({}, row, row); }
void CueListModel::onInserted(int)          { endInsertRows(); }
void CueListModel::onAboutToRemove(int row) { beginRemoveRows({}, row, row); }
void CueListModel::onRemoved(int)           { endRemoveRows(); }
void CueListModel::onCueChanged(int row)
{
    emit dataChanged(index(row, 0), index(row, ColumnCount - 1));
}

} // namespace quewi::core
