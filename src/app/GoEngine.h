#pragma once

#include "audio/Vbap.h"
#include "core/Workspace.h"

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QSet>

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
    void cancelAll(double fadeOutSeconds = 0.05);

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
    //   - Group: NOT emitted (would require child tracking; v1.1+)
    void cueFinished(quewi::cues::Cue *cue);
    void statusMessage(const QString &msg);
    void gotoRequested(quewi::core::CueId targetId);

private:
    void doFire(cues::Cue *cue, const QByteArray &outputDeviceOverride = {});
    void scheduleContinue(cues::Cue *cue, double delaySeconds);
    cues::Cue *nextCueAfter(cues::Cue *cue) const;
    cues::Cue *findCue(core::CueId id) const;

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
