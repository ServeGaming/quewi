#pragma once

#include "video/VideoEngine.h"

#include <QPointer>
#include <QWidget>
#include <memory>

class QMediaPlayer;
class QAudioOutput;
class QVideoWidget;
class QLabel;

namespace quewi::video {

// One output window. Frameless, top-level, sized to a normalised rect
// on a chosen screen. Plays/displays a single visual cue.
class VideoOutputWindow : public QWidget {
    Q_OBJECT
public:
    explicit VideoOutputWindow(const VideoVoiceParams &params, QWidget *parent = nullptr);
    ~VideoOutputWindow() override;

signals:
    void finished();

private slots:
    void onMediaStatusChanged(int status);

private:
    void layoutOnScreen(int screenIndex, const QRectF &normalisedGeometry);

    VideoVoiceParams              m_params;
    QPointer<QMediaPlayer>        m_player;
    QPointer<QAudioOutput>        m_audioOut;
    QPointer<QVideoWidget>        m_videoWidget;
    QPointer<QLabel>              m_imageLabel;
    QPointer<QLabel>              m_textLabel;
};

} // namespace quewi::video
