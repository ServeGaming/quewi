#pragma once

#include <QPolygonF>
#include <QWidget>

namespace quewi::ui {

// 4-point corner-pin editor. Renders a 16:9 preview rectangle inside
// the widget; the four corners are draggable handles that map onto
// normalised 0..1 window space. The widget emits cornersChanged
// whenever the user drags a handle so the live compositor warp
// updates in real time.
//
// Conventions match CompositorWindow:
//   corners()[0] = top-left
//   corners()[1] = top-right
//   corners()[2] = bottom-right
//   corners()[3] = bottom-left
class CornerPinEditor : public QWidget {
    Q_OBJECT
public:
    explicit CornerPinEditor(QWidget *parent = nullptr);

    QPolygonF corners() const { return m_corners; }
    void      setCorners(const QPolygonF &normalised);
    void      reset();      // identity quad

    QSize sizeHint() const override { return QSize(280, 200); }
    QSize minimumSizeHint() const override { return QSize(220, 160); }

signals:
    void cornersChanged(const QPolygonF &normalised);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;

private:
    QPointF normalisedToPixel(const QPointF &p) const;
    QPointF pixelToNormalised(const QPointF &p) const;
    int     handleAt(const QPointF &pixelPos) const;
    QRectF  stageRect() const;       // 16:9 preview area

    QPolygonF m_corners;             // normalised 0..1
    int       m_dragHandle = -1;
};

} // namespace quewi::ui
