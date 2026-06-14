#include "ui/CueListView.h"

#include "core/CueListModel.h"
#include "core/UndoCommands.h"
#include "core/Workspace.h"

#include "cues/Cue.h"
#include "show/ShowFile.h"

#include <QAction>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QDataStream>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QGuiApplication>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QMainWindow>
#include <QMenu>
#include <QStatusBar>
#include <QMimeData>
#include <QPainter>
#include <QPaintEvent>
#include <QSettings>
#include <QStyledItemDelegate>
#include <QUndoStack>
#include <QUuid>

namespace quewi::ui {

namespace {

// Item delegate that overlays a coloured wash on top of the QSS-rendered
// row when the cue is running. QSS owns the base background (panel +
// hover + selected), and stylesheet rules win over the model's
// Qt::BackgroundRole — so the only way to get a "running" tint visible
// across the whole row, layered above the hover/selected styling, is
// to paint after QStyledItemDelegate::paint() finishes.
class CueRowDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter *p, const QStyleOptionViewItem &opt,
               const QModelIndex &idx) const override
    {
        // Live VU bar in the Level column, painted in place of the
        // default text. Audio cues that aren't currently playing get
        // an empty cell — same row height, no jitter.
        if (idx.column() == core::CueListModel::ColumnLevel) {
            // Background first so hover/selected QSS still shows behind.
            QStyleOptionViewItem fill = opt;
            fill.text.clear();
            QStyledItemDelegate::paint(p, fill, idx);

            const float pl = idx.data(core::CueListModel::PeakLeftRole).toFloat();
            const float pr = idx.data(core::CueListModel::PeakRightRole).toFloat();
            if (pl <= 0.001f && pr <= 0.001f) return;

            auto dbToFrac = [](float lin) -> float {
                if (lin <= 0.001f) return 0.f;
                float db = 20.f * std::log10(lin);
                if (db < -60.f) db = -60.f;
                if (db >  0.f)  db = 0.f;
                return (db + 60.f) / 60.f;
            };
            const QRect r = opt.rect.adjusted(4, 4, -4, -4);
            const int barH = std::max(2, (r.height() - 1) / 2);
            const int leftW  = int(dbToFrac(pl) * r.width());
            const int rightW = int(dbToFrac(pr) * r.width());

            auto bandColor = [](float lin) {
                const float db = 20.f * std::log10(std::max(0.001f, lin));
                if (db > -3.f)  return QColor(0xC2, 0x6A, 0x55);   // red
                if (db > -12.f) return QColor(0xD7, 0xA2, 0x4E);   // amber
                return QColor(0x6F, 0xAE, 0x63);                   // green
            };
            p->save();
            p->fillRect(QRect(r.left(), r.top(), leftW,  barH), bandColor(pl));
            p->fillRect(QRect(r.left(), r.top() + barH + 1, rightW, barH),
                        bandColor(pr));
            p->restore();
            return;
        }

        QStyledItemDelegate::paint(p, opt, idx);

        // Running wash — the model returns a green QBrush via the
        // BackgroundRole only when the cue is in the running set.
        // Use that as a signal to paint the overlay; alpha-blends so
        // selection and hover stay visible underneath.
        const QVariant bg = idx.data(Qt::BackgroundRole);
        if (!bg.canConvert<QBrush>()) return;
        const QBrush br = bg.value<QBrush>();
        if (br.style() == Qt::NoBrush) return;
        const QColor c = br.color();
        // Cheap "is this the running tint?" test: high green channel,
        // moderate alpha. Avoids over-painting the user's chosen cue
        // colours which use lighter() and lower alpha.
        if (c.alpha() >= 60 && c.green() > c.red() && c.green() > c.blue()) {
            p->save();
            p->fillRect(opt.rect, br);
            p->restore();
        }
    }
};

} // namespace

