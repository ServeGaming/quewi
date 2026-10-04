#pragma once

#include "video/Layer.h"
#include "video/PictureTiming.h"

#include <QImage>
#include <QPointer>
#include <QString>

class QMediaPlayer;
class QAudioOutput;
class QVideoSink;

namespace quewi::video {

// QMediaPlayer + QVideoSink decode pipeline. Frames arrive on
// videoFrameChanged; we keep the latest one as a QImage and emit
// frameAvailable so the CompositorWindow repaints.
//
// QAudioOutput is wired but defaults to muted — video cues that need
// audio go through the AudioEngine for sample-accurate playback.
// Letting QMediaPlayer speak its own audio is too unreliable for
// theatre use; the video stream stays in lockstep with the OS clock.
class VideoLayer : public Layer {
    Q_OBJECT
public:
    // Starts at timing.inSeconds, stops (or loops back) at the Out point and
    // fades its envelope per timing. An untrimmed loop uses the player's own
    // seamless looping.
    explicit VideoLayer(const QString &filePath, const PictureTiming &timing,
                        QObject *parent = nullptr);
    ~VideoLayer() override;

    QImage currentFrame() const override { return m_frame; }
    void   teardown() override;

    // Transport — thin forwards onto the backing QMediaPlayer so a UI
    // scrubber can drive a playing video cue. All null-guarded (m_player
    // is a QPointer that goes null on teardown).
    qint64 positionMs() const;
    qint64 durationMs() const;
    void   seekMs(qint64 ms);
    void   pause();
    void   resume();
    bool   isPaused()  const;
    bool   isLooping() const { return m_timing.loop; }
    const PictureTiming &timing() const { return m_timing; }

private slots:
    void onMediaStatus(int status);

private:
    // Out point / wrap handling and the fade envelope for position posMs.
    void followPosition(qint64 posMs);
    void wrapToIn();

    QString m_path;
    PictureTiming m_timing;
    bool    m_nativeLoop = false;    // QMediaPlayer::Infinite (untrimmed loop)
    bool    m_firstPass  = true;     // the fade in only runs once
    bool    m_ended      = false;    // finished() sent (non-looping Out)
    bool    m_startSeekPending = false;
    bool    m_wrapping   = false;    // seek back to In not landed yet
    qint64  m_lastPosMs  = 0;
    QPointer<QMediaPlayer>  m_player;
    QPointer<QAudioOutput>  m_audioOut;
    QPointer<QVideoSink>    m_sink;
    QImage  m_frame;
};

} // namespace quewi::video
