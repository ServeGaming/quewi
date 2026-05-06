#include "video/Compositor.h"

#include "video/CompositorWindow.h"
#include "video/Layer.h"

namespace quewi::video {

Compositor::Compositor(QObject *parent) : QObject(parent) {}

Compositor::~Compositor()
{
    clear();
}

CompositorWindow *Compositor::windowFor(int screenIndex)
{
    auto it = m_windows.find(screenIndex);
    if (it != m_windows.end()) return it.value();
    auto *win = new CompositorWindow(screenIndex);
    m_windows.insert(screenIndex, win);
    return win;
}

void Compositor::addLayer(int screenIndex, Layer *layer)
{
    if (!layer) return;
    auto *win = windowFor(screenIndex);
    win->addLayer(layer);
    m_layerScreen.insert(layer, screenIndex);
    if (!win->isVisible()) win->show();
}

void Compositor::removeLayer(Layer *layer)
{
    if (!layer) return;
    auto it = m_layerScreen.find(layer);
    if (it == m_layerScreen.end()) return;
    const int screen = it.value();
    m_layerScreen.erase(it);

    auto winIt = m_windows.find(screen);
    if (winIt != m_windows.end() && winIt.value()) {
        winIt.value()->removeLayer(layer);
        // Hide the window when the last layer leaves so a black
        // rectangle doesn't sit on the projector between cues.
        if (winIt.value()->layerCount() == 0) {
            winIt.value()->hide();
        }
    }
}

void Compositor::clear()
{
    for (auto *win : m_windows) {
        if (win) {
            win->hide();
            win->deleteLater();
        }
    }
    m_windows.clear();
    m_layerScreen.clear();
}

void Compositor::setCornerPin(int screenIndex, const QPolygonF &quad)
{
    auto *win = windowFor(screenIndex);
    win->setCornerPin(quad);
}

QPolygonF Compositor::cornerPin(int screenIndex) const
{
    auto it = m_windows.find(screenIndex);
    if (it == m_windows.end() || !it.value()) return {};
    return it.value()->cornerPin();
}

} // namespace quewi::video
