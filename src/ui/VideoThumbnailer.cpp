#include "ui/VideoThumbnailer.h"

#include <QMediaPlayer>
#include <QUrl>
#include <QVideoFrame>
#include <QVideoSink>
#include <algorithm>
#include <cmath>

namespace quewi::ui {

VideoThumbnailer::VideoThumbnailer(QObject *parent) : QObject(parent)
{
    m_timeout.setSingleShot(true);
    m_timeout.setInterval(2000);
    // A frame that never comes (a bad spot in the file): skip it.
    connect(&m_timeout, &QTimer::timeout, this, [this] {
        if (!m_running) return;
        m_waiting = false;
        ++m_index;
        requestNext();
    });
}

VideoThumbnailer::~VideoThumbnailer() { stop(); }

void VideoThumbnailer::stop()
{
    m_running = false;
    m_waiting = false;
    m_timeout.stop();
    if (m_player) {
        m_player->stop();
        m_player->deleteLater();
    }
    if (m_sink) m_sink->deleteLater();
    m_player = nullptr;
    m_sink = nullptr;
}

void VideoThumbnailer::start(const QString &path, double spacingSeconds, int maxCount, int height)
{
    stop();
    m_spacing = std::max(0.1, spacingSeconds);
    m_maxCount = std::max(1, maxCount);
    m_height = std::max(16, height);
    m_times.clear();
    m_index = 0;
    m_started = false;
    m_running = true;

    m_player = new QMediaPlayer(this);
    m_sink = new QVideoSink(this);
    m_player->setVideoSink(m_sink);   // no audio output: silent
    connect(m_sink, &QVideoSink::videoFrameChanged, this, &VideoThumbnailer::onFrame);
    connect(m_player, &QMediaPlayer::mediaStatusChanged, this,
            [this](QMediaPlayer::MediaStatus s) {
                if (!m_running) return;
                if (s == QMediaPlayer::InvalidMedia) {
                    const QString why = m_player ? m_player->errorString() : QString();
                    stop();
                    emit failed(why);
                } else if (s == QMediaPlayer::LoadedMedia && !m_started) {
                    m_started = true;
                    begin();
                }
            });
    m_player->setSource(QUrl::fromLocalFile(path));
}

void VideoThumbnailer::begin()
{
    const double dur = m_player ? m_player->duration() / 1000.0 : 0.0;
    emit durationKnown(dur);
    if (dur <= 0.0 || !m_player->hasVideo()) {
        stop();
        emit failed(tr("No picture in this file"));
        return;
    }
    const int count = std::clamp(int(std::ceil(dur / m_spacing)), std::min(12, m_maxCount), m_maxCount);
    // Frames at the middle of each slot, coarse pass first (every 4th), so a
    // long file shows its whole shape quickly and the gaps fill after.
    std::vector<double> all;
    for (int i = 0; i < count; ++i) all.push_back((i + 0.5) * dur / count);
    for (int pass = 0; pass < 4; ++pass)
        for (int i = pass == 0 ? 0 : (pass == 1 ? 2 : (pass == 2 ? 1 : 3)); i < count; i += 4)
            m_times.push_back(all[size_t(i)]);
    m_player->pause();   // paused: each seek delivers exactly the frame there
    requestNext();
}

void VideoThumbnailer::requestNext()
{
    if (!m_running || !m_player) return;
    if (m_index >= m_times.size()) {
        stop();
        emit finished();
        return;
    }
    m_waiting = true;
    m_player->setPosition(qint64(m_times[m_index] * 1000.0));
    m_timeout.start();
}

void VideoThumbnailer::onFrame(const QVideoFrame &frame)
{
    if (!m_running || !m_waiting || !frame.isValid() || m_index >= m_times.size()) return;
    // A frame from before the seek landed isn't the one we asked for. The
    // frame shown can start up to a keyframe gap early; allow a second and a
    // half, or any frame when the backend doesn't stamp them.
    const double target = m_times[m_index];
    if (frame.startTime() >= 0 && std::abs(frame.startTime() / 1e6 - target) > 1.5) return;
    QImage img = frame.toImage();
    if (img.isNull()) return;
    img = img.scaledToHeight(m_height, Qt::SmoothTransformation)
             .convertToFormat(QImage::Format_RGB32);
    m_waiting = false;
    m_timeout.stop();
    emit thumbnailReady(target, img);
    ++m_index;
    // Next seek from the event loop, not inside the sink's signal.
    QMetaObject::invokeMethod(this, &VideoThumbnailer::requestNext, Qt::QueuedConnection);
}

} // namespace quewi::ui
