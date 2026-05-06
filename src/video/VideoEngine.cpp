#include "video/VideoEngine.h"

#include "video/Compositor.h"
#include "video/ImageLayer.h"
#include "video/Layer.h"
#include "video/TestPatternLayer.h"
#include "video/TextLayer.h"
#include "video/VideoLayer.h"

#include <algorithm>

namespace quewi::video {

VideoEngine::VideoEngine(QObject *parent)
    : QObject(parent)
    , m_compositor(std::make_unique<Compositor>(this))
{
    qRegisterMetaType<VideoVoiceId>("quewi::video::VideoVoiceId");
}

VideoEngine::~VideoEngine() { stopAll(); }

VideoVoiceId VideoEngine::fire(const VideoVoiceParams &params)
{
    Layer *layer = nullptr;
    switch (params.kind) {
    case VideoVoiceParams::Video:
        layer = new VideoLayer(params.filePath, params.loop);
        break;
    case VideoVoiceParams::Image:
        layer = new ImageLayer(params.filePath);
        break;
    case VideoVoiceParams::Text:
        layer = new TextLayer(params.text, params.fontPixelSize, params.textColor);
        break;
    }
    if (!layer) return 0;

    layer->setGeometry(params.geometry);
    layer->setOpacity(params.opacity);
    layer->setZOrder(params.zOrder);

    m_compositor->addLayer(params.screenIndex, layer);

    const VideoVoiceId id = ++m_nextId;
    m_voices.push_back({id, layer});

    // Capture the raw pointer because QPointer can become null between
    // the signal firing and the slot running; we look it up by id.
    Layer *layerPtr = layer;
    connect(layer, &Layer::finished, this, [this, layerPtr]() {
        onLayerFinished(layerPtr);
    });
    return id;
}

void VideoEngine::stop(VideoVoiceId id)
{
    auto it = std::find_if(m_voices.begin(), m_voices.end(),
        [id](const Voice &v) { return v.id == id; });
    if (it == m_voices.end()) return;
    if (auto *layer = it->layer.data()) {
        m_compositor->removeLayer(layer);
    }
    const auto vid = it->id;
    m_voices.erase(it);
    emit voiceFinished(vid);
}

void VideoEngine::stopAll()
{
    auto voices = std::move(m_voices);
    m_voices.clear();
    if (m_compositor) m_compositor->clear();
    for (auto &v : voices) {
        emit voiceFinished(v.id);
    }
}

void VideoEngine::setCornerPin(int screenIndex, const QPolygonF &quad)
{
    if (m_compositor) m_compositor->setCornerPin(screenIndex, quad);
}

QPolygonF VideoEngine::cornerPin(int screenIndex) const
{
    return m_compositor ? m_compositor->cornerPin(screenIndex) : QPolygonF();
}

void VideoEngine::showTestPattern(int screenIndex)
{
    if (!m_compositor) return;
    if (auto it = m_testPatterns.find(screenIndex); it != m_testPatterns.end()) {
        if (it.value()) return;       // already showing
        m_testPatterns.erase(it);     // stale pointer; rebuild
    }
    auto *layer = new TestPatternLayer();
    m_compositor->addLayer(screenIndex, layer);
    m_testPatterns.insert(screenIndex, layer);
}

void VideoEngine::hideTestPattern(int screenIndex)
{
    auto it = m_testPatterns.find(screenIndex);
    if (it == m_testPatterns.end()) return;
    if (Layer *layer = it.value().data()) {
        m_compositor->removeLayer(layer);
    }
    m_testPatterns.erase(it);
}

bool VideoEngine::hasTestPattern(int screenIndex) const
{
    auto it = m_testPatterns.constFind(screenIndex);
    return it != m_testPatterns.constEnd() && it.value();
}

void VideoEngine::onLayerFinished(Layer *layer)
{
    auto it = std::find_if(m_voices.begin(), m_voices.end(),
        [layer](const Voice &v) { return v.layer.data() == layer; });
    if (it == m_voices.end()) return;
    const auto id = it->id;
    if (auto *l = it->layer.data()) m_compositor->removeLayer(l);
    m_voices.erase(it);
    emit voiceFinished(id);
}

} // namespace quewi::video
