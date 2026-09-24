#pragma once

#include "audio/Vbap.h"
#include "core/Workspace.h"

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <functional>

class QTimer;

namespace quewi::audio { class AudioCue; }

namespace quewi::core   { class Workspace; }
namespace quewi::cues   { class Cue; }
namespace quewi::audio  { class AudioEngine; }
namespace quewi::lighting { class LightingEngine; }
namespace quewi::video  { class VideoEngine; }
namespace quewi::osc    { class OscEngine; }
namespace quewi::midi   { class MidiEngine; }

namespace quewi {

// Centralised cue scheduler. MainWindow asks the engine to fire a cue;
// the engine handles pre-wait, dispatches to the right output engine,
// and chains to the next cue based on continueMode + post-wait. Replaces
// the inline dispatch that used to live in MainWindow::onGoRequested.
//
// All timing is wall-clock via QTimer — sample-accurate scheduling and
// audio-clock follow are Phase-7 polish.
class GoEngine : public QObject {
    Q_OBJECT
public:
    explicit GoEngine(QObject *parent = nullptr);
    ~GoEngine() override;

    void setWorkspace(core::Workspace *ws);
    void setAudioEngine(audio::AudioEngine *e);
    void setLightingEngine(lighting::LightingEngine *e);
    void setVideoEngine(video::VideoEngine *e);
    void setOscEngine(osc::OscEngine *e);
    void setMidiEngine(midi::MidiEngine *e);

    // outputDeviceOverride: when non-empty, audio cues fired through this
    // call route to that output device instead of their own — used by the
    // soundboard to send its whole board to a chosen device (e.g. a virtual
    // cable) without mutating the shared cue. Empty = use the cue's device.
    void fire(cues::Cue *cue, const QByteArray &outputDeviceOverride = {});

    // PANIC: cancel everything scheduled and stop every output now — audio
    // (short fade), video, and the lighting rig to black.
    void cancelAll(double fadeOutSeconds = 0.05);
    // Cancel pre-waits, continues, follows and group waits only. Output is
    // left exactly as it is.
    void cancelScheduling();
    // FADE ALL: cancel scheduling, then fade audio, video AND lights out over
    // `seconds` (it used to snap the lights to black instantly).
    void fadeAll(double seconds);
    // PAUSE: freeze every playing audio/video voice and every pending timer
    // (pre-waits, continues, wait durations) where it is; resumeAll() picks
    // them all up again. Lights are left as they are. (Pause used to be a
    // disguised panic: it stopped the audio for good and blacked out the rig.)
    void pauseAll();
    void resumeAll();
    bool isPaused() const { return m_paused; }

    // Where the playhead belongs after GO fired `fired`: past the whole
    // auto-continue / auto-follow chain (those cues fire on their own) and
    // past every fired group's children (they fire with their group).
    // nullptr = past the end of the list.
    cues::Cue *standbyAfter(cues::Cue *fired) const;

