#pragma once

#include "audio/AudioEditorModel.h"

#include <QPropertyAnimation>
#include <QPointer>
#include <QScrollBar>
#include <QWidget>
#include <optional>

namespace quewi::ui {

// The main editing surface of the audio editor.
// Layout (left to right, top to bottom):
//
//  [ruler: time ticks]
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

    explicit TimelineCanvas(audio::AudioEditorModel *model, QWidget *parent = nullptr);
    ~TimelineCanvas() override = default;

    void setScrollBars(QScrollBar *hbar, QScrollBar *vbar);
    void setTool(Tool t) { m_tool = t; update(); }

    // Zoom: framesPerPixel — lower = more zoomed in
    double framesPerPixel() const { return m_framesPerPixel; }
    void   setFramesPerPixel(double fpp);

    // Playback cursor position in frames (set by transport)
    void setPlayheadFrame(qint64 f);

    static constexpr int kHeaderWidth = 120;
    static constexpr int kRulerHeight = 24;

signals:
    void regionSelected(QUuid regionId);
    void trackSelected(int trackIndex);
    void requestAddTrack();
    void requestRemoveTrack(int trackIndex);

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
    void drawTrackHeader(QPainter &p, int trackIndex, const QRect &r);
    void drawRegion(QPainter &p, const audio::AudioRegion &region,
                    int trackIndex, const QRect &trackRect);
    void drawPlayhead(QPainter &p);
    void updateScrollBars();

    // ── State ─────────────────────────────────────────────────────────────────
    audio::AudioEditorModel *m_model;
    Tool   m_tool    = Tool::Select;
    double m_framesPerPixel = 100.0;
    int    m_trackHeight    = 80;
    int    m_scrollX        = 0;  // horizontal scroll offset (pixels)
    int    m_scrollY        = 0;  // vertical scroll offset (pixels)
    qint64 m_playheadFrame  = 0;

    QScrollBar *m_hbar = nullptr;
    QScrollBar *m_vbar = nullptr;
    QPointer<QPropertyAnimation> m_scrollAnim;

    // Drag state
    struct DragState {
        bool      active     = false;
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

    QUuid m_selectedRegion;
};

} // namespace quewi::ui