CueListView::CueListView(QWidget *parent)
    : QTreeView(parent)
{
    setRootIsDecorated(true);  // disclosure triangles for group cues
    setUniformRowHeights(true);
    setAllColumnsShowFocus(true);
    setIndentation(14);
    // Per-pixel scroll mode + ScrollPerPixel: SmoothScroll animates
    // the scrollbar's value continuously, but the default ScrollPerItem
    // rounds the visible offset to whole rows — making the animation
    // staircase-jitter on every frame. Per-pixel scrolling lets the
    // glide land on every offset cleanly.
    setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    // Row tint for running cues comes from this delegate — it runs
    // after the QSS-supplied hover/selected background and overlays
    // a green wash when the model says the cue is playing.
    setItemDelegate(new CueRowDelegate(this));
    // No inline stylesheet — the global QSS controls row padding and the
    // hover/selection visuals. An inline override here would interleave
    // with the global rules and break the continuous-row indicator.
    setIconSize(QSize(16, 16));
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    // QLab-style uniform row colour with a thin divider — the old
    // alternating-row banding made the list look noisy on shows with
    // many rows and made it harder to scan a single cue's column
    // values across. The divider is drawn by QSS via border-bottom on
    // QTreeView::item (see quewi-dark.qss).
    setAlternatingRowColors(false);
    setMouseTracking(true);   // hover styling needs cursor-move events
    setAttribute(Qt::WA_Hover, true);
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
                [this]{ viewport()->update(); applyFilter(); });
        connect(model, &QAbstractItemModel::rowsRemoved, this,
                [this]{ viewport()->update(); });
        connect(model, &QAbstractItemModel::modelReset, this,
                [this]{ viewport()->update(); applyFilter(); });
        connect(model, &QAbstractItemModel::dataChanged, this,
                [this]{ applyFilter(); });
        // Column widths picked so a 1440-wide cue panel fits the common
        // columns (state/number/type/name/pre/post/fade) without the
        // Name column getting squeezed. Optional columns (Output, Target,
        // Host, Port, File) are sized for the values they actually hold.
        header()->resizeSection(CueListModel::ColumnState,    32);
        header()->resizeSection(CueListModel::ColumnNumber,   72);
        header()->resizeSection(CueListModel::ColumnType,     90);
        header()->resizeSection(CueListModel::ColumnName,    320);
        header()->resizeSection(CueListModel::ColumnPreWait,  56);
        header()->resizeSection(CueListModel::ColumnPostWait, 56);
        header()->resizeSection(CueListModel::ColumnGain,     56);
        header()->resizeSection(CueListModel::ColumnPan,      56);
        header()->resizeSection(CueListModel::ColumnFadeIn,   64);
        header()->resizeSection(CueListModel::ColumnFadeOut,  64);
        header()->resizeSection(CueListModel::ColumnOutput,  110);
        header()->resizeSection(CueListModel::ColumnTarget,  110);
        header()->resizeSection(CueListModel::ColumnHost,    130);
        header()->resizeSection(CueListModel::ColumnPort,     54);
        header()->resizeSection(CueListModel::ColumnFile,    220);
        header()->resizeSection(CueListModel::ColumnLevel,    96);
        applyColumnVisibility();
    }
}

void CueListView::setShowModeLocked(bool locked)
{
    if (m_showLocked == locked) return;
    m_showLocked = locked;
    // Disable internal drag-reorder by toggling DragEnabled. Drop
    // acceptance stays on (so we can intercept and reject in
    // dropEvent with a helpful status message) but the QTreeView
    // can't initiate a drag from this view.
    setDragEnabled(!locked);
    // Disable Qt's edit triggers entirely while locked. Selection
    // and arrow navigation still work; double-click stops opening
    // an editor. We restore the previous policy on unlock.
    if (locked) {
        m_savedEditTriggers = editTriggers();
        setEditTriggers(QAbstractItemView::NoEditTriggers);
    } else {
        setEditTriggers(m_savedEditTriggers);
    }
}

