#include "ui/CueListView.h"

#include "core/CueListModel.h"
#include "core/UndoCommands.h"
#include "core/Workspace.h"

#include "cues/Cue.h"

#include <QAction>
#include <QContextMenuEvent>
#include <QDataStream>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QHeaderView>
#include <QKeyEvent>
#include <QMenu>
#include <QMimeData>
#include <QPainter>
#include <QPaintEvent>
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
        // Re-paint the empty-state placeholder when the row count changes.
        connect(model, &QAbstractItemModel::rowsInserted, this,
                [this]{ viewport()->update(); });
        connect(model, &QAbstractItemModel::rowsRemoved, this,
                [this]{ viewport()->update(); });
        connect(model, &QAbstractItemModel::modelReset, this,
                [this]{ viewport()->update(); });
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
    m_dragActive = false;
    viewport()->update();
    event->acceptProposedAction();
}

void CueListView::currentChanged(const QModelIndex &current, const QModelIndex &previous)
{
    QTreeView::currentChanged(current, previous);
    auto *m = qobject_cast<core::CueListModel *>(model());
    emit currentCueChanged(m ? m->cueAt(current) : nullptr);
}

void CueListView::contextMenuEvent(QContextMenuEvent *event)
{
    auto *m = qobject_cast<core::CueListModel *>(model());
    if (!m || !m_workspace) return;

    const QModelIndex idx = indexAt(event->pos());
    auto *cue = m->cueAt(idx);
    if (!cue) return;

    // If the user right-clicked a row outside the current selection,
    // include only that row; otherwise the action applies to all
    // selected rows.
    QList<cues::Cue *> targets;
    const auto sel = selectionModel() ? selectionModel()->selectedRows() : QModelIndexList{};
    bool clickInSel = false;
    for (const auto &i : sel)
        if (i.row() == idx.row()) { clickInSel = true; break; }
    if (clickInSel && !sel.isEmpty()) {
        for (const auto &i : sel)
            if (auto *c = m->cueAt(i)) targets << c;
    } else {
        targets << cue;
    }

    QMenu menu(this);
    auto *colorMenu = menu.addMenu(tr("Color"));

    // Eight distinct theatre-friendly tints + a neutral white. We store
    // the color as a QColor and let CueListModel tint the row at paint
    // time. Picking the same color as the current one acts as a toggle
    // (clears it) — operators expect that.
    struct Swatch { const char *label; const char *hex; };
    static const Swatch kSwatches[] = {
        { "Red",      "#C26A55" },
        { "Amber",    "#D7A24E" },
        { "Yellow",   "#E8C861" },
        { "Green",    "#6FAE63" },
        { "Teal",     "#5AA89D" },
        { "Blue",     "#4F8EAF" },
        { "Purple",   "#9577B0" },
        { "Pink",     "#C97A9B" },
    };

    auto applyColor = [this, targets](const QColor &col) {
        if (!m_workspace) return;
        auto *stack = m_workspace->undoStack();
        const QString label = col.isValid() ? tr("Set cue color")
                                              : tr("Clear cue color");
        stack->beginMacro(label);
        for (auto *c : targets) {
            if (!c) continue;
            const QColor before = c->color();
            // Toggle off if every selected cue already has this exact color.
            QColor target = col;
            if (col.isValid() && before == col) target = QColor();
            stack->push(new core::EditCueFieldCommand(
                c, QStringLiteral("color"),
                QVariant::fromValue(before),
                QVariant::fromValue(target)));
        }
        stack->endMacro();
    };

    for (const auto &s : kSwatches) {
        const QColor col(QString::fromLatin1(s.hex));
        QPixmap swatch(16, 16);
        swatch.fill(col);
        auto *act = colorMenu->addAction(QIcon(swatch), tr(s.label));
        connect(act, &QAction::triggered, this, [applyColor, col]{ applyColor(col); });
    }
    colorMenu->addSeparator();
    auto *clear = colorMenu->addAction(tr("Clear color"));
    connect(clear, &QAction::triggered, this, [applyColor]{ applyColor(QColor()); });

    menu.exec(event->globalPos());
}

void CueListView::dragMoveEvent(QDragMoveEvent *event)
{
    QTreeView::dragMoveEvent(event);
    m_dragActive = true;
    m_dragIndex  = indexAt(event->position().toPoint());
    m_dragPos    = static_cast<int>(dropIndicatorPosition());
    viewport()->update();
}

void CueListView::dragLeaveEvent(QDragLeaveEvent *event)
{
    QTreeView::dragLeaveEvent(event);
    m_dragActive = false;
    viewport()->update();
}

void CueListView::paintEvent(QPaintEvent *event)
{
    QTreeView::paintEvent(event);

    // Bolder drop indicator on top of the QStyle default — three pixels
    // tall, accent-coloured, full viewport width. The default 1px line
    // is too easy to miss on a 165 Hz monitor mid-drag.
    if (m_dragActive) {
        QPainter p(viewport());
        p.setRenderHint(QPainter::Antialiasing, false);
        const QColor accent = palette().color(QPalette::Highlight);
        p.setPen(Qt::NoPen);

        if (m_dragIndex.isValid()) {
            const QRect r = visualRect(m_dragIndex);
            const auto pos = static_cast<DropIndicatorPosition>(m_dragPos);
            if (pos == OnItem) {
                p.setBrush(QColor(accent.red(), accent.green(), accent.blue(), 60));
                p.drawRect(0, r.top(), viewport()->width(), r.height());
            } else {
                QColor line = accent; line.setAlpha(220);
                p.setBrush(line);
                const int y = (pos == BelowItem) ? r.bottom() : r.top();
                p.drawRect(0, y - 1, viewport()->width(), 3);
            }
        } else if (model() && model()->rowCount() > 0) {
            // Drop past the last row — line at the bottom of that row.
            const auto last = model()->index(model()->rowCount() - 1, 0);
            const QRect r = visualRect(last);
            QColor line = accent; line.setAlpha(220);
            p.setBrush(line);
            p.drawRect(0, r.bottom() - 1, viewport()->width(), 3);
        }
    }

    if (model() && model()->rowCount() > 0) return;

    // Draw a centered hint over the empty viewport. Painted *after* the
    // base view so the alternating-row stripes don't show through.
    QPainter p(viewport());
    p.setRenderHint(QPainter::Antialiasing);

    auto col = palette().color(QPalette::PlaceholderText);
    if (!col.isValid()) col = palette().color(QPalette::Mid);

    QFont titleFont = font();
    titleFont.setPointSizeF(titleFont.pointSizeF() + 4.0);
    titleFont.setWeight(QFont::DemiBold);

    const QString title = tr("No cues yet");
    const QString hint  = tr("Press N for a memo · A for audio · O for OSC\n"
                             "or drag audio/video files here");

    const QRect r = viewport()->rect();
    p.setPen(col);

    p.setFont(titleFont);
    QRect titleRect = p.fontMetrics().boundingRect(r, Qt::AlignCenter, title);
    titleRect.moveCenter(QPoint(r.center().x(), r.center().y() - titleRect.height()));
    p.drawText(titleRect, Qt::AlignCenter, title);

    p.setFont(font());
    QRect hintRect = r;
    hintRect.setTop(titleRect.bottom() + 8);
    p.drawText(hintRect, Qt::AlignHCenter | Qt::AlignTop, hint);
}

} // namespace quewi::ui
