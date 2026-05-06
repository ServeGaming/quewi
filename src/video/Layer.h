#pragma once

#include <QImage>
#include <QObject>
#include <QPolygonF>
#include <QRectF>

namespace quewi::video {

// Render-state for one visual cue inside a CompositorWindow. The
// Compositor owns the lifetime; cue data flows in through update*()
// setters from VideoEngine when the user nudges the inspector.
//
// Subclasses produce a QImage on demand (currentFrame); the
// CompositorWindow draws it scaled into `geometry`. opacity, geometry,
// and z-order live on the base class because they're identical for
// every kind of visual cue.
class Layer : public QObject {
    Q_OBJECT
public:
    explicit Layer(QObject *parent = nullptr) : QObject(parent) {}
    ~Layer() override = default;

    // Normalised 0..1 rect inside the CompositorWindow.
    QRectF  geometry()    const { return m_geometry; }
    void setGeometry(const QRectF &g) { m_geometry = g; emit changed(); }

    double opacity() const { return m_opacity; }
    void   setOpacity(double o) { m_opacity = std::clamp(o, 0.0, 1.0); emit changed(); }

    int  zOrder() const { return m_z; }
    void setZOrder(int z) { m_z = z; emit changed(); }

    // Subclasses override to provide the current frame. May return a
    // null image while loading; the window then skips this layer.
    virtual QImage currentFrame() const = 0;

    // Subclasses override to release any heavy resources before the
    // Compositor drops the layer. Default is no-op.
    virtual void teardown() {}

signals:
    void changed();           // geometry / opacity / z-order changed
    void frameAvailable();    // current frame moved on (drives repaint)
    void finished();           // playback ended (video only)

protected:
    QRectF m_geometry { 0.0, 0.0, 1.0, 1.0 };
    double m_opacity = 1.0;
    int    m_z       = 0;
};

} // namespace quewi::video
