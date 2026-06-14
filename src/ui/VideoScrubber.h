#pragma once

#include <QWidget>

class QToolButton;
class QLabel;

namespace quewi::ui {

class ScrubTrack;

// A QLab-style transport scrubber for a live video cue: a play/pause
// button, a draggable progress track with a handle, and an
// elapsed / total time label.
//
// It is a pure view — it emits intent (seekRequested / playPauseRequested)
// and is driven from the outside via setPositionMs/setDurationMs/
// setPlaying/setActive. The owner (Inspector) polls the VideoEngine for
// the selected cue's live voice and pushes the state in.
class VideoScrubber : public QWidget {
    Q_OBJECT
public:
    explicit VideoScrubber(QWidget *parent = nullptr);

    void setDurationMs(qint64 ms);
    void setPositionMs(qint64 ms);     // ignored while the user is dragging
    void setPlaying(bool playing);     // toggles the play/pause glyph
    void setActive(bool hasLiveVoice); // greys the whole control when false

signals:
    void seekRequested(qint64 ms);
    void playPauseRequested();

private:
    void refreshTime();

    QToolButton *m_play  = nullptr;
    ScrubTrack  *m_track = nullptr;
    QLabel      *m_time  = nullptr;

    qint64 m_durMs   = 0;
    qint64 m_posMs   = 0;
    bool   m_playing = false;
    bool   m_active  = false;
};

} // namespace quewi::ui
