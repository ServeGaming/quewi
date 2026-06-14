#pragma once

#include "video/Layer.h"

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
    explicit VideoLayer(const QString &filePath, bool loop, QObject *parent = nullptr);
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
    bool   isLooping() const { return m_loop; }

private slots:
    void onMediaStatus(int status);

private:
    QString m_path;
    bool    m_loop = false;
    QPointer<QMediaPlayer>  m_player;
    QPointer<QAudioOutput>  m_audioOut;
    QPointer<QVideoSink>    m_sink;
    QImage  m_frame;
};

} // namespace quewi::video
