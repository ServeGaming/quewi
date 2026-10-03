#pragma once

#include "audio/AudioEditorModel.h"
#include "audio/BeatGrid.h"
#include "audio/LightTrigger.h"

#include <QHash>
#include <QImage>
#include <QPropertyAnimation>
#include <QPointer>
#include <QScrollBar>
#include <QSet>
#include <QWidget>
#include <optional>

namespace quewi::ui {

// The main editing surface of the audio editor.
// Layout (left to right, top to bottom):
//
//  [ruler: time ticks]
//  [lighting lane: trigger points / ranges]
//  [track header | waveform regions ...]   ← per track
//  [track header | waveform regions ...]
//  ...
//
// Horizontal axis = time (samples → pixels via m_framesPerPixel).
// Vertical axis   = stacked tracks at m_trackHeight each.
// Scrolling is handled by external QScrollBars passed in via setScrollBars().
//
// Edit modes:
//   Select  — click selects a region; drag moves it; hover near edge shows Trim cursor.
//   Razor   — click splits region at cursor; no drag.
class TimelineCanvas : public QWidget {
    Q_OBJECT
public:
    enum class Tool { Select, Razor };
    // How region bodies render their audio. Spectrogram draws a log-frequency
    // heat-map of the whole source file (Audacity-style); built lazily off
    // the GUI thread and cached per source file.
    enum class ViewMode { Waveform, Spectrogram };

    explicit TimelineCanvas(audio::AudioEditorModel *model, QWidget *parent = nullptr);
    ~TimelineCanvas() override;

    void setScrollBars(QScrollBar *hbar, QScrollBar *vbar);
    void setTool(Tool t) { m_tool = t; update(); }

    ViewMode viewMode() const { return m_viewMode; }
    void     setViewMode(ViewMode m);

    // Zoom: framesPerPixel — lower = more zoomed in
    double framesPerPixel() const { return m_framesPerPixel; }
    void   setFramesPerPixel(double fpp);

    // Playback cursor position in frames (set by transport while playing)
    void setPlayheadFrame(qint64 f);

    // Edit cursor — where a click lands and where preview playback starts
    // from (Audacity-style). Drawn as a marker with a handle in the ruler.
    qint64 editCursorFrame() const { return m_editCursorFrame; }
    void   setEditCursorFrame(qint64 f);

    static constexpr int kHeaderWidth = 120;
    static constexpr int kRulerHeight = 24;
    // Lighting-trigger lane between the ruler and the first track. Like the
    // ruler it doesn't scroll vertically; the tracks start below it.
    static constexpr int kMarkerLaneHeight = 20;

    // Lighting triggers shown in the marker lane. Times are seconds; the lane
    // maps them to frames with sampleRate. The canvas never edits them — it
    // reports what the user did through the trigger* signals below.
    void setTriggers(const audio::LightTriggers &t, double sampleRate);
    void setSelectedTrigger(const QUuid &id);
    // Brief highlight of a trigger that just fired.
    void flashTrigger(const QUuid &id);

