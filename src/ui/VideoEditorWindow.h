#pragma once

#include "ui/VideoTimeline.h"

#include <QMainWindow>
#include <QPointer>
#include <QTimer>

class QAction;
class QAudioOutput;
class QLabel;
class QMediaPlayer;
class QUndoStack;
class QVideoFrame;
class QVideoSink;

namespace quewi::audio { class AudioCue; }
namespace quewi::video { class VideoCue; }

namespace quewi::ui {

class VideoInspector;
class VideoMonitor;
class VideoThumbnailer;

// The video editor: one video cue's picture and soundtrack, laid out like
// an NLE.
//
//  • Viewer — the frame at the playhead, as the cue will show it (its
//    opacity and picture fades applied), with the transport under it:
//    to In, a frame back, play / pause, a frame on, to Out, loop, sound,
//    and the timecode (where the playhead is / how long the cue plays for,
//    trims and cuts applied).
//  • Inspector — one column beside the viewer: Clip (In, Out, plays-for,
//    loop), Video (opacity, picture fades), Audio (sound on, level, sound
//    fades, the audio editor), Lighting (the soundtrack's triggers).
//  • Timeline — across the bottom: a ruler, V1 (frame thumbnails) and A1
//    (the waveform), the In / Out bars (one trim for picture and sound),
//    fade handles, light-trigger marks, the playhead, and the cut sections.
//  • Tools — Select (V): click to seek, drag to select a range; Razor (B):
//    click to drop a split point, then click a segment to select it.
//    Delete cuts the selection out (field "cuts"); Keep only this trims
//    In / Out to it; right-click a cut to restore it. The timeline keeps
//    showing source time with the cuts drawn over it, so what was removed
//    stays visible. The preview jumps over cuts like the show will.
//  • Keys — Space play / pause, I / O set In / Out, Shift+I / Shift+O mark
//    the selection, Home / End jump, ← / → step a frame (Shift: a second),
//    Ctrl+K split at the playhead, Delete cuts the selection, Esc clears it.
//
// Every change is a cue field edit on the show's undo stack (Ctrl+Z here or
// in the main window), so the cue plays it straight away.
class VideoEditorWindow : public QMainWindow {
    Q_OBJECT
public:
    // `undo` takes the edits; null = edit the cue directly.
    VideoEditorWindow(video::VideoCue *cue, QUndoStack *undo, QWidget *parent = nullptr);
    ~VideoEditorWindow() override;

    video::VideoCue *cue() const;
    VideoTimeline  *timeline() const { return m_timeline; }
    VideoInspector *inspector() const { return m_inspector; }

    double playheadSeconds() const { return m_playhead; }
    double durationSeconds() const { return m_duration; }
    // How long the cue plays for: In → Out, less the cuts.
    double playsForSeconds() const;
    bool   isPlaying() const;

public slots:
    void togglePlay();
    void seekTo(double seconds);
    void setInAtPlayhead();
    void setOutAtPlayhead();
    void clearTrims();
    void stepFrames(int frames);

    // ── Splicing ──────────────────────────────────────────────────────
    void setTool(VideoTimeline::Tool tool);
    void selectRange(double start, double end);
    void clearSelection();
    void markSelectionStart();          // Shift+I: the selection from the playhead
    void markSelectionEnd();            // Shift+O: …to the playhead
    void splitAtPlayhead();             // Ctrl+K: a split point
    void deleteSelection();             // Delete: the selection becomes a cut
    void keepOnlySelection();           // In / Out around the selection
    void restoreCut(double start, double end);
    void restoreAllCuts();

signals:
    // "Edit sound…": the soundtrack in the audio editor (effects, EQ, …).
    void editSoundRequested(quewi::audio::AudioCue *sound);
    // "Lighting…": the soundtrack's lighting triggers (audio editor's tab).
    void lightingRequested(quewi::audio::AudioCue *sound);

protected:
    void keyPressEvent(QKeyEvent *e) override;
    void closeEvent(QCloseEvent *e) override;

private:
    void buildToolbar();
    void buildCentral();
    QWidget *buildViewer(QWidget *parent);
    void wireInspector();
    void refreshFromCue();
    void updateHeader();
    void updateTimeReadout();
    void updatePlayButton();
    void updateSpliceActions();
    void showTimelineMenu(double seconds, const QPoint &globalPos);
    void onTick();
    void onFrame(const QVideoFrame &frame);
    void applyPreviewLevel();
    double endSeconds() const;
    // One undoable cue field edit (merged with the previous edit of the
    // same field, so a drag is one undo step).
    void edit(const QString &field, const QVariant &value);
    // A cuts edit: its own undo step, never merged with the last one.
    void editCuts(const audio::Cuts &cuts, const QString &what);

    QPointer<video::VideoCue> m_cue;
    QPointer<QUndoStack>      m_undo;
    bool   m_loading = false;      // refreshFromCue: don't echo edits back

    // Preview player (its own; the show's video engine isn't touched).
    QMediaPlayer *m_player = nullptr;
    QVideoSink   *m_sink   = nullptr;
    QAudioOutput *m_audio  = nullptr;
    QTimer        m_tick;
    double m_duration = 0.0;
    double m_playhead = 0.0;
    double m_fps = 25.0;
    double m_pendingSeek = -1.0;   // a seek the player hasn't reported yet
    qint64 m_pendingSince = 0;
    QString m_resolution;

    VideoMonitor     *m_monitor   = nullptr;
    VideoTimeline    *m_timeline  = nullptr;
    VideoInspector   *m_inspector = nullptr;
    VideoThumbnailer *m_thumbs    = nullptr;

    QLabel *m_headerNumber = nullptr;
    QLabel *m_headerName   = nullptr;
    QLabel *m_headerMeta   = nullptr;
    QLabel *m_timeLabel    = nullptr;
    QLabel *m_lengthLabel  = nullptr;
    QAction *m_playAct     = nullptr;
    QAction *m_loopAct     = nullptr;
    QAction *m_hearAct     = nullptr;
    QAction *m_selectAct   = nullptr;
    QAction *m_razorAct    = nullptr;
    QAction *m_deleteAct   = nullptr;
    QAction *m_keepAct     = nullptr;
    QAction *m_restoreAllAct = nullptr;
    QAction *m_clearSplitsAct = nullptr;
};

} // namespace quewi::ui