    // Set of audio voice ids currently alive. The soundboard polls this to
    // light pads whose cue is playing (a cue is playing when its
    // currentVoiceId() is in this set). Cheap; safe to call at a few Hz.
    QSet<quint64> activeAudioVoiceIds() const;

signals:
    void cueFired(quewi::cues::Cue *cue);
    // Emitted when a cue's primary effect has completed:
    //   - LightFade / Fade / Wait: after their declared duration
    //   - Memo / OSC / MIDI / MSC / Start / Stop / Goto / Pause /
    //     Load / Reset / Devamp / Light (static): immediately after
    //     fire (these have no runtime; the wire trip IS the cue)
    //   - Audio / Video: NOT emitted here; AudioEngine::voiceFinished
    //     and VideoEngine::voiceFinished are the authoritative
    //     signals — MainWindow maps those to cueFinished separately
    //   - Group: once every child it fired has finished (so an auto-follow
    //     group continues after its contents, like QLab)
    void cueFinished(quewi::cues::Cue *cue);
    void statusMessage(const QString &msg);
    void gotoRequested(quewi::core::CueId targetId);
    void pausedChanged(bool paused);

private:
    void runFire(cues::Cue *cue, const QByteArray &outputDeviceOverride);
    void doFire(cues::Cue *cue, const QByteArray &outputDeviceOverride = {});
    void scheduleContinue(cues::Cue *cue, double delaySeconds);
    cues::Cue *nextCueAfter(cues::Cue *cue) const;
    // Resolve a target id: the firing cue's OWN list first (a Fade in list A
    // must find its target while list B's tab is on screen), then every list.
    cues::Cue *findCue(core::CueId id, const cues::Cue *context = nullptr) const;
    // First ARMED cue after `cue` in its own list, skipping ids in `skip`.
    cues::Cue *nextArmedAfter(const cues::Cue *cue, const QSet<core::CueId> &skip) const;
    // Every cue nested under `cue` if it's a group (children, grandchildren…).
    QSet<core::CueId> descendantsOf(const cues::Cue *cue) const;
    // Stop cue behaviour on any target: audio, video, or a whole group.
    void stopTarget(cues::Cue *target, int depth = 0);
    // A child of a running group finished; finishes the group when it's the last.
    void noteChildFinished(cues::Cue *child);
    void clearPauseState();
    // Every delayed action goes through here so Panic cancels it and Pause
    // freezes it. (Some used to be untracked QTimer::singleShots: a Wait's
    // "finished" timer survived a panic and could release a later follow early.)
    void after(int ms, std::function<void()> fn);

    // Auto-follow: a cue is added to m_followPending when it fires in
    // AutoFollow mode, and its continue is triggered only when its action
    // actually finishes — cueFinished (instant/duration cues) or a voice's
    // NATURAL end (audio/video). cancelAll() empties the set so a panic or a
    // manual stop never advances the list. tryFollow() fires the deferred
    // continue iff the cue is still pending (i.e. not cancelled).
    void tryFollow(cues::Cue *cue);
    void onCueFinishedFollow(cues::Cue *cue);
    void onAudioVoiceFinishedNatural(quint64 voiceId);
    void onVideoVoiceFinishedNatural(quint64 voiceId);
    QSet<cues::Cue *> m_followPending;

    // Raw pointers — MainWindow owns both us and the engines and outlives us.
    core::Workspace              *m_workspace = nullptr;
    audio::AudioEngine           *m_audio = nullptr;
    lighting::LightingEngine     *m_lighting = nullptr;
    video::VideoEngine           *m_video = nullptr;
    osc::OscEngine               *m_osc = nullptr;
    midi::MidiEngine             *m_midi = nullptr;

    QList<QTimer *> m_pending;

    // Running groups → the children they fired that haven't finished yet.
    // Keyed by id (not pointer) so a group deleted mid-run can't dangle.
    QHash<core::CueId, QSet<core::CueId>> m_groupRemaining;

    // Pause state: which voices WE paused (so resume doesn't wake ones a
    // Pause cue paused on purpose) and each frozen timer's remaining ms.
    bool                 m_paused = false;
    QList<quint64>       m_pausedAudio;
    QList<quint64>       m_pausedVideo;
    QHash<QTimer *, int> m_frozenTimers;

    // Nesting depth of synchronous fires (Start / Group children / links).
    // Past a limit, fires bounce through the event loop instead of
    // recursing, so a self-starting loop can't overflow the stack.
    int m_fireDepth = 0;

    // Object-audio trajectory ticker. While at least one playing audio
    // cue has a non-trivial trajectory, a 30 Hz timer recomputes VBAP
    // gains from the cue's current playback position and pushes them
    // into the AudioEngine.
    struct TrajectoryEntry {
        QPointer<audio::AudioCue> cue;
        QList<audio::Speaker>     speakers;
        int                       outChannels = 0;
    };
    QHash<quint64, TrajectoryEntry> m_trajectories;   // keyed by VoiceId
    QTimer                          *m_trajectoryTimer = nullptr;
    void onTrajectoryTick();
};

} // namespace quewi
