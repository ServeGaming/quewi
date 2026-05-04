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

    connect(this, &QTreeView::doubleClicked, this, [this](const QModelIndex &idx) {
        auto *m = qobject_cast<core::CueListModel *>(model());
        if (!m) return;
        if (auto *cue = m->cueAt(idx)) emit cueDoubleClicked(cue);
    });
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
        header()->resizeSection(CueListModel::ColumnState,    24);
        header()->resizeSection(CueListModel::ColumnNumber,   64);
        header()->resizeSection(CueListModel::ColumnType,     96);
        header()->resizeSection(CueListModel::ColumnName,    340);
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
    // QLab semantics: "next" is the cue the playhead is on — i.e. the
    // currently selected one. Pressing GO fires it and the caller is
    // responsible for advancing selection to row+1.
    auto *m = qobject_cast<core::CueListModel *>(model());
    if (!m || m->rowCount() == 0) return nullptr;
    const auto idx = currentIndex();
    const int row = idx.isValid() ? idx.row() : 0;
    if (row < 0 || row >= m->rowCount()) return nullptr;
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
