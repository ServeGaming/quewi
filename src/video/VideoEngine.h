#pragma once

#include <QColor>
#include <QHash>
#include <QObject>
#include <QPolygonF>
#include <QPointer>
#include <QRectF>
#include <QString>
#include <memory>
#include <vector>

class QScreen;

namespace quewi::video {

using VideoVoiceId = quint64;

// Description of one visual cue's runtime state — used by the engine
// to set up the layer. The cue itself is the source of truth; this
// struct is the snapshot the engine consumes at fire time.
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
    int     zOrder = 0;        // higher = on top
};

class Compositor;
class Layer;
class VideoLayer;

// Owns a Compositor (one window per output screen). fire() creates the
// matching Layer subclass and routes it onto the compositor; stop()
// removes the layer. Per-screen corner-pin transforms are held here
// too so the inspector can configure them.
class VideoEngine : public QObject {
    Q_OBJECT
public:
    explicit VideoEngine(QObject *parent = nullptr);
    ~VideoEngine() override;

    VideoVoiceId fire(const VideoVoiceParams &params);

    void stop(VideoVoiceId id);
    void stopAll();

    // Soft stop — animates every active layer's opacity from its
    // current value down to 0 over `durationSeconds`, then stops the
    // layers. Used by /quewi/video/fadeOut and /quewi/fadeAll so
    // a remote can crossfade out without the hard cut stopAll
    // produces. Duration ≤ 0 falls back to instant stopAll.
    void fadeOutAll(double durationSeconds);

    // Fade one playing video voice's opacity to `targetOpacity` (0..1) over
    // durationSeconds. Drives FadeCue with parameter "opacity". No-op if the
    // voice has already ended.
    void fadeOpacity(VideoVoiceId id, double targetOpacity, double durationSeconds);

    int activeVoiceCount() const { return static_cast<int>(m_voices.size()); }

    // ── Live transport for a playing video voice ────────────────────────
    // Lets a UI scrubber drive a fired video cue. A snapshot query (polled
    // ~30 Hz, like the audio ACTIVE strip) plus seek/pause/resume.
    struct VideoTransport {
        bool   valid   = false;  // false if the id is unknown / not a video voice
        qint64 posMs   = 0;
        qint64 durMs   = 0;
        bool   paused  = false;
        bool   looping = false;
    };
    VideoTransport transport(VideoVoiceId id) const;
    void seek(VideoVoiceId id, qint64 ms);
    void pause(VideoVoiceId id);
    void resume(VideoVoiceId id);

    // Per-screen projection mapping. Quad corners are normalised 0..1
    // window space; identity = {(0,0),(1,0),(1,1),(0,1)}.
    void  setCornerPin(int screenIndex, const QPolygonF &quad);
    QPolygonF cornerPin(int screenIndex) const;

    // Show / hide a calibration test pattern on the given screen. The
    // ProjectionMappingDialog calls these so the user has something to
    // align against before any cue has fired. Idempotent.
    void  showTestPattern(int screenIndex);
    void  hideTestPattern(int screenIndex);
    bool  hasTestPattern(int screenIndex) const;

signals:
    void voiceFinished(quewi::video::VideoVoiceId id);
    // Emitted ONLY when the media played to its natural end (onLayerFinished),
    // not on stop()/stopAll()/fade-out. Drives auto-follow in the GoEngine.
    void voiceFinishedNatural(quewi::video::VideoVoiceId id);

private:
    struct Voice {
        VideoVoiceId        id = 0;
        QPointer<Layer>     layer;
    };
    void onLayerFinished(Layer *layer);
    VideoLayer *videoLayerFor(VideoVoiceId id) const;

    std::unique_ptr<Compositor> m_compositor;
    std::vector<Voice> m_voices;
    VideoVoiceId       m_nextId = 0;

    // Per-screen test-pattern layers. The compositor owns the actual
    // QObject; this map is just so we can find them again to remove.
    QHash<int, QPointer<Layer>> m_testPatterns;
};

} // namespace quewi::video

Q_DECLARE_METATYPE(quewi::video::VideoVoiceId)
