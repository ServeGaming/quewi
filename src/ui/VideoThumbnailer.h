#pragma once

#include <QImage>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <vector>

class QMediaPlayer;
class QVideoFrame;
class QVideoSink;

namespace quewi::ui {

// Grabs a strip of frames from a video file for the video editor's
// timeline: its own muted, paused player seeks to each time in turn and
// takes the frame that arrives (a paused seek delivers one). Thumbnails
// come out one at a time, so the strip fills in while you work.
class VideoThumbnailer : public QObject {
    Q_OBJECT
public:
    explicit VideoThumbnailer(QObject *parent = nullptr);
    ~VideoThumbnailer() override;

    // Starts over on `path`: roughly one frame every `spacingSeconds`
    // (at least 12, at most `maxCount`), scaled to `height` px tall.
    void start(const QString &path, double spacingSeconds = 2.0,
               int maxCount = 180, int height = 144);
    void stop();
    bool isRunning() const { return m_running; }

signals:
    void durationKnown(double seconds);
    void thumbnailReady(double atSeconds, const QImage &image);
    void finished();
    void failed(const QString &why);

private:
    void onFrame(const QVideoFrame &frame);
    void requestNext();
    void begin();

    QPointer<QMediaPlayer> m_player;
    QPointer<QVideoSink>   m_sink;
    QTimer m_timeout;
    std::vector<double> m_times;
    size_t m_index = 0;
    bool   m_running = false;
    bool   m_started = false;   // the player re-reports Loaded after every seek
    bool   m_waiting = false;
    double m_spacing = 2.0;
    int    m_maxCount = 180;
    int    m_height = 144;
};

} // namespace quewi::ui
