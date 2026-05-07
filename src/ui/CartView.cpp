#include "ui/CartView.h"

#include "app/GoEngine.h"
#include "core/CartGrid.h"
#include "core/CueList.h"
#include "core/Workspace.h"
#include "cues/Cue.h"

#include <QAction>
#include <QContextMenuEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QGridLayout>
#include <QInputDialog>
#include <QMenu>
#include <QMimeData>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QUrl>

namespace quewi::ui {

// ---------------------------------------------------------------
// CartCellButton — one tile in the grid.
//
// Subclasses QPushButton so we get hover/press visuals, focus, and
// auto-style from the global QSS for free; overrides paintEvent to
// stack the cue number, name, and a colour stripe inside one tile.
// Accepts file drops; empty cells offer a "drop a sound here" hint.
// ---------------------------------------------------------------
class CartCellButton : public QPushButton {
    Q_OBJECT
public:
    CartCellButton(int row, int col, QWidget *parent = nullptr)
        : QPushButton(parent), m_row(row), m_col(col)
    {
        setAcceptDrops(true);
        setFocusPolicy(Qt::TabFocus);
        setMinimumSize(120, 80);
        setCheckable(false);
        setObjectName(QStringLiteral("cartCell"));
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }

    int row() const { return m_row; }
    int col() const { return m_col; }

    void setCue(cues::Cue *cue) {
        if (m_cue == cue) return;
        m_cue = cue;
        update();
    }
    cues::Cue *cue() const { return m_cue.data(); }

signals:
    void fileDropped(int row, int col, const QString &path);

protected:
    void paintEvent(QPaintEvent *event) override
    {
        // Let the QPushButton base paint hover/pressed/focus visuals
        // first; we only stack our own labels and colour stripe on top.
        QPushButton::paintEvent(event);

        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const QRect r = rect().adjusted(8, 6, -8, -6);

        if (!m_cue) {
            // Empty cell — faint dashed border + drop hint. The base
            // QPushButton already drew a tile background; we just
            // overlay the suggestion text.
            p.setPen(QPen(palette().color(QPalette::Mid), 1, Qt::DashLine));
            p.drawRoundedRect(r.adjusted(2, 2, -2, -2), 4, 4);
            p.setPen(palette().color(QPalette::Mid));
            QFont f = font(); f.setItalic(true); p.setFont(f);
            p.drawText(r, Qt::AlignCenter, tr("drop sound here"));
            return;
        }

        // Cue number (top-left, small mono).
        QFont small = font(); small.setPointSizeF(small.pointSizeF() - 1);
        small.setStyleHint(QFont::Monospace);
        p.setFont(small);
        p.setPen(palette().color(QPalette::Mid));
        p.drawText(r, Qt::AlignTop | Qt::AlignLeft,
                   QStringLiteral("Q%1").arg(m_cue->number(), 0, 'f',
                       qFuzzyCompare(m_cue->number(),
                                     std::round(m_cue->number())) ? 0 : 2));

        // Cue name centered, bold.
        QFont big = font(); big.setBold(true); big.setPointSizeF(big.pointSizeF() + 1);
        p.setFont(big);
        p.setPen(palette().color(QPalette::WindowText));
        const QString text = m_cue->name().isEmpty()
            ? m_cue->typeName()
            : m_cue->name();
        p.drawText(r, Qt::AlignCenter | Qt::TextWordWrap, text);

        // Colour stripe down the left edge if the cue has a colour.
        if (m_cue->color().isValid()) {
            QRect stripe(rect().left() + 2, rect().top() + 4,
                         3, rect().height() - 8);
            p.fillRect(stripe, m_cue->color());
        }
    }

    void dragEnterEvent(QDragEnterEvent *e) override {
        if (e->mimeData()->hasUrls() && !m_cue) e->acceptProposedAction();
    }
    void dragMoveEvent(QDragMoveEvent *e) override {
        if (e->mimeData()->hasUrls() && !m_cue) e->acceptProposedAction();
    }
    void dropEvent(QDropEvent *e) override {
        if (m_cue) return;
        const auto urls = e->mimeData()->urls();
        if (urls.isEmpty()) return;
        const QString path = urls.first().toLocalFile();
        if (path.isEmpty()) return;
        emit fileDropped(m_row, m_col, path);
        e->acceptProposedAction();
    }

private:
    int m_row;
    int m_col;
    QPointer<cues::Cue> m_cue;
};

// ---------------------------------------------------------------
// CartView
// ---------------------------------------------------------------

CartView::CartView(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("cartView"));
    auto *outer = new QGridLayout(this);
    outer->setContentsMargins(8, 8, 8, 8);
    outer->setSpacing(6);
    m_grid = outer;
}

