#include "video/VideoEngine.h"

#include "video/VideoOutputWindow.h"

#include <algorithm>

namespace quewi::video {

VideoEngine::VideoEngine(QObject *parent) : QObject(parent)
{
    qRegisterMetaType<VideoVoiceId>("quewi::video::VideoVoiceId");
}

VideoEngine::~VideoEngine() { stopAll(); }

VideoVoiceId VideoEngine::fire(const VideoVoiceParams &params)
{
    auto *window = new VideoOutputWindow(params);
    window->show();
    window->raise();

    const VideoVoiceId id = ++m_nextId;
    m_voices.push_back({id, window});

    connect(window, &VideoOutputWindow::finished, this, [this, window]() {
        onWindowFinished(window);
    });
    return id;
}

void VideoEngine::stop(VideoVoiceId id)
{
    auto it = std::find_if(m_voices.begin(), m_voices.end(),
        [id](const Voice &v) { return v.id == id; });
    if (it == m_voices.end()) return;
    if (it->window) it->window->deleteLater();
    const auto vid = it->id;
    m_voices.erase(it);
    emit voiceFinished(vid);
}

void VideoEngine::stopAll()
{
    auto voices = std::move(m_voices);
    m_voices.clear();
    for (auto &v : voices) {
        if (v.window) v.window->deleteLater();
        emit voiceFinished(v.id);
    }
}

void VideoEngine::onWindowFinished(VideoOutputWindow *window)
{
    auto it = std::find_if(m_voices.begin(), m_voices.end(),
        [window](const Voice &v) { return v.window == window; });
    if (it == m_voices.end()) return;
    const auto id = it->id;
    if (it->window) it->window->deleteLater();
    m_voices.erase(it);
    emit voiceFinished(id);
}

} // namespace quewi::video
