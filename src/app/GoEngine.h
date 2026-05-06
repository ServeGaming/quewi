#pragma once

#include "audio/Vbap.h"
#include "core/Workspace.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>

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

    void fire(cues::Cue *cue);
    void cancelAll(double fadeOutSeconds = 0.05);

signals:
    void cueFired(quewi::cues::Cue *cue);
    void statusMessage(const QString &msg);
    void gotoRequested(quewi::core::CueId targetId);

private:
    void doFire(cues::Cue *cue);
    void scheduleContinue(cues::Cue *cue, double delaySeconds);
    cues::Cue *nextCueAfter(cues::Cue *cue) const;
    cues::Cue *findCue(core::CueId id) const;

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