void CueListView::applyColumnVisibility()
{
    using core::CueListModel;
    QSettings s(QStringLiteral("ServeGaming"), QStringLiteral("quewi"));
    s.beginGroup(QStringLiteral("ui/cueColumns"));
    for (int c = 0; c < CueListModel::ColumnCount; ++c) {
        if (!CueListModel::columnIsOptional(c)) continue;
        // Level column defaults ON — the inline VU meter is the
        // single feature most operators want visible at a glance
        // during a show. Other optional columns default off and
        // stay user-toggled.
        const bool defaultOn = (c == CueListModel::ColumnLevel);
        bool visible = s.value(CueListModel::columnSettingsKey(c), defaultOn).toBool();
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
    // QLab semantics: "next" is the cue the playhead is on — but disarmed
    // cues are skipped on GO, so the real target is the first ARMED cue
    // at or after the playhead. Pressing GO fires it and the caller is
    // responsible for advancing the standby past it.
    auto *m = qobject_cast<core::CueListModel *>(model());
    if (!m || m->rowCount() == 0) return nullptr;
    const auto idx = currentIndex();
    int row = idx.isValid() ? idx.row() : 0;
    if (row < 0) row = 0;
    for (; row < m->rowCount(); ++row) {
        if (auto *c = m->cueAt(m->index(row, 0)))
            if (c->isArmed()) return c;
    }
    return nullptr;
}

void CueListView::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Space) {
        emit goRequested();
        event->accept();
        return;
    }
    // Show-Mode lock — swallow edit-only shortcuts so the operator
    // can't accidentally clobber the cue list mid-show. Selection,
    // GO (Space, handled above), and arrow-key navigation all keep
    // working since they don't mutate the document.
    if (m_showLocked) {
        if (event->matches(QKeySequence::Copy)) {
            // Copy is read-only, allow it — useful for "what's in
            // this cue?" inspection during a long show.
        } else if (event->matches(QKeySequence::Cut)
                   || event->matches(QKeySequence::Paste)
                   || event->key() == Qt::Key_Delete
                   || event->key() == Qt::Key_Backspace
                   || (event->modifiers() == Qt::ControlModifier
                       && event->key() == Qt::Key_D))
        {
            event->accept();
            return;
        }
    }
    // Clipboard shortcuts at the view level so they work without focus
    // on a specific QAction. Selection drives the target list.
    if (event->matches(QKeySequence::Copy) || event->matches(QKeySequence::Cut)
        || event->matches(QKeySequence::Paste)
        || (event->modifiers() == Qt::ControlModifier && event->key() == Qt::Key_D))
    {
        auto *m = qobject_cast<core::CueListModel *>(model());
        if (!m) { QTreeView::keyPressEvent(event); return; }
        QList<cues::Cue *> sel;
        for (const auto &i : selectionModel()->selectedRows())
            if (auto *c = m->cueAt(i)) sel << c;
        const int curRow = currentIndex().isValid() ? currentIndex().row()
                                                    : m->rowCount() - 1;
        if (event->matches(QKeySequence::Copy))      { copyCuesToClipboard(sel); event->accept(); return; }
        if (event->matches(QKeySequence::Cut))       { cutCuesToClipboard(sel, curRow); event->accept(); return; }
        if (event->matches(QKeySequence::Paste))     { pasteCuesFromClipboard(curRow); event->accept(); return; }
        if (event->key() == Qt::Key_D)               { duplicateCues(sel, curRow); event->accept(); return; }
    }
    QTreeView::keyPressEvent(event);
}

