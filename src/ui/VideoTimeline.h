#pragma once

#include "audio/Cuts.h"

#include <QImage>
#include <QList>
#include <QWidget>
#include <map>
#include <memory>

namespace quewi::audio { class AudioFile; }

namespace quewi::ui {

// The video editor's timeline, laid out like an NLE's: track headers on
// the left (V1 picture, A1 sound — the sound's mute is the cue's "Play
// sound"), a time ruler, a V1 lane of frame thumbnails with the picture's
// fade ramps and an A1 lane of the soundtrack's waveform with its own, over
// the whole file.
//
// The In / Out bars span both lanes — one trim for picture and sound. Fade
// handles sit on the top edge of each lane at the end of its ramp. Cut
// sections (spliced out) are drawn hatched in both lanes; the ruler keeps
// showing source time, so what was removed stays visible.
//
// Tools: Select — click to seek (and, between split points, to select that
// segment), drag in a lane to select a range; drag in the ruler to scrub.
// Razor — click to drop a split point. The selection and the split points
// are the operator's working marks: the window turns them into cuts.
//
// Pure view: it shows what it's told (set*) and emits what the operator
// asks for, live while dragging, then editingFinished on release. Times are
// seconds of the file; Out 0 = the end of the file, as on the cue.
class VideoTimeline : public QWidget {
    Q_OBJECT
public:
    enum class Tool { Select, Razor };

    explicit VideoTimeline(QWidget *parent = nullptr);
    ~VideoTimeline() override;

    void setDuration(double seconds);
    double duration() const { return m_duration; }
    // Snaps selections and split points to frames (0 = don't).
    void setFrameRate(double fps);
    void setTrims(double inSeconds, double outSeconds);
    void setPictureFades(double inSeconds, double outSeconds);
    void setSoundFades(double inSeconds, double outSeconds);
    void setSoundEnabled(bool on);
    void setAudioFile(std::shared_ptr<audio::AudioFile> file);
    void setThumbnail(double atSeconds, const QImage &image);
    void clearThumbnails();
    // Status text for the picture lane while it has no thumbnails.
    void setPictureNote(const QString &note);
    void setPlayhead(double seconds);      // < 0 hides it
    void setTriggerMarks(const QList<double> &seconds);
    void setCuts(const audio::Cuts &cuts);
    const audio::Cuts &cuts() const { return m_cuts; }

    // ── Tools, selection, split points ─────────────────────────────────
    void setTool(Tool tool);
    Tool tool() const { return m_tool; }

    // A selected range of the file, or none (hasSelection false).
    void setSelection(double start, double end);
    void clearSelection();
    bool hasSelection() const { return m_selEnd > m_selStart; }
    double selectionStart() const { return m_selStart; }
    double selectionEnd() const { return m_selEnd; }

    // Razor marks: boundaries the Select tool picks segments between.
    void addSplitPoint(double seconds);
    void removeSplitPoint(double seconds);
    void clearSplitPoints();
    const QList<double> &splitPoints() const { return m_splits; }
    // The segment around `seconds` bounded by split points, cut edges and
    // the In / Out bars (nothing split or cut: an empty range).
    std::pair<double, double> segmentAt(double seconds) const;

    void zoomIn();
    void zoomOut();
    void zoomFit();

    // Where the Out bar actually sits (Out, or the end of the file).
    double endSeconds() const;

    // Layout, for tests: the x of a file time, and the lanes.
    int    xForSeconds(double s) const;
    QRect  rulerRect() const;
    QRect  pictureLane() const;
    QRect  soundLane() const;
    QRect  muteButtonRect() const;

    QSize sizeHint() const override { return {800, 200}; }
    QSize minimumSizeHint() const override { return {320, 150}; }

signals:
    void trimInChanged(double seconds);
    void trimOutChanged(double seconds);      // 0 = end of file
    void pictureFadeInChanged(double seconds);
    void pictureFadeOutChanged(double seconds);
    void soundFadeInChanged(double seconds);
    void soundFadeOutChanged(double seconds);
    void seekRequested(double seconds);
    void editingFinished();
    void selectionChanged();                  // hasSelection() / selection*()
    void splitPointsChanged();
    void toolChanged(VideoTimeline::Tool tool);
    // The A1 header's mute: the operator wants the sound on / off.
    void soundMuteToggled(bool muted);
    // Right-click at a file time (over a cut, a selection, or nothing).
    void contextMenuRequested(double seconds, const QPoint &globalPos);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void leaveEvent(QEvent *) override;
    void resizeEvent(QResizeEvent *) override;

private:
    enum class Handle { None, TrimIn, TrimOut, PicFadeIn, PicFadeOut, SndFadeIn, SndFadeOut };

    Handle hitTest(const QPoint &p) const;
    double secondsForX(double x) const;
    double snapToFrame(double s) const;
    double snapToMarks(double s) const;     // split points, In / Out, cut edges
    QRect  trackArea() const;               // right of the track headers
    QRect  lanesRect() const;               // both lanes, for overlays
    double viewSpan() const { return m_viewEnd - m_viewStart; }
    void   clampView();
    void   applyDrag(double t);
    void   paintHeaders(QPainter &p);
    void   paintRuler(QPainter &p);
    void   paintPicture(QPainter &p);
    void   paintSound(QPainter &p);
    void   paintRamp(QPainter &p, const QRect &lane, double fadeIn, double fadeOut);
    void   paintCuts(QPainter &p);
    void   paintMarks(QPainter &p);
    void   updateCursor(const QPoint &pos);

    double m_duration = 0.0;
    double m_fps = 0.0;
    double m_in = 0.0, m_out = 0.0;
    double m_picFadeIn = 0.0, m_picFadeOut = 0.0;
    double m_sndFadeIn = 0.0, m_sndFadeOut = 0.0;
    bool   m_soundEnabled = true;
    std::shared_ptr<audio::AudioFile> m_file;
    std::map<double, QImage> m_thumbs;       // by time
    QString m_pictureNote;
    double m_playhead = -1.0;
    QList<double> m_triggerMarks;
    audio::Cuts m_cuts;

    Tool   m_tool = Tool::Select;
    double m_selStart = 0.0, m_selEnd = 0.0;   // end <= start: none
    QList<double> m_splits;
    double m_hoverSeconds = -1.0;            // razor ghost

    double m_viewStart = 0.0, m_viewEnd = 0.0; // 0/0 = not fitted yet

    Handle m_drag = Handle::None;
    bool   m_scrubbing = false;              // ruler drag
    bool   m_selecting = false;              // lane drag (Select tool)
    bool   m_pressedInLane = false;
    QPoint m_pressPos;
    bool   m_panning = false;
    int    m_panAnchorX = 0;
    double m_panAnchorStart = 0.0;
    double m_pressSeconds = 0.0;             // Shift = fine drag, from here
    double m_dragInitial = 0.0;
};

} // namespace quewi::ui
