#pragma once

#include <QMainWindow>
#include <QPointer>
#include <QTimer>

class QAction;
class QAudioOutput;
class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QMediaPlayer;
class QPushButton;
class QUndoStack;
class QVideoFrame;
class QVideoSink;

namespace quewi::audio { class AudioCue; }
namespace quewi::video { class VideoCue; }

namespace quewi::ui {

class VideoMonitor;
class VideoThumbnailer;
class VideoTimeline;

// The video editor: one video cue's picture and soundtrack on a timeline.
//
//  • Monitor — the frame at the playhead, as the cue will show it (its
//    opacity and picture fades applied).
//  • Timeline — a strip of frame thumbnails over the soundtrack's waveform,
//    with the In / Out points (one trim for picture and sound) and fade
//    handles for each. Click or drag anywhere else to move the playhead.
//  • Transport — Space plays / pauses, I and O set the In / Out at the
//    playhead, Home / End jump to them, ← / → step a frame (Shift: a second).
//  • Fields under the timeline for exact values, the cue's level and loop,
//    and the way into the audio editor for the soundtrack's effects.
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
    VideoTimeline *timeline() const { return m_timeline; }

    double playheadSeconds() const { return m_playhead; }
    double durationSeconds() const { return m_duration; }
    bool   isPlaying() const;

public slots:
    void togglePlay();
    void seekTo(double seconds);
    void setInAtPlayhead();
    void setOutAtPlayhead();
    void clearTrims();
    void stepFrames(int frames);

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
    void refreshFromCue();
    void updateHeader();
    void updateTimeReadout();
    void updatePlayButton();
    void onTick();
    void onFrame(const QVideoFrame &frame);
    void applyPreviewLevel();
    double endSeconds() const;
    // One undoable cue field edit (merged with the previous edit of the
    // same field, so a drag is one undo step).
    void edit(const QString &field, const QVariant &value);

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
    QString m_resolution;

    VideoMonitor     *m_monitor  = nullptr;
    VideoTimeline    *m_timeline = nullptr;
    VideoThumbnailer *m_thumbs   = nullptr;

    QLabel *m_headerNumber = nullptr;
    QLabel *m_headerName   = nullptr;
    QLabel *m_headerMeta   = nullptr;
    QLabel *m_timeLabel    = nullptr;
    QLabel *m_lengthLabel  = nullptr;
    QAction *m_playAct     = nullptr;
    QAction *m_loopAct     = nullptr;
    QAction *m_hearAct     = nullptr;

    QDoubleSpinBox *m_inSpin = nullptr, *m_outSpin = nullptr;
    QDoubleSpinBox *m_picFadeInSpin = nullptr, *m_picFadeOutSpin = nullptr;
    QDoubleSpinBox *m_opacitySpin = nullptr;
    QDoubleSpinBox *m_sndFadeInSpin = nullptr, *m_sndFadeOutSpin = nullptr;
    QDoubleSpinBox *m_levelSpin = nullptr;
    QCheckBox *m_loopCheck = nullptr, *m_soundCheck = nullptr;
    QPushButton *m_editSoundBtn = nullptr, *m_lightingBtn = nullptr;
};

} // namespace quewi::ui
