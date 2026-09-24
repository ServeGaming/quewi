#include "GoEngine.h"

#include "audio/AudioCue.h"
#include "audio/AudioEngine.h"
#include "audio/Db.h"
#include "audio/AudioTrajectory.h"
#include "audio/SpeakerPatch.h"
#include "audio/Vbap.h"
#include "core/CueList.h"
#include "core/PatchManager.h"
#include "core/Workspace.h"
#include "cues/Cue.h"
#include "cues/FadeCue.h"
#include "cues/GroupCue.h"
#include "cues/MemoCue.h"
#include "cues/TargetingCue.h"
#include "cues/WaitCue.h"
#include "lighting/LightCue.h"
#include "lighting/LightingEngine.h"
#include "midi/MidiCue.h"
#include "midi/MidiEngine.h"
#include "osc/OscCue.h"
#include "osc/OscEngine.h"
#include "video/VideoCue.h"
#include "video/VideoEngine.h"

#include <QRandomGenerator>
#include <QTimer>

#include <algorithm>
#include <cmath>

namespace quewi {

namespace {

// The list a cue lives in (cues are parented to their CueList).
core::CueList *listOf(const cues::Cue *c)
{
    return c ? qobject_cast<core::CueList *>(c->parent()) : nullptr;
}

QString nameOf(const cues::Cue *c)
{
    return c->name().isEmpty() ? c->typeName() : c->name();
}

// Synchronous fires nest (Start → target, Group → children, links). Past this
// depth a fire is bounced through the event loop instead — see fire().
constexpr int kMaxFireDepth = 16;

} // namespace

GoEngine::GoEngine(QObject *parent) : QObject(parent)
{
    // Instant/duration cues (memo, wait, fade, light-fade, …) advertise their
    // completion via cueFinished; route that into the auto-follow check.
    connect(this, &GoEngine::cueFinished, this, &GoEngine::onCueFinishedFollow);
}
GoEngine::~GoEngine() { cancelAll(0.0); }

void GoEngine::onTrajectoryTick()
{
    if (!m_audio) { m_trajectories.clear(); return; }

    const auto active = m_audio->activeVoices();
    QHash<quint64, double> posByVoice;
    posByVoice.reserve(active.size());
    for (const auto &av : active) posByVoice.insert(av.id, av.positionSeconds);

    for (auto it = m_trajectories.begin(); it != m_trajectories.end(); ) {
        const auto vid  = it.key();
        auto      &rec  = it.value();
        if (!posByVoice.contains(vid) || !rec.cue) {
            it = m_trajectories.erase(it);
            continue;
        }
        // positionSeconds is the voice's position IN THE FILE; trajectory
        // time is seconds since the cue started, i.e. since trim-in.
        const double t = std::max(0.0, posByVoice.value(vid) - rec.cue->trimInSeconds());
        const auto sample = rec.cue->trajectory().sampleAt(t);
        audio::Vbap v(rec.speakers);
        const auto gains = v.gains(static_cast<float>(sample.azimuthDeg),
                                   static_cast<float>(sample.elevationDeg),
                                   static_cast<float>(sample.spread),
                                   rec.outChannels);
        m_audio->setVoiceChannelGains(vid, gains);
        ++it;
    }

    if (m_trajectories.isEmpty() && m_trajectoryTimer) {
        m_trajectoryTimer->stop();
    }
}

void GoEngine::setWorkspace(core::Workspace *ws)              { m_workspace = ws; }
void GoEngine::setAudioEngine(audio::AudioEngine *e)
{
    m_audio = e;
    // Only a natural EOF advances an auto-follow audio cue (never a stop).
    if (m_audio)
        connect(m_audio, &audio::AudioEngine::voiceFinishedNatural,
                this, &GoEngine::onAudioVoiceFinishedNatural,
                Qt::UniqueConnection);
}
void GoEngine::setLightingEngine(lighting::LightingEngine *e) { m_lighting = e; }
void GoEngine::setVideoEngine(video::VideoEngine *e)
{
    m_video = e;
    if (m_video)
        connect(m_video, &video::VideoEngine::voiceFinishedNatural,
                this, &GoEngine::onVideoVoiceFinishedNatural,
                Qt::UniqueConnection);
}
void GoEngine::setOscEngine(osc::OscEngine *e)                { m_osc = e; }
void GoEngine::setMidiEngine(midi::MidiEngine *e)             { m_midi = e; }

// ── Lookups ────────────────────────────────────────────────────────────

cues::Cue *GoEngine::findCue(core::CueId id, const cues::Cue *context) const
{
    if (!m_workspace || id.isNull()) return nullptr;
    if (auto *own = listOf(context)) {
        for (int row = 0; row < own->cueCount(); ++row)
            if (auto *c = own->cueAt(row); c && c->id() == id) return c;
    }
    for (const auto &list : m_workspace->cueLists())
        for (int row = 0; row < list->cueCount(); ++row)
            if (auto *c = list->cueAt(row); c && c->id() == id) return c;
    return nullptr;
}

QSet<core::CueId> GoEngine::descendantsOf(const cues::Cue *cue) const
{
    QSet<core::CueId> out;
    auto *group = qobject_cast<const cues::GroupCue *>(cue);
    if (!group) return out;
    QList<core::CueId> stack = group->childIds();
    while (!stack.isEmpty()) {
        const core::CueId id = stack.takeLast();
        if (out.contains(id) || id == cue->id()) continue;   // cycle-safe
        out.insert(id);
        if (auto *sub = qobject_cast<cues::GroupCue *>(findCue(id, cue)))
            stack.append(sub->childIds());
    }
    return out;
}

cues::Cue *GoEngine::nextArmedAfter(const cues::Cue *cue, const QSet<core::CueId> &skip) const
{
    // Walk the cue's OWN list. It used to walk whichever list tab was on
    // screen, so switching tabs mid-chain made the continue silently stop.
    auto *list = listOf(cue);
    if (!list && m_workspace) list = m_workspace->activeCueList();
    if (!list || !cue) return nullptr;
    const int n = list->cueCount();
    for (int row = list->rowOf(const_cast<cues::Cue *>(cue)) + 1; row > 0 && row < n; ++row) {
        // Skip disarmed cues so a chain lands on the next ARMED cue instead
        // of dead-ending on a disarmed one (fire() would no-op).
        auto *c = list->cueAt(row);
        if (c && c->isArmed() && !skip.contains(c->id())) return c;
    }
    return nullptr;
}

cues::Cue *GoEngine::nextCueAfter(cues::Cue *cue) const
{
    // A group's children are rows right after it, but they fire WITH the
    // group — continuing into them would fire them a second time.
    return nextArmedAfter(cue, descendantsOf(cue));
}

cues::Cue *GoEngine::standbyAfter(cues::Cue *fired) const
{
    if (!fired) return nullptr;
    QSet<core::CueId> skip = descendantsOf(fired);
    const cues::Cue *c = fired;
    // Everything the auto-continue / auto-follow chain will fire by itself is
    // not a GO target: the playhead jumps past the whole chain, as in QLab.
    // (It used to stop on the second cue of the chain, so the next GO fired
    // cues the chain had already played.)
    for (int guard = 0; c && c->continueMode() != cues::ContinueMode::DoNotContinue
                        && guard < 100000; ++guard) {
        auto *n = nextArmedAfter(c, skip);
        if (!n) return nullptr;              // the chain runs off the end
        skip.unite(descendantsOf(n));
        c = n;
    }
    return nextArmedAfter(c, skip);
}

// ── Firing ─────────────────────────────────────────────────────────────

void GoEngine::after(int ms, std::function<void()> fn)
{
    auto *t = new QTimer(this);
    t->setSingleShot(true);
    m_pending.append(t);
    connect(t, &QTimer::timeout, this, [this, t, fn = std::move(fn)] {
        m_pending.removeAll(t);
        m_frozenTimers.remove(t);
        t->deleteLater();
        fn();
    });
    t->start(std::max(0, ms));
}

void GoEngine::fire(cues::Cue *cue, const QByteArray &outputDeviceOverride)
{
    if (!cue || !cue->isArmed()) return;

    // Start and Group cues fire their targets inline, so a cycle — a group
    // that Starts itself (the usual "loop this" setup), or two Starts aimed
    // at each other — used to recurse until the stack overflowed and quewi
    // crashed. Past a sane depth, bounce through the event loop instead: the
    // loop still runs, but on a flat stack.
    if (m_fireDepth >= kMaxFireDepth) {
        QPointer<cues::Cue> guard(cue);
        after(0, [this, guard, outputDeviceOverride] {
            if (guard) fire(guard, outputDeviceOverride);
        });
        return;
    }

    const double preWait = cue->preWait();
    if (preWait > 0.0) {
        QPointer<cues::Cue> guard(cue);
        after(static_cast<int>(preWait * 1000.0), [this, guard, outputDeviceOverride] {
            if (guard) runFire(guard, outputDeviceOverride);
        });
    } else {
        runFire(cue, outputDeviceOverride);
    }
}

void GoEngine::runFire(cues::Cue *cue, const QByteArray &outputDeviceOverride)
{
    ++m_fireDepth;
    doFire(cue, outputDeviceOverride);
    --m_fireDepth;
}

void GoEngine::stopTarget(cues::Cue *target, int depth)
{
    if (!target || depth > 32) return;
    // A stopped cue must never advance an auto-follow chain.
    m_followPending.remove(target);
    if (auto *ac = qobject_cast<audio::AudioCue *>(target)) {
        if (m_audio && ac->currentVoiceId() != 0) m_audio->stop(ac->currentVoiceId(), 0.1);
    } else if (auto *vc = qobject_cast<video::VisualCue *>(target)) {
        if (m_video && vc->currentVoiceId() != 0) m_video->stop(vc->currentVoiceId());
    } else if (auto *g = qobject_cast<cues::GroupCue *>(target)) {
        m_groupRemaining.remove(g->id());
        for (const auto &id : g->childIds())
            stopTarget(findCue(id, g), depth + 1);
    }
}

void GoEngine::doFire(cues::Cue *cue, const QByteArray &outputDeviceOverride)
{
    if (!cue) return;
    using namespace quewi;

    auto status = [&](const QString &m) { emit statusMessage(m); };

    // Dispatch by type. As more cue types come online they hook in here.
    if (auto *oscCue = qobject_cast<osc::OscCue *>(cue)) {
        if (m_osc) {
            const auto dv = oscCue->destination();
            osc::Destination dest{
                dv.id, dv.name, dv.host, dv.port,
                static_cast<osc::Destination::Transport>(dv.transport)
            };
            if (m_osc->send(dest, oscCue->buildMessage())) {
                status(tr("GO: %1 → %2:%3 %4")
                    .arg(QString::number(cue->number(), 'f', 2),
                         dest.host, QString::number(dest.port),
                         oscCue->field(QStringLiteral("address")).toString()));
            }
        }
    } else if (auto *audioCue = qobject_cast<audio::AudioCue *>(cue)) {
        if (m_audio) {
            audioCue->prepare();
            auto file = audioCue->audioFile();
            if (!file) {
                status(tr("GO: no file selected"));
            } else if (file->state() == audio::AudioFile::State::Failed) {
                status(tr("GO: decode failed — %1").arg(file->errorString()));
            } else if (file->state() == audio::AudioFile::State::Empty
                       || !file->snapshot()) {
                // No published snapshot yet — the QAudioDecoder hasn't
                // delivered the first buffer. This is brief (< 200 ms
                // typically) and only happens if GO arrives faster
                // than the first decoded chunk. With progressive
                // snapshots from v0.9.4 onward, Loading-with-snapshot
                // is now a valid play state.
                status(tr("GO: audio still decoding"));
            } else {
                audio::VoiceParams p;
                p.gainDb         = audioCue->gainDb();
                p.fadeInSeconds  = audioCue->fadeInSeconds();
                p.fadeOutSeconds = audioCue->fadeOutSeconds();
                p.trimInSeconds  = audioCue->trimInSeconds();
                p.trimOutSeconds = audioCue->trimOutSeconds();
                p.pan            = audioCue->pan();
                p.loop           = audioCue->loop();
                // Per-cue effects chain (EQ/comp/reverb/delay) built fresh
                // for this voice from the cue's saved editor rack. Empty =
                // dry. Applied as a stereo insert in the mixer.
                p.effects        = audioCue->buildEffectChain();
                p.outputDeviceId = outputDeviceOverride.isEmpty()
                                       ? audioCue->outputDeviceId()
                                       : outputDeviceOverride;
                // Per-output sends (dB → linear). Object-audio cues
                // don't use this path — channelGains owns routing.
                if (!audioCue->objectAudioEnabled()
                    && !audioCue->outputGainsDb().isEmpty())
                {
                    QList<float> linear;
                    linear.reserve(audioCue->outputGainsDb().size());
                    for (double db : audioCue->outputGainsDb()) {
                        linear.append(float(audio::dbToLinear(db)));
                    }
                    p.outputGains = std::move(linear);
                }

                // Object Audio: convert (azimuth, elevation, spread) +
                // speaker patch into per-channel gains. If the patch is
                // missing or empty, fall back to legacy stereo pan so
                // the cue still plays — better than silence.
                QList<audio::Speaker> trajSpeakers;
                int                   trajOutChans = 0;
                if (audioCue->objectAudioEnabled() && m_workspace) {
                    const auto speakers = audio::readSpeakers(
                        m_workspace->patches(), audioCue->speakerPatchId());
                    const int outChans = m_audio->outputChannelCount(p.outputDeviceId);
                    if (!speakers.isEmpty() && outChans > 0) {
                        // Initial gains: keyframe @ t=0 if a trajectory
                        // exists, otherwise the static cue position.
                        double az = audioCue->objectAzimuthDeg();
                        double el = audioCue->objectElevationDeg();
                        double sp = audioCue->objectSpread();
                        if (!audioCue->trajectory().isEmpty()) {
                            const auto s = audioCue->trajectory().sampleAt(0.0);
                            az = s.azimuthDeg;
                            el = s.elevationDeg;
                            sp = s.spread;
                        }
                        audio::Vbap v(speakers);
                        p.channelGains = v.gains(
                            static_cast<float>(az),
                            static_cast<float>(el),
                            static_cast<float>(sp),
                            outChans);
                        trajSpeakers = speakers;
                        trajOutChans = outChans;
                    }
                }

                const auto vid = m_audio->fire(file, p);
                audioCue->setCurrentVoiceId(vid);
                if (vid != 0
                    && audioCue->objectAudioEnabled()
                    && !audioCue->trajectory().isEmpty()
                    && !trajSpeakers.isEmpty()
                    && trajOutChans > 0)
                {
                    TrajectoryEntry rec;
                    rec.cue         = audioCue;
                    rec.speakers    = std::move(trajSpeakers);
                    rec.outChannels = trajOutChans;
                    m_trajectories.insert(vid, std::move(rec));
                    if (!m_trajectoryTimer) {
                        m_trajectoryTimer = new QTimer(this);
                        m_trajectoryTimer->setInterval(33);   // ~30 Hz
                        connect(m_trajectoryTimer, &QTimer::timeout,
                                this, &GoEngine::onTrajectoryTick);
                    }
                    if (!m_trajectoryTimer->isActive()) m_trajectoryTimer->start();
                }
                if (vid == 0) status(tr("GO: audio engine failed — %1")
                    .arg(m_audio->lastError()));
                else status(tr("GO: ▶ %1").arg(nameOf(cue)));
            }
        }
    } else if (auto *lightCue = qobject_cast<lighting::LightCue *>(cue)) {
        if (m_lighting) {
            QHash<int, int> values;
            const auto &chs = lightCue->channels();
            for (auto it = chs.constBegin(); it != chs.constEnd(); ++it) {
                values.insert(it.key(), it.value());
            }
            m_lighting->applyChannels(lightCue->universe(), values);
            status(tr("GO: ⚡ Light U%1").arg(lightCue->universe()));
        }
    } else if (auto *lfadeCue = qobject_cast<lighting::LightFadeCue *>(cue)) {
        auto *target = qobject_cast<lighting::LightCue *>(findCue(lfadeCue->targetId(), cue));
        if (m_lighting && target) {
            QHash<int, int> values;
            const auto &chs = target->channels();
            for (auto it = chs.constBegin(); it != chs.constEnd(); ++it) {
                values.insert(it.key(), it.value());
            }
            m_lighting->fadeChannels(target->universe(), values, lfadeCue->durationSeconds());
            status(tr("Light Fade: U%1 over %2 s")
                .arg(target->universe()).arg(lfadeCue->durationSeconds()));
        }
    } else if (auto *visualCue = qobject_cast<video::VisualCue *>(cue)) {
        if (m_video) {
            video::VideoVoiceParams p;
            p.screenIndex = visualCue->screenIndex();
            p.geometry = QRectF(visualCue->posX(), visualCue->posY(),
                                visualCue->posW(), visualCue->posH());
            p.opacity = visualCue->opacity();
            if (auto *vc = qobject_cast<video::VideoCue *>(cue)) {
                p.kind = video::VideoVoiceParams::Video;
                p.filePath = vc->filePath();
                p.loop = vc->loop();
            } else if (auto *ic = qobject_cast<video::ImageCue *>(cue)) {
                p.kind = video::VideoVoiceParams::Image;
                p.filePath = ic->filePath();
            } else if (auto *tc = qobject_cast<video::TextCue *>(cue)) {
                p.kind = video::VideoVoiceParams::Text;
                p.text = tc->text();
                p.fontPixelSize = tc->fontPixelSize();
                p.textColor = tc->textColor();
            }
            // Store the live voice id on the cue so the Inspector scrubber
            // can resolve cue -> VideoVoiceId -> VideoLayer to seek/pause.
            visualCue->setCurrentVoiceId(m_video->fire(p));
            status(tr("GO: ▶ %1 on screen %2")
                .arg(nameOf(cue))
                .arg(visualCue->screenIndex()));
        }
    } else if (auto *fadeCue = qobject_cast<cues::FadeCue *>(cue)) {
        auto *targetCue   = findCue(fadeCue->targetId(), cue);
        auto *audioTarget = qobject_cast<audio::AudioCue *>(targetCue);
        auto *videoTarget = qobject_cast<video::VisualCue *>(targetCue);
        if (m_audio && audioTarget && audioTarget->currentVoiceId() != 0
            && fadeCue->parameter() == QLatin1String("gainDb")) {
            m_audio->fadeGain(audioTarget->currentVoiceId(),
                              fadeCue->targetValue(), fadeCue->durationSeconds());
            status(tr("Fade → %1 dB over %2 s")
                .arg(fadeCue->targetValue()).arg(fadeCue->durationSeconds()));
        } else if (m_video && videoTarget && videoTarget->currentVoiceId() != 0
                   && fadeCue->parameter() == QLatin1String("opacity")) {
            m_video->fadeOpacity(videoTarget->currentVoiceId(),
                                 fadeCue->targetValue(), fadeCue->durationSeconds());
            status(tr("Fade → %1 opacity over %2 s")
                .arg(fadeCue->targetValue(), 0, 'f', 2)
                .arg(fadeCue->durationSeconds()));
        } else {
            status(tr("Fade: target not playing"));
        }
    } else if (qobject_cast<cues::WaitCue *>(cue) != nullptr) {
        status(tr("Wait %1 s")
            .arg(qobject_cast<cues::WaitCue *>(cue)->durationSeconds()));
    } else if (auto *startCue = qobject_cast<cues::StartCue *>(cue)) {
        if (auto *target = findCue(startCue->targetId(), cue)) {
            // If the target is paused, Start resumes it from the pause point
            // rather than firing a fresh voice. Operators expect this — Start
            // after Pause means "go again" not "start over."
            auto *ac = qobject_cast<audio::AudioCue *>(target);
            auto *vc = qobject_cast<video::VisualCue *>(target);
            if (ac && m_audio && ac->currentVoiceId() != 0
                && m_audio->isPaused(ac->currentVoiceId()))
            {
                m_audio->resume(ac->currentVoiceId());
                m_pausedAudio.removeAll(ac->currentVoiceId());
                status(tr("Start (resume) → %1").arg(nameOf(ac)));
            } else if (vc && m_video && vc->currentVoiceId() != 0
                       && m_video->transport(vc->currentVoiceId()).paused) {
                m_video->resume(vc->currentVoiceId());
                m_pausedVideo.removeAll(vc->currentVoiceId());
                status(tr("Start (resume) → %1").arg(nameOf(vc)));
            } else {
                status(tr("Start → %1").arg(nameOf(target)));
                fire(target);
            }
        } else {
            status(tr("Start: target not found"));
        }
    } else if (auto *stopCue = qobject_cast<cues::StopCue *>(cue)) {
        // Stops audio, video, or a whole group (it used to handle audio only
        // and report every other target as "not found").
        if (auto *target = findCue(stopCue->targetId(), cue)) {
            stopTarget(target);
            status(tr("Stop → %1").arg(nameOf(target)));
        } else {
            status(tr("Stop: target not found"));
        }
    } else if (auto *gotoCue = qobject_cast<cues::GotoCue *>(cue)) {
        if (auto *target = findCue(gotoCue->targetId(), cue)) {
            emit gotoRequested(target->id());
            status(tr("Goto %1").arg(QString::number(target->number(), 'f', 2)));
        }
    } else if (auto *pauseCue = qobject_cast<cues::PauseCue *>(cue)) {
        // Real pause: the voice keeps its read position and rejoins the mix
        // unchanged when a Start cue targeting it fires.
        auto *target = findCue(pauseCue->targetId(), cue);
        if (auto *ac = qobject_cast<audio::AudioCue *>(target)) {
            if (m_audio && ac->currentVoiceId() != 0
                && m_audio->pause(ac->currentVoiceId())) {
                status(tr("Pause → %1").arg(nameOf(ac)));
            } else {
                status(tr("Pause: target not playing"));
            }
        } else if (auto *vc = qobject_cast<video::VisualCue *>(target)) {
            if (m_video && vc->currentVoiceId() != 0) {
                m_video->pause(vc->currentVoiceId());
                status(tr("Pause → %1").arg(nameOf(vc)));
            } else {
                status(tr("Pause: target not playing"));
            }
        } else {
            status(tr("Pause: target not found"));
        }
    } else if (auto *loadCue = qobject_cast<cues::LoadCue *>(cue)) {
        if (auto *ac = qobject_cast<audio::AudioCue *>(findCue(loadCue->targetId(), cue))) {
            ac->prepare();
            status(tr("Load → %1").arg(nameOf(ac)));
        } else {
            status(tr("Load: target not an audio cue"));
        }
    } else if (auto *resetCue = qobject_cast<cues::ResetCue *>(cue)) {
        auto *target = findCue(resetCue->targetId(), cue);
        if (auto *ac = qobject_cast<audio::AudioCue *>(target)) {
            if (m_audio && ac->currentVoiceId() != 0) {
                m_audio->stop(ac->currentVoiceId(), 0.0);
            }
            m_followPending.remove(ac);
            ac->prepare();      // re-decode head so next fire is instant
            status(tr("Reset → %1").arg(nameOf(ac)));
        } else if (target) {
            stopTarget(target);
            status(tr("Reset → %1").arg(nameOf(target)));
        } else {
            status(tr("Reset: target not found"));
        }
    } else if (qobject_cast<cues::DevampCue *>(cue) != nullptr) {
        // Vamping (looping until devamped) lands with the audio editor
        // overhaul. Until then this is a documented no-op so shows
        // authored against future builds round-trip cleanly.
        status(tr("Devamp: vamping not yet implemented"));
    } else if (auto *midiCue = qobject_cast<midi::MidiCue *>(cue)) {
        if (m_midi) {
            if (m_midi->sendRaw(midiCue->portName(), midiCue->bytes())) {
                status(tr("MIDI → %1 (%2 bytes)")
                    .arg(midiCue->portName().isEmpty() ? tr("(default)") : midiCue->portName())
                    .arg(midiCue->bytes().size()));
            } else {
                status(tr("MIDI: %1").arg(m_midi->lastError()));
            }
        }
    } else if (auto *mscCue = qobject_cast<midi::MscCue *>(cue)) {
        if (m_midi) {
            if (m_midi->sendMsc(mscCue->portName(),
                                static_cast<quint8>(mscCue->deviceId()),
                                static_cast<quint8>(mscCue->commandFormat()),
                                static_cast<quint8>(mscCue->command()),
                                mscCue->buildPayload())) {
                status(tr("MSC → device %1, cmd 0x%2")
                    .arg(mscCue->deviceId())
                    .arg(mscCue->command(), 2, 16, QChar('0')));
            } else {
                status(tr("MSC: %1").arg(m_midi->lastError()));
            }
        }
    } else if (auto *groupCue = qobject_cast<cues::GroupCue *>(cue)) {
        const auto kids = groupCue->childIds();
        const auto offs = groupCue->childOffsets();

        // Resolve which children this GO will actually fire, and register the
        // group as running BEFORE firing any of them, so a child that finishes
        // instantly is still counted. The group finishes (cueFinished → an
        // auto-follow group continues) when the last of them finishes.
        QList<cues::Cue *> toFire;   // in fire order
        switch (groupCue->mode()) {
        case cues::GroupCue::Mode::Parallel:
        case cues::GroupCue::Mode::Sequential:
        case cues::GroupCue::Mode::Timeline:
            for (const auto &id : kids)
                if (auto *c = findCue(id, groupCue); c && c->isArmed()) toFire.append(c);
            break;
        case cues::GroupCue::Mode::StartFirst:
            if (!kids.isEmpty())
                if (auto *c = findCue(kids.first(), groupCue); c && c->isArmed())
                    toFire.append(c);
            break;
        case cues::GroupCue::Mode::StartRandom:
            if (!kids.isEmpty()) {
                const int idx = QRandomGenerator::global()->bounded(kids.size());
                if (auto *c = findCue(kids[idx], groupCue); c && c->isArmed())
                    toFire.append(c);
                status(tr("Group ▶ random child %1/%2").arg(idx + 1).arg(kids.size()));
            }
            break;
        }
        QSet<core::CueId> running;
        for (auto *c : toFire) running.insert(c->id());
        m_groupRemaining.insert(groupCue->id(), running);
        if (running.isEmpty()) {
            // Nothing to run → the group is done straight away.
            QPointer<cues::Cue> safe(groupCue);
            after(0, [this, safe] {
                if (safe) { m_groupRemaining.remove(safe->id()); emit cueFinished(safe.data()); }
            });
        }

        switch (groupCue->mode()) {
        case cues::GroupCue::Mode::Parallel:
            status(tr("Group ▶ %1 children (parallel)").arg(toFire.size()));
            for (auto *c : toFire) fire(c);
            break;
        case cues::GroupCue::Mode::Sequential: {
            status(tr("Group ▶ %1 children (sequential)").arg(toFire.size()));
            double delay = 0.0;
            const double step = std::max(0.0, groupCue->stepInterval());
            for (auto *child : toFire) {
                if (delay <= 0.0) {
                    fire(child);
                } else {
                    QPointer<cues::Cue> guard(child);
                    after(static_cast<int>(delay * 1000.0),
                          [this, guard] { if (guard) fire(guard); });
                }
                delay += step;
            }
            break;
        }
        case cues::GroupCue::Mode::StartFirst:
            if (!toFire.isEmpty()) {
                status(tr("Group ▶ first child"));
                fire(toFire.first());
            }
            break;
        case cues::GroupCue::Mode::StartRandom:
            if (!toFire.isEmpty()) fire(toFire.first());
            break;
        case cues::GroupCue::Mode::Timeline: {
            status(tr("Group ▶ %1 children (timeline)").arg(toFire.size()));
            for (auto *child : toFire) {
                const int i = kids.indexOf(child->id());
                const double off = (i >= 0 && i < offs.size()) ? std::max(0.0, offs[i]) : 0.0;
                if (off <= 0.0) {
                    fire(child);
                } else {
                    QPointer<cues::Cue> guard(child);
                    after(static_cast<int>(off * 1000.0),
                          [this, guard] { if (guard) fire(guard); });
                }
            }
            break;
        }
        }
    } else {
        status(tr("GO: %1 %2")
            .arg(QString::number(cue->number(), 'f', 2), cue->name()));
    }

    emit cueFired(cue);

    // Schedule cueFinished emission. Audio + Video cues finish when
    // their engine's voiceFinished fires (handled by MainWindow); groups
    // finish when their last child does (noteChildFinished); everything
    // else either has a known duration or is "instant" (the cue's effect IS
    // the GO press, so emit on the next event-loop turn so OSC subscribers
    // see fired→finished in order). Tracked, so a panic cancels them.
    double finishedDelay = -1.0;
    if (auto *lf = qobject_cast<lighting::LightFadeCue *>(cue)) {
        finishedDelay = lf->durationSeconds();
    } else if (auto *fc = qobject_cast<cues::FadeCue *>(cue)) {
        finishedDelay = fc->durationSeconds();
    } else if (auto *wc = qobject_cast<cues::WaitCue *>(cue)) {
        finishedDelay = wc->durationSeconds();
    } else if (qobject_cast<cues::MemoCue *>(cue)
            || qobject_cast<osc::OscCue *>(cue)
            || qobject_cast<midi::MidiCue *>(cue)
            || qobject_cast<midi::MscCue *>(cue)
            || qobject_cast<cues::TargetingCue *>(cue)
            || qobject_cast<lighting::LightCue *>(cue)) {
        finishedDelay = 0.0;   // instant
    }
    if (finishedDelay >= 0.0) {
        QPointer<cues::Cue> safe(cue);
        after(int(finishedDelay * 1000.0), [this, safe] {
            if (safe) emit cueFinished(safe.data());
        });
    }

    // Continue logic. Wait cues fold their duration into the chain delay.
    double waitExtra = 0.0;
    if (auto *w = qobject_cast<cues::WaitCue *>(cue)) waitExtra = w->durationSeconds();

    switch (cue->continueMode()) {
    case cues::ContinueMode::DoNotContinue:
        break;
    case cues::ContinueMode::AutoContinue:
        // Fire the NEXT cue once this cue has started, after its post-wait
        // (QLab semantics, and what the docs have always said; with the
        // default post-wait of 0 that's immediate). A Wait cue folds its
        // duration in, so Wait + AutoContinue still waits it out.
        scheduleContinue(cue, waitExtra + std::max(0.0, cue->postWait()));
        break;
    case cues::ContinueMode::AutoFollow:
        // Defer: the next cue fires only when THIS cue's action finishes.
        // Mark it pending; onCueFinishedFollow (instant/duration cues and
        // groups) or on{Audio,Video}VoiceFinishedNatural (a track reaching
        // its end) does the actual continue, then post-wait. cancelAll()
        // clears the set so a panic mid-cue never advances.
        m_followPending.insert(cue);
        break;
    }
}

void GoEngine::scheduleContinue(cues::Cue *cue, double delaySeconds)
{
    auto *next = nextCueAfter(cue);
    if (!next) return;
    QPointer<cues::Cue> guard(next);
    // Even a zero-delay continue goes through the event loop rather than
    // calling fire() inline: a list of all-auto-follow zero-wait cues would
    // otherwise recurse for the whole list on one stack frame, and a cue that
    // GOTOs backward into such a chain would hang the UI synchronously.
    after(static_cast<int>(delaySeconds * 1000.0), [this, guard] {
        if (guard) fire(guard);
    });
}

// ── Panic / Fade All / Pause ───────────────────────────────────────────

void GoEngine::cancelScheduling()
{
    for (auto *t : m_pending) { t->stop(); t->deleteLater(); }
    m_pending.clear();
    m_frozenTimers.clear();
    // Disarm every pending auto-follow so a late cueFinished or
    // voiceFinishedNatural arriving after the cancel can't advance.
    m_followPending.clear();
    m_groupRemaining.clear();
}

void GoEngine::clearPauseState()
{
    const bool was = m_paused;
    m_paused = false;
    m_pausedAudio.clear();
    m_pausedVideo.clear();
    m_frozenTimers.clear();
    if (was) emit pausedChanged(false);
}

void GoEngine::cancelAll(double fadeOutSeconds)
{
    cancelScheduling();
    clearPauseState();
    if (m_audio)    m_audio->stopAll(fadeOutSeconds);
    if (m_lighting) m_lighting->blackout();
    if (m_video)    m_video->stopAll();
    m_trajectories.clear();
    if (m_trajectoryTimer) m_trajectoryTimer->stop();
}

void GoEngine::fadeAll(double seconds)
{
    cancelScheduling();
    clearPauseState();
    if (m_audio)    m_audio->stopAll(seconds);
    if (m_lighting) m_lighting->fadeOutAll(seconds);
    if (m_video)    m_video->fadeOutAll(seconds);
}

void GoEngine::pauseAll()
{
    if (m_paused) return;
    m_paused = true;
    if (m_audio) {
        for (const auto &av : m_audio->activeVoices()) {
            if (!m_audio->isPaused(av.id) && m_audio->pause(av.id))
                m_pausedAudio.append(av.id);
        }
    }
    if (m_video) {
        for (const auto id : m_video->activeVoiceIds()) {
            const auto t = m_video->transport(id);
            if (t.valid && !t.paused) {
                m_video->pause(id);
                m_pausedVideo.append(id);
            }
        }
    }
    // Freeze the clock: every pre-wait, continue, wait duration and group
    // step stops where it is and resumes with exactly the time it had left.
    for (auto *t : m_pending) {
        if (t->isActive()) {
            m_frozenTimers.insert(t, t->remainingTime());
            t->stop();
        }
    }
    emit pausedChanged(true);
}

void GoEngine::resumeAll()
{
    if (!m_paused) return;
    if (m_audio) for (const auto id : m_pausedAudio) m_audio->resume(id);
    if (m_video) for (const auto id : m_pausedVideo) m_video->resume(id);
    for (auto it = m_frozenTimers.constBegin(); it != m_frozenTimers.constEnd(); ++it)
        if (m_pending.contains(it.key())) it.key()->start(std::max(0, it.value()));
    m_paused = false;
    m_pausedAudio.clear();
    m_pausedVideo.clear();
    m_frozenTimers.clear();
    emit pausedChanged(false);
}

// ── Auto-follow / group completion ─────────────────────────────────────

void GoEngine::tryFollow(cues::Cue *cue)
{
    // remove() returns true only if the cue is still armed — cancelAll() (a
    // panic) empties the set, so a follow can't fire after a stop.
    if (cue && m_followPending.remove(cue))
        scheduleContinue(cue, cue->postWait());
}

void GoEngine::noteChildFinished(cues::Cue *child)
{
    if (!child || m_groupRemaining.isEmpty()) return;
    QList<core::CueId> done;
    for (auto it = m_groupRemaining.begin(); it != m_groupRemaining.end(); ) {
        if (it.value().remove(child->id()) && it.value().isEmpty()) {
            done.append(it.key());
            it = m_groupRemaining.erase(it);
        } else {
            ++it;
        }
    }
    for (const auto &gid : done) {
        QPointer<cues::Cue> safe(findCue(gid, child));
        after(0, [this, safe] { if (safe) emit cueFinished(safe.data()); });
    }
}

void GoEngine::onCueFinishedFollow(cues::Cue *cue)
{
    tryFollow(cue);
    noteChildFinished(cue);
}

void GoEngine::onAudioVoiceFinishedNatural(quint64 voiceId)
{
    if (!m_workspace) return;
    for (const auto &list : m_workspace->cueLists())
        for (int i = 0; i < list->cueCount(); ++i)
            if (auto *ac = qobject_cast<audio::AudioCue *>(list->cueAt(i)))
                if (ac->currentVoiceId() == voiceId) {
                    tryFollow(ac);
                    noteChildFinished(ac);
                    return;
                }
}

void GoEngine::onVideoVoiceFinishedNatural(quint64 voiceId)
{
    if (!m_workspace) return;
    for (const auto &list : m_workspace->cueLists())
        for (int i = 0; i < list->cueCount(); ++i)
            if (auto *vc = qobject_cast<video::VisualCue *>(list->cueAt(i)))
                if (vc->currentVoiceId() == voiceId) {
                    tryFollow(vc);
                    noteChildFinished(vc);
                    return;
                }
}

QSet<quint64> GoEngine::activeAudioVoiceIds() const
{
    QSet<quint64> ids;
    if (!m_audio) return ids;
    const auto voices = m_audio->activeVoices();
    ids.reserve(voices.size());
    for (const auto &v : voices) ids.insert(v.id);
    return ids;
}

} // namespace quewi
