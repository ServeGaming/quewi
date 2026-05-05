#include "ui/CueListView.h"

#include "core/CueListModel.h"
#include "core/UndoCommands.h"
#include "core/Workspace.h"

#include <QDataStream>
#include <QDropEvent>
#include <QHeaderView>
#include <QKeyEvent>
#include <QMimeData>
#include <QSettings>
#include <QUndoStack>

namespace quewi::ui {

CueListView::CueListView(QWidget *parent)
    : QTreeView(parent)
{
    setRootIsDecorated(true);  // disclosure triangles for group cues
    setUniformRowHeights(true);
    setAllColumnsShowFocus(true);
    setIndentation(16);
    // Roomier rows — operator-friendly density, QLab-ish breathing room.
    setStyleSheet(QStringLiteral(
        "QTreeView::item { min-height: 28px; }"
    ));
    setIconSize(QSize(18, 18));
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setAlternatingRowColors(true);
    setEditTriggers(QAbstractItemView::EditKeyPressed);
    header()->setStretchLastSection(true);
    setFocusPolicy(Qt::StrongFocus);

    setDragEnabled(true);
    setAcceptDrops(true);
    setDropIndicatorShown(true);
    setDragDropMode(QAbstractItemView::InternalMove);
    setDefaultDropAction(Qt::MoveAction);

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
        header()->resizeSection(CueListModel::ColumnGain,     64);
        header()->resizeSection(CueListModel::ColumnPan,      64);
        header()->resizeSection(CueListModel::ColumnFadeIn,   72);
        header()->resizeSection(CueListModel::ColumnFadeOut,  72);
        header()->resizeSection(CueListModel::ColumnOutput,  120);
        header()->resizeSection(CueListModel::ColumnTarget,  120);
        header()->resizeSection(CueListModel::ColumnHost,    140);
        header()->resizeSection(CueListModel::ColumnPort,     56);
        header()->resizeSection(CueListModel::ColumnFile,    180);
        applyColumnVisibility();
    }
}

void CueListView::applyColumnVisibility()
{
    using core::CueListModel;
    QSettings s(QStringLiteral("ServeGaming"), QStringLiteral("quewi"));
    s.beginGroup(QStringLiteral("ui/cueColumns"));
    for (int c = 0; c < CueListModel::ColumnCount; ++c) {
        if (!CueListModel::columnIsOptional(c)) continue;
        bool visible = s.value(CueListModel::columnSettingsKey(c), false).toBool();
        setColumnHidden(c, !visible);
    }
    s.endGroup();
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

void CueListView::dropEvent(QDropEvent *event)
{
    // Internal-only: only accept our MIME type. External drops fall through
    // to the QTreeView default (which the MainWindow upgrades into "create
    // cues from files" for non-row drops).
    auto *data = event->mimeData();
    if (data->hasUrls() && event->source() != this) {
        // External file drop — punt to MainWindow which knows how to
        // turn URLs into cues. Compute the target row from the indicator.
        int dest = indexAt(event->position().toPoint()).row();
        const auto pos = dropIndicatorPosition();
        if (dest < 0) dest = model() ? model()->rowCount() : 0;
        else if (pos == BelowItem) dest += 1;
        emit filesDropped(data->urls(), dest);
        event->acceptProposedAction();
        return;
    }
    if (event->source() != this
        || !data->hasFormat(QStringLiteral("application/x-quewi-cue-row"))) {
        QTreeView::dropEvent(event);
        return;
    }

    QDataStream ds(data->data(QStringLiteral("application/x-quewi-cue-row")));
    qint32 count = 0;
    ds >> count;
    // Cap the count well above any plausible cue-list size so a malformed
    // or hostile MIME payload can't pin reserve() to a huge allocation.
    constexpr qint32 kMaxDropRows = 100000;
    if (count <= 0 || count > kMaxDropRows) { event->ignore(); return; }
    QList<int> rows;
    rows.reserve(count);
    for (qint32 i = 0; i < count; ++i) {
        qint32 r = -1; ds >> r;
        if (ds.status() != QDataStream::Ok) { event->ignore(); return; }
        if (r >= 0) rows.append(r);
    }
    if (rows.isEmpty()) { event->ignore(); return; }

    auto *m = qobject_cast<core::CueListModel *>(model());
    if (!m || !m_workspace) { event->ignore(); return; }
    auto *list = m->cueList();
    if (!list) { event->ignore(); return; }

    // Compute target row from drop position + indicator.
    int dest = indexAt(event->position().toPoint()).row();
    const auto pos = dropIndicatorPosition();
    if (dest < 0) {
        dest = m->rowCount(); // dropped past end
    } else if (pos == BelowItem) {
        dest += 1;
    }
    // pos == OnItem: drop *onto* a row — treat as "before that row".

    // Move rows in source order, top-to-bottom. Issue one undoable
    // command per moved row so the user can ctrl-Z each step (or undo
    // the whole batch if we wrap them into a macro — done below).
    auto *stack = m_workspace->undoStack();
    stack->beginMacro(QObject::tr("Reorder cues"));
    for (int row : rows) {
        if (row == dest || row + 1 == dest) {
            // No-op move (dropping a row on itself)
            continue;
        }
        stack->push(new core::MoveCueCommand(list, row, dest));
        // After moving row → dest, subsequent rows in the source list
        // shift if they were below dest.
        if (row > dest) {
            // Rows numerically greater than the *original* row that we
            // captured shift up by one if they were between dest..row-1,
            // but our snapshot rows were already collected pre-move.
            // The simplest correct behaviour for multi-row drag is to
            // recompute on the fly: increment dest so the next moved row
            // lands right after the previous one.
            dest += 1;
        }
        // For row < dest case the source got pulled out from below dest,
        // so dest itself shifted down; the destRowAfterTake() in the
        // command already handles that.
    }
    stack->endMacro();
    event->acceptProposedAction();
}

void CueListView::currentChanged(const QModelIndex &current, const QModelIndex &previous)
{
    QTreeView::currentChanged(current, previous);
    auto *m = qobject_cast<core::CueListModel *>(model());
    emit currentCueChanged(m ? m->cueAt(current) : nullptr);
}

} // namespace quewi::ui