    // The song's beat grid: beat / bar lines in the lighting lane, faint bar
    // lines through the tracks. With snap on, lane clicks, drags, moves and
    // resizes land on the nearest beat; holding Alt turns that off for the
    // gesture.
    void setBeatGrid(const audio::BeatGrid &g);
    const audio::BeatGrid &beatGrid() const { return m_grid; }
    void setSnapToBeats(bool on) { m_snap = on; }
    bool snapToBeats() const { return m_snap; }

signals:
    void regionSelected(QUuid regionId);
    void trackSelected(int trackIndex);
    void requestAddTrack();
    void requestRemoveTrack(int trackIndex);
    // Emitted when the user clicks to reposition the edit cursor.
    void editCursorMoved(qint64 frame);
    // Marker lane. end < 0 = a point. Moves/resizes are reported on release
    // (drawn live while dragging).
    void triggerAdded(double start, double end);
    void triggerMoved(QUuid id, double start, double end);
    void triggerSelected(QUuid id);
    // "rename", "toggleRange", "toggleEnabled", "delete".
    void triggerContextAction(QUuid id, QString action);
protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;
    void contextMenuEvent(QContextMenuEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void resizeEvent(QResizeEvent *) override;

private:
    // ── Geometry helpers ──────────────────────────────────────────────────────
    int   trackY(int trackIndex) const;      // top pixel of track (in canvas coords)
    int   trackHeight()          const { return m_trackHeight; }
    int   timelineLeft()         const { return kHeaderWidth; }
    int   rulerBottom()          const { return kRulerHeight; }
    int   tracksTop()            const { return kRulerHeight + kMarkerLaneHeight; }
    bool  inTriggerLane(int y)   const { return y >= kRulerHeight && y < tracksTop(); }
    // Canvas height required to show all tracks
    int   contentHeight()        const;

    // Convert between timeline frames and canvas x-pixels
    double framesToX(qint64 frames) const;
    qint64 xToFrames(int x) const;

    // Hit-test: which track is at canvas y (excluding ruler)?
    int trackAtY(int y) const;

    // Find region under canvas position (x, y). Returns nullopt if none.
    struct Hit {
        int trackIndex;
        int regionIndex;
        enum { Body, LeftEdge, RightEdge } part;
    };
    std::optional<Hit> hitTest(int x, int y) const;
    static constexpr int kEdgeTolerance = 6; // pixels to trigger trim cursor

    // ── Drawing ───────────────────────────────────────────────────────────────
    void drawRuler(QPainter &p);
    void drawEditCursor(QPainter &p);
    void drawTrackHeader(QPainter &p, int trackIndex, const QRect &r);
    void drawRegion(QPainter &p, const audio::AudioRegion &region,
                    int trackIndex, const QRect &trackRect);
    void drawRegionWaveform(QPainter &p, const audio::AudioRegion &region,
                            int x1, int x2, int waveTop, int waveH);
    // Draws the cached spectrogram for the region's visible span. Returns
    // false if the image isn't built yet (caller falls back to waveform).
    bool drawRegionSpectrogram(QPainter &p, const audio::AudioRegion &region,
                               int x1, int x2, int top, int h);
    void drawPlayhead(QPainter &p);
    void drawTriggerGuides(QPainter &p);   // faint lines down through the tracks
    void drawTriggerLane(QPainter &p);
    // Beat / bar lines. lane = the lighting lane (beats + bars + numbers);
    // otherwise faint bar lines only, for the tracks.
    void drawBeatLines(QPainter &p, int top, int h, bool lane);

    // ── Lighting triggers ─────────────────────────────────────────────────
    double secondsToX(double s) const;
    double xToSeconds(int x) const;        // clamped >= 0
    // s on the nearest beat when snapping is on and the grid is set.
    double snapSeconds(double s, bool noSnap) const;
    audio::BeatGrid m_grid;
    bool   m_snap = false;
    enum class TriggerPart { Body, StartEdge, EndEdge };
    // Index into m_triggers of the trigger under x in the lane, or -1.
    int    triggerAt(int x, TriggerPart *part = nullptr) const;
    // Start/end as drawn: the live drag values for the one being dragged.
    void   shownSpan(const audio::LightTrigger &t, double &start, double &end) const;
    audio::LightTriggers m_triggers;
    double m_triggerRate = 48000.0;
    QUuid  m_selectedTrigger;
    QUuid  m_flashTrigger;
    struct TriggerDrag {
        enum Mode { None, Create, Move, ResizeStart, ResizeEnd } mode = None;
        QUuid  id;
        bool   moved = false;
        QPoint pressPos;
        double pressSec = 0.0;
        double pressSnapped = 0.0;   // pressSec on the grid (a click's point)
        bool   noSnap = false;       // Alt held at the press
        double origStart = 0.0, origEnd = -1.0;
        double curStart = 0.0, curEnd = -1.0;
    } m_tdrag;
    void updateScrollBars();

    // ── Spectrogram cache (Spectrogram view mode) ─────────────────────────
    // One heat-map image per distinct source file, built once on a worker
    // thread. Keyed by raw AudioFile* (stable for the editor's lifetime); the
    // frame-count guard rebuilds if a file is reloaded at the same address.
    void ensureSpectrogram(const std::shared_ptr<audio::AudioFile> &file);
    void clearSpectrogramCache();
    ViewMode m_viewMode = ViewMode::Waveform;
    QHash<const audio::AudioFile *, QImage>  m_specImages;
    QHash<const audio::AudioFile *, qint64>  m_specFrames;   // frameCount when built
    QSet<const audio::AudioFile *>           m_specBuilding; // in-flight builds

    // ── State ─────────────────────────────────────────────────────────────────
    audio::AudioEditorModel *m_model;
    Tool   m_tool    = Tool::Select;
    double m_framesPerPixel = 100.0;
    int    m_trackHeight    = 80;
    int    m_scrollX        = 0;  // horizontal scroll offset (pixels)
    int    m_scrollY        = 0;  // vertical scroll offset (pixels)
    qint64 m_playheadFrame  = -1; // <0 = hidden (only shown while playing)
    qint64 m_editCursorFrame = 0; // where a click landed / playback starts

    QScrollBar *m_hbar = nullptr;
    QScrollBar *m_vbar = nullptr;
    QPointer<QPropertyAnimation> m_scrollAnim;

    // Drag state
    struct DragState {
        bool      active     = false;
        bool      moved      = false;  // crossed the move threshold yet?
        QUuid     regionId;
        int       trackIndex = -1;
        bool      isTrim     = false;
        bool      trimLeft   = false;
        qint64    dragStartFrame = 0;
        qint64    regionStartPos = 0;  // original timelinePosSamples
        qint64    regionSrcIn    = 0;
        qint64    regionSrcOut   = 0;
        QPoint    mouseStart;
    } m_drag;
    // A region body must be dragged at least this many pixels before it
    // actually moves — so a plain click just sets the cursor / selects
    // instead of nudging the clip (Audacity-like).
    static constexpr int kDragThreshold = 4;

    QUuid m_selectedRegion;
};

} // namespace quewi::ui
