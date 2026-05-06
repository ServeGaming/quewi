#include "video/VideoLayer.h"

#include <QAudioOutput>
#include <QMediaPlayer>
#include <QUrl>
#include <QVideoFrame>
#include <QVideoSink>

namespace quewi::video {

VideoLayer::VideoLayer(const QString &filePath, bool loop, QObject *parent)
    : Layer(parent), m_path(filePath), m_loop(loop)
{
    m_player   = new QMediaPlayer(this);
    m_audioOut = new QAudioOutput(this);
    m_audioOut->setMuted(true);   // routing audio is the AudioCue's job
    m_sink     = new QVideoSink(this);

    m_player->setAudioOutput(m_audioOut);
    m_player->setVideoSink(m_sink);
    m_player->setSource(QUrl::fromLocalFile(m_path));
    if (m_loop) m_player->setLoops(QMediaPlayer::Infinite);

    connect(m_sink, &QVideoSink::videoFrameChanged,
            this, [this](const QVideoFrame &f) {
                if (!f.isValid()) return;
                QImage img = f.toImage();
                // Convert to a premultiplied 32-bit format once here so
                // every paintEvent in the compositor blits without an
                // implicit format conversion. Costs a few ms per frame
                // up front; saves more on every screen redraw.
                if (!img.isNull()
                    && img.format() != QImage::Format_ARGB32_Premultiplied
                    && img.format() != QImage::Format_RGB32)
                {
                    img = img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
                }
                m_frame = std::move(img);
                emit frameAvailable();
            });
    connect(m_player, &QMediaPlayer::mediaStatusChanged,
            this, [this](QMediaPlayer::MediaStatus s) {
                onMediaStatus(static_cast<int>(s));
            });

    m_player->play();
}

VideoLayer::~VideoLayer() = default;

void VideoLayer::teardown()
{
    if (m_player) m_player->stop();
}

void VideoLayer::onMediaStatus(int status)
{
    if (status == QMediaPlayer::EndOfMedia && !m_loop) {
        emit finished();
    }
}

} // namespace quewi::video
