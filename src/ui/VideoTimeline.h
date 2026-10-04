#pragma once

#include <QImage>
#include <QList>
#include <QWidget>
#include <map>
#include <memory>

namespace quewi::audio { class AudioFile; }

namespace quewi::ui {

// The video editor's timeline: a ruler, a PICTURE lane (a strip of frame
// thumbnails with the picture's fade ramps) and a SOUND lane (the
// soundtrack's waveform with its fade ramps), over the whole file.
//
// The In / Out bars span both lanes — one trim for picture and sound. Fade
// handles sit on the top edge of each lane at the end of its ramp. Anything
// else you click or drag moves the playhead (seekRequested).
//
// Pure view: it shows what it's told (set*) and emits what the operator
// asks for, live while dragging, then editingFinished on release. Times are
// seconds of the file; Out 0 = the end of the file, as on the cue.
class VideoTimeline : public QWidget {
    Q_OBJECT
public:
    explicit VideoTimeline(QWidget *parent = nullptr);
    ~VideoTimeline() override;

    void setDuration(double seconds);
    double duration() const { return m_duration; }
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

    void zoomIn();
    void zoomOut();
    void zoomFit();

    // Where the Out bar actually sits (Out, or the end of the file).
    double endSeconds() const;

    // Layout, for tests: the x of a file time, and the lanes.
    int    xForSeconds(double s) const;
    QRect  pictureLane() const;
    QRect  soundLane() const;

    QSize sizeHint() const override { return {800, 180}; }
    QSize minimumSizeHint() const override { return {320, 180}; }

signals:
    void trimInChanged(double seconds);
    void trimOutChanged(double seconds);      // 0 = end of file
    void pictureFadeInChanged(double seconds);
    void pictureFadeOutChanged(double seconds);
    void soundFadeInChanged(double seconds);
    void soundFadeOutChanged(double seconds);
    void seekRequested(double seconds);
    void editingFinished();

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void resizeEvent(QResizeEvent *) override;

private:
    enum class Handle { None, TrimIn, TrimOut, PicFadeIn, PicFadeOut, SndFadeIn, SndFadeOut };

    Handle hitTest(const QPoint &p) const;
    double secondsForX(double x) const;
    QRect  trackArea() const;               // right of the lane labels
    QRect  rulerRect() const;
    double viewSpan() const { return m_viewEnd - m_viewStart; }
    void   clampView();
    void   applyDrag(double t);
    void   paintRuler(QPainter &p);
    void   paintPicture(QPainter &p);
    void   paintSound(QPainter &p);
    void   paintRamp(QPainter &p, const QRect &lane, double fadeIn, double fadeOut);
    void   updateCursor(const QPoint &pos);

    double m_duration = 0.0;
    double m_in = 0.0, m_out = 0.0;
    double m_picFadeIn = 0.0, m_picFadeOut = 0.0;
    double m_sndFadeIn = 0.0, m_sndFadeOut = 0.0;
    bool   m_soundEnabled = true;
    std::shared_ptr<audio::AudioFile> m_file;
    std::map<double, QImage> m_thumbs;       // by time
    QString m_pictureNote;
    double m_playhead = -1.0;
    QList<double> m_triggerMarks;

    double m_viewStart = 0.0, m_viewEnd = 0.0; // 0/0 = not fitted yet

    Handle m_drag = Handle::None;
    bool   m_seeking = false;
    bool   m_panning = false;
    int    m_panAnchorX = 0;
    double m_panAnchorStart = 0.0;
    double m_pressSeconds = 0.0;             // Shift = fine drag, from here
    double m_dragInitial = 0.0;
};

} // namespace quewi::ui
