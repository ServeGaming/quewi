#pragma once

#include <QHash>
#include <QObject>
#include <QPointer>

namespace quewi::video {

class CompositorWindow;
class Layer;

// Owns one CompositorWindow per active screen. VideoEngine calls
// addLayer(screenIndex, layer) to surface a cue's visual; the compositor
// lazily creates the window on first use, hides it when empty.
//
// Per-screen corner-pin transforms are also stored here and applied to
// the matching CompositorWindow — projection mapping is a property of
// the output, not the cue.
class Compositor : public QObject {
    Q_OBJECT
public:
    explicit Compositor(QObject *parent = nullptr);
    ~Compositor() override;

    void addLayer(int screenIndex, Layer *layer);
    // Remove a layer. By default the window is KEPT (showing black) when its
    // last layer leaves, so a finished video/projection cue holds black on
    // the projector instead of flashing the desktop — it stays black until a
    // new cue plays or Panic (clear()) releases the output. Pass
    // releaseWindowIfEmpty=true for non-cue layers (e.g. the calibration test
    // pattern) that should let the window close when they're the last to go.
    void removeLayer(Layer *layer, bool releaseWindowIfEmpty = false);
    void clear();

    // Per-screen corner-pin. Quad is in normalised 0..1 window space.
    void setCornerPin(int screenIndex, const QPolygonF &quad);
    QPolygonF cornerPin(int screenIndex) const;

    int activeWindowCount() const { return m_windows.size(); }

private:
    CompositorWindow *windowFor(int screenIndex);

    QHash<int, CompositorWindow *> m_windows;       // screenIndex → window
    QHash<Layer *, int>            m_layerScreen;   // layer → screenIndex
};

} // namespace quewi::video