CartView::~CartView() = default;

void CartView::setWorkspace(core::Workspace *ws)
{
    if (m_workspace) {
        if (auto *cart = m_workspace->cart())
            disconnect(cart, nullptr, this, nullptr);
    }
    m_workspace = ws;
    if (auto *cart = ws ? ws->cart() : nullptr) {
        connect(cart, &core::CartGrid::layoutChanged,
                this, &CartView::onLayoutChanged);
    }
    rebuildGrid();
}

void CartView::setGoEngine(GoEngine *engine) { m_goEngine = engine; }

void CartView::onLayoutChanged() { rebuildGrid(); }

void CartView::onCueChanged()
{
    // Cue's name / colour / number changed — find the affected cell
    // and trigger a repaint. Cheap to just repaint the whole cart.
    for (auto *btn : m_cells) btn->update();
}

cues::Cue *CartView::cueForCellId(const QUuid &id) const
{
    if (id.isNull() || !m_workspace) return nullptr;
    for (const auto &list : m_workspace->cueLists()) {
        for (int row = 0; row < list->cueCount(); ++row) {
            auto *c = list->cueAt(row);
            if (c && c->id() == id) return c;
        }
    }
    return nullptr;
}

void CartView::rebuildGrid()
{
    // Wipe and rebuild — cheap for sane cart sizes (~24 cells), and
    // keeps the grid layout in lockstep with size changes.
    while (m_grid->count() > 0) {
        if (auto *w = m_grid->itemAt(0)->widget()) {
            m_grid->removeWidget(w);
            w->deleteLater();
        }
    }
    m_cells.clear();
    if (!m_workspace || !m_workspace->cart()) return;

    auto *cart = m_workspace->cart();
    for (int r = 0; r < cart->rows(); ++r) {
        for (int c = 0; c < cart->cols(); ++c) {
            auto *btn = new CartCellButton(r, c, this);
            const auto id = cart->cueAt(r, c);
            btn->setCue(cueForCellId(id));

            connect(btn, &QPushButton::clicked, this, [this, btn] {
                if (auto *c = btn->cue()) emit fireRequested(c);
            });
            connect(btn, &CartCellButton::fileDropped,
                    this, &CartView::fileDropped);
            // Hook cue change signal so renames / colour swaps repaint
            // the cell live.
            if (auto *c = btn->cue()) {
                connect(c, &cues::Cue::changed, this, &CartView::onCueChanged);
            }

            m_grid->addWidget(btn, r, c);
            m_cells.append(btn);
        }
    }
}

void CartView::contextMenuEvent(QContextMenuEvent *event)
{
    if (!m_workspace || !m_workspace->cart()) return;
    auto *cart = m_workspace->cart();

    QMenu menu(this);
    auto *resize = menu.addAction(tr("Resize cart…"));
    menu.addSeparator();
    QPointer<CartCellButton> hit;
    for (auto *btn : m_cells) {
        if (btn->geometry().contains(btn->mapFromParent(event->pos()))
            || btn->underMouse()) {
            hit = btn;
            break;
        }
    }
    QAction *unbind = nullptr;
    if (hit && hit->cue()) {
        unbind = menu.addAction(tr("Unbind cell (Q%1 stays in cue list)")
                                    .arg(hit->cue()->number()));
    }

    auto *picked = menu.exec(event->globalPos());
    if (!picked) return;
    if (picked == resize) {
        bool ok = false;
        const auto rows = QInputDialog::getInt(this, tr("Resize cart"),
            tr("Rows:"), cart->rows(), 1, 16, 1, &ok);
        if (!ok) return;
        const auto cols = QInputDialog::getInt(this, tr("Resize cart"),
            tr("Columns:"), cart->cols(), 1, 12, 1, &ok);
        if (!ok) return;
        cart->setSize(rows, cols);
    } else if (unbind && picked == unbind && hit) {
        cart->clearCell(hit->row(), hit->col());
    }
}

} // namespace quewi::ui

#include "CartView.moc"
