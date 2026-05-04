#include "ui/CueListView.h"

#include "core/CueListModel.h"
#include "core/Workspace.h"

#include <QHeaderView>
#include <QKeyEvent>

namespace quewi::ui {

CueListView::CueListView(QWidget *parent)
    : QTreeView(parent)
{
    setRootIsDecorated(false);
    setUniformRowHeights(true);
    setAllColumnsShowFocus(true);
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setAlternatingRowColors(true);
    setEditTriggers(QAbstractItemView::EditKeyPressed);
    header()->setStretchLastSection(true);
    setFocusPolicy(Qt::StrongFocus);
}

CueListView::~CueListView() = default;

void CueListView::setWorkspace(core::Workspace *workspace)
{
    m_workspace = workspace;
}

void CueListView::setModel(QAbstractItemModel *model)
{
    QTreeView::setModel(model);
    using core::CueListModel;
    if (model) {
        // Sensible default column widths.
        header()->resizeSection(CueListModel::ColumnNumber,   60);
        header()->resizeSection(CueListModel::ColumnType,     90);
        header()->resizeSection(CueListModel::ColumnName,    320);
        header()->resizeSection(CueListModel::ColumnPreWait,  60);
        header()->resizeSection(CueListModel::ColumnPostWait, 60);
    }
}

cues::Cue *CueListView::currentCue() const
{
    if (auto *m = qobject_cast<core::CueListModel *>(model()))
        return m->cueAt(currentIndex());
    return nullptr;
}

cues::Cue *CueListView::nextCue() const
{
    auto *m = qobject_cast<core::CueListModel *>(model());
    if (!m) return nullptr;
    auto idx = currentIndex();
    int row = idx.isValid() ? idx.row() + 1 : 0;
    if (row >= m->rowCount()) return nullptr;
    return m->cueAt(m->index(row, 0));
}

void CueListView::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Space) {
        emit goRequested();
        event->accept();
        return;
    }
    QTreeView::keyPressEvent(event);
}

void CueListView::currentChanged(const QModelIndex &current, const QModelIndex &previous)
{
    QTreeView::currentChanged(current, previous);
    auto *m = qobject_cast<core::CueListModel *>(model());
    emit currentCueChanged(m ? m->cueAt(current) : nullptr);
}

} // namespace quewi::ui
