#include "ui/CornerPinEditor.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPolygonF>

#include <algorithm>

namespace quewi::ui {

namespace {
QPolygonF identityQuad() {
    return QPolygonF{ {0.0, 0.0}, {1.0, 0.0}, {1.0, 1.0}, {0.0, 1.0} };
}
constexpr double kHandleRadius = 7.0;
constexpr double kHitSlop      = 12.0;
// Pixel radius for snap-to-corner. Tight enough that the user can park
// just past a corner deliberately, generous enough that aiming with a
// shaky cursor still snaps cleanly.
constexpr double kSnapPixels   = 12.0;
} // namespace

CornerPinEditor::CornerPinEditor(QWidget *parent)
    : QWidget(parent), m_corners(identityQuad())
{
    setMinimumSize(minimumSizeHint());
    setMouseTracking(true);
    setCursor(Qt::CrossCursor);
}

void CornerPinEditor::setCorners(const QPolygonF &normalised)
{
    if (normalised.size() != 4) return;
    m_corners = normalised;
    update();
}

void CornerPinEditor::reset()
{
    m_corners = identityQuad();
    emit cornersChanged(m_corners);
    update();
}

QRectF CornerPinEditor::stageRect() const
{
    // 16:9 inset rectangle centred in the widget.
    const QSizeF avail(width() - 24.0, height() - 24.0);
    const double aspect = 16.0 / 9.0;
    double w = avail.width();
    double h = w / aspect;
    if (h > avail.height()) {
        h = avail.height();
        w = h * aspect;
    }
    const double x = (width()  - w) * 0.5;
    const double y = (height() - h) * 0.5;
    return { x, y, w, h };
}

QPointF CornerPinEditor::normalisedToPixel(const QPointF &p) const
{
    const auto r = stageRect();
    return { r.x() + p.x() * r.width(), r.y() + p.y() * r.height() };
}

QPointF CornerPinEditor::pixelToNormalised(const QPointF &p) const
{
    const auto r = stageRect();
    if (r.width() < 1 || r.height() < 1) return {};
    // Allow handles outside the stage rect (-0.5 .. 1.5) so the user
    // can warp inward and outward — projection mapping isn't always
    // a sub-rectangle of the source.
    const double nx = (p.x() - r.x()) / r.width();
    const double ny = (p.y() - r.y()) / r.height();
    return { std::clamp(nx, -0.5, 1.5), std::clamp(ny, -0.5, 1.5) };
}

int CornerPinEditor::handleAt(const QPointF &pixelPos) const
{
    int best = -1;
    double bestDist = kHitSlop * kHitSlop;
    for (int i = 0; i < m_corners.size(); ++i) {
        const QPointF h = normalisedToPixel(m_corners[i]);
        const double dx = h.x() - pixelPos.x();
        const double dy = h.y() - pixelPos.y();
        const double d2 = dx * dx + dy * dy;
        if (d2 < bestDist) { bestDist = d2; best = i; }
    }
    return best;
}

void CornerPinEditor::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const QRectF r = stageRect();
    const QColor bg     = palette().color(QPalette::Base);
    const QColor stage  = palette().color(QPalette::AlternateBase);
    const QColor edge   = palette().color(QPalette::Mid);
    const QColor accent = palette().color(QPalette::Highlight);

    p.fillRect(rect(), bg);

    // Reference rectangle (the un-warped output target).
    p.setPen(QPen(edge, 1, Qt::DashLine));
    p.setBrush(stage);
    p.drawRect(r);

    // Warped quad polygon.
    QPolygonF warped;
    for (const auto &c : m_corners) warped << normalisedToPixel(c);
    QColor fill = accent; fill.setAlpha(40);
    p.setBrush(fill);
    p.setPen(QPen(accent, 2));
    p.drawPolygon(warped);

    // Handles. The snapped handle (if any) gets a brighter halo so the
    // user has feedback that snap kicked in.
    p.setPen(QPen(palette().color(QPalette::Text), 1));
    for (int i = 0; i < warped.size(); ++i) {
        if (i == m_snappedHandle) {
            QColor halo = accent;
            halo.setAlpha(90);
            p.setBrush(halo);
            p.setPen(Qt::NoPen);
            p.drawEllipse(warped[i], kHandleRadius + 6, kHandleRadius + 6);
            p.setPen(QPen(palette().color(QPalette::Text), 1));
        }
        p.setBrush(accent);
        p.drawEllipse(warped[i], kHandleRadius, kHandleRadius);
    }

    // Help text.
    p.setPen(palette().color(QPalette::Text));
    p.drawText(QPointF(8, height() - 8),
               tr("Drag corners to warp. Hold Shift to disable snap. "
                  "Double-click to reset."));
}

void CornerPinEditor::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton) {
        m_dragHandle = handleAt(e->position());
    }
}

void CornerPinEditor::mouseMoveEvent(QMouseEvent *e)
{
    if (m_dragHandle < 0) {
        // Hover feedback — change cursor when over a handle.
        const int h = handleAt(e->position());
        setCursor(h >= 0 ? Qt::SizeAllCursor : Qt::CrossCursor);
        return;
    }

    QPointF normalised = pixelToNormalised(e->position());

    // Snap to one of the four canonical corners when the cursor is
    // within kSnapPixels of it in screen space. Holding Shift disables
    // snap so the operator can park just past a corner — useful for
    // overshooting projector beds.
    int snapTo = -1;
    if (!(e->modifiers() & Qt::ShiftModifier)) {
        const QPolygonF anchors = identityQuad();
        const QPointF cursor = e->position();
        double bestD2 = kSnapPixels * kSnapPixels;
        for (int i = 0; i < anchors.size(); ++i) {
            const QPointF anchorPx = normalisedToPixel(anchors[i]);
            const double dx = anchorPx.x() - cursor.x();
            const double dy = anchorPx.y() - cursor.y();
            const double d2 = dx * dx + dy * dy;
            if (d2 < bestD2) { bestD2 = d2; snapTo = i; }
        }
        if (snapTo >= 0) normalised = anchors[snapTo];
    }
    m_snappedHandle = (snapTo >= 0) ? m_dragHandle : -1;

    m_corners[m_dragHandle] = normalised;
    emit cornersChanged(m_corners);
    update();
}

void CornerPinEditor::mouseReleaseEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton) {
        m_dragHandle = -1;
        m_snappedHandle = -1;
        update();
    }
}

void CornerPinEditor::mouseDoubleClickEvent(QMouseEvent *)
{
    reset();
}

} // namespace quewi::ui
