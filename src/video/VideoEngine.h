#pragma once

#include <QColor>
#include <QObject>
#include <QPointer>
#include <QRectF>
#include <QString>
#include <vector>

class QScreen;

namespace quewi::video {

using VideoVoiceId = quint64;

// Description of one visual cue's runtime state — used by the engine
// to set up the output window. The cue itself is the source of truth;
// this struct is the snapshot the engine consumes at fire time.
struct VideoVoiceParams {
    enum Kind { Video, Image, Text };
    Kind    kind = Video;
    QString filePath;          // Video / Image
    QString text;              // Text
    int     fontPixelSize = 96;
    QColor  textColor = Qt::white;
    int     screenIndex = 0;   // 0 = primary
    QRectF  geometry = {0.0, 0.0, 1.0, 1.0}; // normalised to chosen screen
    double  opacity = 1.0;
    bool    loop = false;      // Video only
};

class VideoOutputWindow;

// Owns a pool of frameless output windows — one per active visual cue.
// A real compositing pipeline (multiple cues sharing a surface, blending,
// effects) lands when the GoEngine arrives in Phase 6; for v1 each cue
// gets its own borderless top-level window on the chosen screen.
class VideoEngine : public QObject {
    Q_OBJECT
public:
    explicit VideoEngine(QObject *parent = nullptr);
    ~VideoEngine() override;

    VideoVoiceId fire(const VideoVoiceParams &params);

    void stop(VideoVoiceId id);
    void stopAll();

    int activeVoiceCount() const { return static_cast<int>(m_voices.size()); }

signals:
    void voiceFinished(quewi::video::VideoVoiceId id);

private:
    struct Voice {
        VideoVoiceId        id = 0;
        VideoOutputWindow  *window = nullptr;
    };
    void onWindowFinished(VideoOutputWindow *window);

    std::vector<Voice> m_voices;
    VideoVoiceId       m_nextId = 0;
};

} // namespace quewi::video

Q_DECLARE_METATYPE(quewi::video::VideoVoiceId)
