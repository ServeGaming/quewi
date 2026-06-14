#pragma once

#include <QList>
#include <QMetaObject>
#include <QPointer>
#include <QPolygonF>
#include <QWidget>

namespace quewi::video {

class Layer;

// Frameless top-level window that paints one or more Layers on a
// chosen screen. Multiple Layers blend by z-order (higher = above);
// each Layer's geometry is normalised to the window rect.
//
// Optional 4-point corner-pin maps the window's logical (0..1)
// space onto an arbitrary quad, so projector misalignment can be
// corrected without physically moving the projector. Identity quad
// (corners at (0,0),(1,0),(1,1),(0,1)) means no warp.
//
// Repaint is driven by the layers — they emit frameAvailable when
// new content is ready (video frame decoded, image loaded, text
// changed) and the window calls update().
class CompositorWindow : public QWidget {
    Q_OBJECT
public:
    explicit CompositorWindow(int screenIndex, QWidget *parent = nullptr);
    ~CompositorWindow() override;

    int  screenIndex() const { return m_screenIndex; }

    void addLayer(Layer *layer);     // takes ownership; deleted on remove
    void removeLayer(Layer *layer);
    int  layerCount() const { return static_cast<int>(m_layers.size()); }

    // Identity = {(0,0),(1,0),(1,1),(0,1)}. Other quads map the unit
    // square onto a perspective-warped projection target.
    void  setCornerPin(const QPolygonF &quad);
    QPolygonF cornerPin() const { return m_pin; }
    void resetCornerPin();

protected:
    void paintEvent(QPaintEvent *) override;

private:
    void layoutOnScreen();
    void rebuildPinTransform();

    int m_screenIndex = 0;
    QList<QPointer<Layer>> m_layers;
    QPolygonF m_pin;
    QTransform m_pinTransform;   // cached
    bool m_pinIsIdentity = true;
    // Tracks the current target QScreen's geometryChanged connection so it
    // can be re-wired when the target screen changes (display reorder).
    QMetaObject::Connection m_screenGeomConn;
};

} // namespace quewi::video
