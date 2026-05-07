#include "ui/ScriptPdfView.h"

#include "core/CueList.h"
#include "core/ScriptModel.h"
#include "core/Workspace.h"
#include "cues/Cue.h"

#include <QEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPdfDocument>
#include <QPdfPageNavigator>
#include <QPointF>
#include <QScrollBar>
#include <QSizeF>

namespace quewi::ui {

ScriptPdfView::ScriptPdfView(QWidget *parent)
    : QPdfView(parent)
{
    m_doc = new QPdfDocument(this);
    setDocument(m_doc);
    setPageMode(QPdfView::PageMode::MultiPage);
    setZoomMode(QPdfView::ZoomMode::FitToWidth);
}

ScriptPdfView::~ScriptPdfView() = default;

void ScriptPdfView::setWorkspace(core::Workspace *ws)
{
    if (m_workspace == ws) return;
    m_workspace = ws;
    if (auto *m = ws ? ws->scriptModel() : nullptr) {
        connect(m, &core::ScriptModel::scriptChanged,
                this, &ScriptPdfView::onScriptChanged);
        connect(m, &core::ScriptModel::annotationsChanged,
                this, &ScriptPdfView::onAnnotationsChanged);
    }
    onScriptChanged();
}

void ScriptPdfView::reloadDocument()
{
    onScriptChanged();
}

void ScriptPdfView::onScriptChanged()
{
    if (!m_doc) return;
    if (!m_workspace || !m_workspace->scriptModel()
        || m_workspace->scriptModel()->format() != core::ScriptModel::Format::Pdf) {
        m_doc->close();
        viewport()->update();
        return;
    }
    const QString path = m_workspace->scriptModel()->path();
    if (path.isEmpty()) {
        m_doc->close();
    } else {
        m_doc->load(path);
    }
    viewport()->update();
}

void ScriptPdfView::onAnnotationsChanged()
{
    viewport()->update();
}

void ScriptPdfView::setNextCue(const QUuid &cueId)
{
    if (m_nextCue == cueId) return;
    m_nextCue = cueId;
    viewport()->update();
}

void ScriptPdfView::scrollToCue(const QUuid &cueId)
{
    m_runningCue = cueId;
    if (!m_workspace || !m_workspace->scriptModel() || !m_doc) {
        viewport()->update();
        return;
    }
    auto *m = m_workspace->scriptModel();
    const int idx = m->annotationIndexForCue(cueId);
    if (idx < 0) { viewport()->update(); return; }
    const auto &a = m->annotations()[idx];
    if (a.line > 0) return;   // text annotation, not for this view
    if (auto *nav = pageNavigator()) {
        const QSizeF sz = m_doc->pagePointSize(a.page);
        // y=0 means top — centre it ish; QPdfView jumps so the requested
        // point is at the top, so subtract a half-viewport's worth.
        const double y = a.yFraction * sz.height();
        nav->jump(a.page, QPointF(0, qMax(0.0, y - 60.0)), 1.0);
    }
    viewport()->update();
}

bool ScriptPdfView::pointToLocation(const QPoint &viewportPoint,
                                    int *pageOut, double *yFracOut) const
{
    // Unfortunately QPdfView has no public hit-test from viewport coords
    // to (page, point). We approximate using the navigator's currentPage
    // plus the viewport position relative to a fixed anchor. This is a
    // 90% solution: clicking inside the currently dominant page binds
    // to that page; y is measured against the viewport height.
    if (!m_doc || m_doc->pageCount() <= 0) return false;
    auto *nav = const_cast<ScriptPdfView*>(this)->pageNavigator();
    if (!nav) return false;
    *pageOut  = nav->currentPage();
    const double h = std::max(1, viewport()->height());
    *yFracOut = qBound(0.0, double(viewportPoint.y()) / h, 1.0);
    return true;
}

QRect ScriptPdfView::locationToRect(int page, double yFraction) const
{
    if (!m_doc) return {};
    auto *nav = const_cast<ScriptPdfView*>(this)->pageNavigator();
    if (!nav) return {};
    if (nav->currentPage() != page) return {};   // not visible
    const double h = viewport()->height();
    const int y = qBound(0, int(yFraction * h), int(h) - 4);
    return QRect(0, y - 1, viewport()->width(), 3);
}

void ScriptPdfView::mousePressEvent(QMouseEvent *event)
{
    if (m_mode != Mode::Edit || !m_workspace || !m_workspace->scriptModel()) {
        QPdfView::mousePressEvent(event);
        return;
    }
    auto *m = m_workspace->scriptModel();
    int page = 0;
    double y = 0;
    if (pointToLocation(event->pos(), &page, &y)) {
        if (event->button() == Qt::LeftButton && !m_selectedCue.isNull()) {
            // Toggle: if same cue + ~same y on same page, unbind.
            const int existing = m->annotationIndexForCue(m_selectedCue);
            if (existing >= 0) {
                const auto &ex = m->annotations()[existing];
                if (ex.page == page && std::abs(ex.yFraction - y) < 0.02) {
                    m->removeAnnotation(m_selectedCue);
                    QPdfView::mousePressEvent(event);
                    return;
                }
            }
            m->setPdfAnnotation(m_selectedCue, page, y);
        }
    }
    QPdfView::mousePressEvent(event);
}

void ScriptPdfView::paintEvent(QPaintEvent *event)
{
    // Let QPdfView render the page(s) into the viewport first, then
    // overlay our annotation tabs on top via a fresh painter. Painting
    // through an event filter would have suppressed QPdfView's own
    // paintEvent (Qt routes scroll-area paints via virtuals, not via
    // filtered events), which is why the viewer was blank in v0.6.0.
    QPdfView::paintEvent(event);

    if (!m_workspace || !m_workspace->scriptModel()) return;
    QPainter p(viewport());
    const auto *m = m_workspace->scriptModel();
    const QColor running = QColor(0x6F, 0xAE, 0x63, 200);
    const QColor next    = QColor(0xD7, 0xA2, 0x4E, 200);
    const QColor accent  = QColor(0xC5, 0x8B, 0x4A, 160);
    for (const auto &a : m->annotations()) {
        if (a.line > 0) continue;
        const QRect r = locationToRect(a.page, a.yFraction);
        if (r.isEmpty()) continue;
        QColor c = accent;
        if      (a.cueId == m_runningCue) c = running;
        else if (a.cueId == m_nextCue)    c = next;
        p.fillRect(r, c);
        // Cue label dot on the left side.
        p.fillRect(QRect(0, r.center().y() - 6, 5, 12), c);
    }
}

} // namespace quewi::ui
