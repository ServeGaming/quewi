#include "video/VideoLayer.h"

#include <QAudioOutput>
#include <QMediaPlayer>
#include <QUrl>
#include <QVideoFrame>
#include <QVideoSink>

namespace quewi::video {

VideoLayer::VideoLayer(const QString &filePath, const PictureTiming &timing, QObject *parent)
    : Layer(parent), m_path(filePath), m_timing(timing)
{
    m_player   = new QMediaPlayer(this);
    m_audioOut = new QAudioOutput(this);
    m_audioOut->setMuted(true);   // routing audio is the AudioCue's job
    m_sink     = new QVideoSink(this);

    m_player->setAudioOutput(m_audioOut);
    m_player->setVideoSink(m_sink);
    m_player->setSource(QUrl::fromLocalFile(m_path));
    // A trimmed loop wraps by hand at the Out point (followPosition); only
    // a whole-file loop can use the player's gapless looping.
    m_nativeLoop = m_timing.loop && !m_timing.isTrimmed();
    if (m_nativeLoop) m_player->setLoops(QMediaPlayer::Infinite);
    if (m_timing.inSeconds > 0.0) {
        m_player->setPosition(qint64(m_timing.inSeconds * 1000.0));
        // Some backends drop a seek made before the media has loaded;
        // onMediaStatus repeats it once it has.
        m_startSeekPending = true;
    }
    m_envelope = m_timing.envelope(m_timing.inSeconds, 0.0, true);

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
                if (m_player) followPosition(m_player->position());
                emit frameAvailable();
            });
    connect(m_player, &QMediaPlayer::positionChanged,
            this, [this](qint64 pos) { followPosition(pos); });
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

qint64 VideoLayer::positionMs() const { return m_player ? m_player->position() : 0; }
qint64 VideoLayer::durationMs() const { return m_player ? m_player->duration() : 0; }
void   VideoLayer::seekMs(qint64 ms)  { if (m_player) m_player->setPosition(ms); }
void   VideoLayer::pause()            { if (m_player) m_player->pause(); }
void   VideoLayer::resume()           { if (m_player) m_player->play(); }

bool VideoLayer::isPaused() const
{
    return m_player && m_player->playbackState() == QMediaPlayer::PausedState;
}

void VideoLayer::onMediaStatus(int status)
{
    if (m_startSeekPending && m_player
        && (status == QMediaPlayer::LoadedMedia || status == QMediaPlayer::BufferedMedia)) {
        m_startSeekPending = false;
        const qint64 inMs = qint64(m_timing.inSeconds * 1000.0);
        if (m_player->position() < inMs - 100) m_player->setPosition(inMs);
    }
    if (status != QMediaPlayer::EndOfMedia || m_nativeLoop) return;
    if (m_timing.loop) { wrapToIn(); return; }
    if (!m_ended) { m_ended = true; emit finished(); }
}

void VideoLayer::wrapToIn()
{
    if (!m_player) return;
    m_firstPass = false;
    m_wrapping  = true;
    m_lastPosMs = qint64(m_timing.inSeconds * 1000.0);
    m_player->setPosition(m_lastPosMs);
    if (m_player->playbackState() != QMediaPlayer::PlayingState) m_player->play();
}

void VideoLayer::followPosition(qint64 posMs)
{
    if (!m_player || m_ended) return;
    const double dur = m_player->duration() / 1000.0;
    // A native loop jumped back to the top: that's the second pass.
    if (m_nativeLoop && posMs + 500 < m_lastPosMs) m_firstPass = false;
    m_lastPosMs = posMs;

    const qint64 outMs = qint64(m_timing.outSeconds * 1000.0);
    // Positions reported before the seek back to In lands aren't a new lap.
    if (m_wrapping) {
        if (outMs > 0 && posMs >= outMs) return;
        m_wrapping = false;
    }
    if (outMs > 0 && posMs >= outMs) {
        if (m_timing.loop) {
            wrapToIn();
        } else {
            // Out point: hold the last frame, then go — like the file ending.
            m_player->pause();
            m_ended = true;
            emit finished();
            return;
        }
    }
    m_envelope = m_timing.envelope(m_lastPosMs / 1000.0, dur, m_firstPass);
}

} // namespace quewi::video