void CueListView::dropEvent(QDropEvent *event)
{
    // Show-Mode lock — refuse every drop (external files and internal
    // row reorder alike). Status nudge so the operator understands why
    // their drag didn't take.
    if (m_showLocked) {
        event->ignore();
        if (auto *w = window()) {
            if (auto *mw = qobject_cast<QMainWindow *>(w))
                mw->statusBar()->showMessage(
                    tr("Show Mode: editing locked. Exit Show Mode to add cues."),
                    2500);
        }
        return;
    }
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

void CueListView::setFilterText(const QString &substring)
{
    if (m_filterText == substring) return;
    m_filterText = substring;
    applyFilter();
}

bool CueListView::rowMatchesFilter(int row) const
{
    if (m_filterText.isEmpty()) return true;
    auto *m = qobject_cast<core::CueListModel *>(model());
    if (!m) return true;
    auto *cue = m->cueAt(m->index(row, 0));
    if (!cue) return true;
    const auto needle = m_filterText;
    if (cue->name().contains(needle, Qt::CaseInsensitive)) return true;
    if (cue->typeName().contains(needle, Qt::CaseInsensitive)) return true;
    if (QString::number(cue->number(), 'f', 2).contains(needle)) return true;
    return false;
}

void CueListView::applyFilter()
{
    auto *m = qobject_cast<core::CueListModel *>(model());
    if (!m) return;
    for (int r = 0; r < m->rowCount(); ++r)
        setRowHidden(r, m->index(0, 0).parent(), !rowMatchesFilter(r));
}

void CueListView::contextMenuEvent(QContextMenuEvent *event)
{
    // Show-Mode lock — no context menu at all. The destructive
    // actions live here (insert, delete, renumber) so the cleanest
    // protection is to suppress the menu entirely. The transport
    // bar's GO / Pause / Fade / Panic stay reachable.
    if (m_showLocked) { event->ignore(); return; }

    auto *m = qobject_cast<core::CueListModel *>(model());
    if (!m || !m_workspace) return;

    const QModelIndex idx = indexAt(event->pos());
    auto *cue = m->cueAt(idx);
    if (!cue) {
        // Empty space below the cues — let MainWindow show the "new cue /
        // preferences" menu, which it can build from its cue-creation actions.
        emit emptyAreaContextMenuRequested(event->globalPos());
        return;
    }

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

    menu.addSeparator();

    // ── Arm / Disarm ────────────────────────────────────────────────
    // A disarmed cue is skipped when GO reaches it. Flip the whole
    // selection to one uniform state derived from the first cue, so a
    // mixed selection resolves cleanly. One undoable macro for the batch.
    {
        const bool firstArmed = targets.first() && targets.first()->isArmed();
        const bool newArmed = !firstArmed;
        auto *armAct = menu.addAction(newArmed ? tr("Arm") : tr("Disarm"));
        connect(armAct, &QAction::triggered, this, [this, targets, newArmed] {
            if (!m_workspace) return;
            auto *stack = m_workspace->undoStack();
            stack->beginMacro(newArmed ? tr("Arm cues") : tr("Disarm cues"));
            for (auto *c : targets) {
                if (!c || c->isArmed() == newArmed) continue;
                stack->push(new core::EditCueFieldCommand(
                    c, QStringLiteral("armed"),
                    QVariant::fromValue(c->isArmed()),
                    QVariant::fromValue(newArmed)));
            }
            stack->endMacro();
        });
    }

    menu.addSeparator();

    // ── Cut / Copy / Paste / Duplicate ──────────────────────────────
    // Cut & copy serialize the selection to JSON on the system clipboard
    // (so paste survives across quewi instances). The MIME type is
    // app-specific so other apps don't pick it up by mistake.
    auto *cutAct  = menu.addAction(tr("Cu&t"));
    cutAct->setShortcut(QKeySequence::Cut);
    auto *copyAct = menu.addAction(tr("&Copy"));
    copyAct->setShortcut(QKeySequence::Copy);
    auto *pasteAct = menu.addAction(tr("&Paste"));
    pasteAct->setShortcut(QKeySequence::Paste);
    auto *dupeAct  = menu.addAction(tr("&Duplicate"));
    dupeAct->setShortcut(QKeySequence(QStringLiteral("Ctrl+D")));

    const int currentRow = idx.row();
    connect(copyAct,  &QAction::triggered, this,
            [this, targets]{ copyCuesToClipboard(targets); });
    connect(cutAct,   &QAction::triggered, this,
            [this, targets, currentRow]{ cutCuesToClipboard(targets, currentRow); });
    connect(pasteAct, &QAction::triggered, this,
            [this, currentRow]{ pasteCuesFromClipboard(currentRow); });
    connect(dupeAct,  &QAction::triggered, this,
            [this, targets, currentRow]{ duplicateCues(targets, currentRow); });

    pasteAct->setEnabled(clipboardHasCues());

    menu.addSeparator();
    auto *insertAbove = menu.addAction(tr("Insert &Above"));
    auto *insertBelow = menu.addAction(tr("Insert &Below"));
    connect(insertAbove, &QAction::triggered, this,
            [this, currentRow]{ emit insertRequested(currentRow); });
    connect(insertBelow, &QAction::triggered, this,
            [this, currentRow]{ emit insertRequested(currentRow + 1); });

    // ── Reorder ─────────────────────────────────────────────────────
    // Move the clicked cue up/down one row. Reordering was drag-only
    // before; the menu items make single-step moves discoverable and
    // keyboard-free. MoveCueCommand uses the drag convention where `to`
    // is the insertion index *before* the source is taken, so moving
    // down one means inserting at row+2.
    menu.addSeparator();
    auto *moveUp   = menu.addAction(tr("Move &Up"));
    auto *moveDown = menu.addAction(tr("Move &Down"));
    moveUp->setEnabled(currentRow > 0);
    moveDown->setEnabled(currentRow >= 0 && currentRow < m->rowCount() - 1);
    connect(moveUp, &QAction::triggered, this, [this, m, currentRow]{
        if (!m_workspace || currentRow <= 0) return;
        m_workspace->undoStack()->push(
            new core::MoveCueCommand(m->cueList(), currentRow, currentRow - 1));
    });
    connect(moveDown, &QAction::triggered, this, [this, m, currentRow]{
        if (!m_workspace || currentRow < 0 || currentRow >= m->rowCount() - 1) return;
        m_workspace->undoStack()->push(
            new core::MoveCueCommand(m->cueList(), currentRow, currentRow + 2));
    });

    menu.exec(event->globalPos());
}

namespace {
constexpr const char kCueClipMime[] = "application/x-quewi-cues";
} // namespace

void CueListView::copyCuesToClipboard(const QList<cues::Cue *> &cues) const
{
    if (cues.isEmpty()) return;
    QJsonArray arr;
    for (auto *c : cues) {
        if (!c) continue;
        QJsonObject o;
        o.insert(QStringLiteral("type"),    c->typeKey());
        o.insert(QStringLiteral("payload"), c->toPayload());
        arr.append(o);
    }
    QJsonDocument doc(arr);
    auto *mime = new QMimeData();
    mime->setData(QString::fromLatin1(kCueClipMime), doc.toJson(QJsonDocument::Compact));
    QGuiApplication::clipboard()->setMimeData(mime);
}

void CueListView::cutCuesToClipboard(const QList<cues::Cue *> &cues, int /*currentRow*/)
{
    if (!m_workspace) return;
    copyCuesToClipboard(cues);
    auto *m = qobject_cast<core::CueListModel *>(model());
    if (!m) return;
    auto *list = m->cueList();
    if (!list) return;
    auto *stack = m_workspace->undoStack();
    stack->beginMacro(tr("Cut cues"));
    // Remove top-down so each row index stays valid.
    QList<int> rows;
    for (auto *c : cues) {
        const int r = list->rowOf(c);
        if (r >= 0) rows.append(r);
    }
    std::sort(rows.begin(), rows.end(), std::greater<int>());
    for (int r : rows) stack->push(new core::RemoveCueCommand(list, r));
    stack->endMacro();
}

bool CueListView::clipboardHasCues() const
{
    const auto *mime = QGuiApplication::clipboard()->mimeData();
    return mime && mime->hasFormat(QString::fromLatin1(kCueClipMime));
}

bool CueListView::canPasteCues() const
{
    return clipboardHasCues();
}

void CueListView::pasteCuesAtEnd()
{
    auto *m = qobject_cast<core::CueListModel *>(model());
    // afterRow = last row → pasteCuesFromClipboard inserts at the very end.
    pasteCuesFromClipboard(m ? m->rowCount() - 1 : -1);
}

void CueListView::pasteCuesFromClipboard(int afterRow)
{
    if (!m_workspace) return;
    auto *m = qobject_cast<core::CueListModel *>(model());
    if (!m) return;
    auto *list = m->cueList();
    if (!list) return;

    const auto *mime = QGuiApplication::clipboard()->mimeData();
    if (!mime || !mime->hasFormat(QString::fromLatin1(kCueClipMime))) return;
    const auto bytes = mime->data(QString::fromLatin1(kCueClipMime));
    const auto doc   = QJsonDocument::fromJson(bytes);
    if (!doc.isArray()) return;

    auto *stack = m_workspace->undoStack();
    stack->beginMacro(tr("Paste cues"));
    int insertAt = afterRow + 1;
    for (const auto &v : doc.array()) {
        const auto o = v.toObject();
        const auto type    = o.value(QStringLiteral("type")).toString();
        const auto payload = o.value(QStringLiteral("payload")).toObject();
        auto cue = show::ShowFile::cueFromTypeAndPayload(type, payload);
        if (!cue) continue;
        // Fresh id so the original keeps its identity for goto/start
        // targeting; otherwise paste would alias the source cue's id.
        cue->setId(QUuid::createUuid());
        stack->push(new core::InsertCueCommand(list, insertAt, std::move(cue)));
        ++insertAt;
    }
    stack->endMacro();
    if (insertAt - 1 < m->rowCount())
        setCurrentIndex(m->index(insertAt - 1, 0));
}

void CueListView::duplicateCues(const QList<cues::Cue *> &cues, int afterRow)
{
    // Round-trip through clipboard format keeps the implementation
    // identical to copy+paste — no alternate code path to maintain.
    copyCuesToClipboard(cues);
    pasteCuesFromClipboard(afterRow);
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
    const QString hint  = tr("Press M for a memo · A for audio · O for OSC\n"
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
