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

#include <cmath>

namespace quewi {

GoEngine::GoEngine(QObject *parent) : QObject(parent) {}
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
        const double t = posByVoice.value(vid);
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
void GoEngine::setAudioEngine(audio::AudioEngine *e)          { m_audio = e; }
void GoEngine::setLightingEngine(lighting::LightingEngine *e) { m_lighting = e; }
void GoEngine::setVideoEngine(video::VideoEngine *e)          { m_video = e; }
void GoEngine::setOscEngine(osc::OscEngine *e)                { m_osc = e; }
void GoEngine::setMidiEngine(midi::MidiEngine *e)             { m_midi = e; }

cues::Cue *GoEngine::findCue(core::CueId id) const
{
    if (!m_workspace) return nullptr;
    auto *list = m_workspace->activeCueList();
    if (!list) return nullptr;
    for (int row = 0; row < list->cueCount(); ++row) {
        auto *c = list->cueAt(row);
        if (c && c->id() == id) return c;
    }
    return nullptr;
}

cues::Cue *GoEngine::nextCueAfter(cues::Cue *cue) const
{
    if (!m_workspace || !cue) return nullptr;
    auto *list = m_workspace->activeCueList();
    if (!list) return nullptr;
    const int n = list->cueCount();
    for (int row = 0; row < n; ++row) {
        if (list->cueAt(row) != cue) continue;
        // Skip past any disarmed cues so an auto-continue / auto-follow
        // chain lands on the next ARMED cue instead of dead-ending on a
        // disarmed one (fire() would otherwise no-op and break the chain).
        for (int next = row + 1; next < n; ++next) {
            auto *c = list->cueAt(next);
            if (c && c->isArmed()) return c;
        }
        return nullptr;
    }
    return nullptr;
}

void GoEngine::fire(cues::Cue *cue, const QByteArray &outputDeviceOverride)
{
    if (!cue || !cue->isArmed()) return;

    const double preWait = cue->preWait();
    if (preWait > 0.0) {
        auto *t = new QTimer(this);
        t->setSingleShot(true);
        m_pending.append(t);
        QPointer<cues::Cue> guard(cue);
        connect(t, &QTimer::timeout, this, [this, t, guard, outputDeviceOverride] {
            m_pending.removeAll(t);
            t->deleteLater();
            if (guard) doFire(guard, outputDeviceOverride);
        });
        t->start(static_cast<int>(preWait * 1000.0));
    } else {
        doFire(cue, outputDeviceOverride);
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
                else status(tr("GO: ▶ %1").arg(
                    cue->name().isEmpty() ? cue->typeName() : cue->name()));
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
        auto *target = qobject_cast<lighting::LightCue *>(findCue(lfadeCue->targetId()));
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
                .arg(cue->name().isEmpty() ? cue->typeName() : cue->name())
                .arg(visualCue->screenIndex()));
        }
    } else if (auto *fadeCue = qobject_cast<cues::FadeCue *>(cue)) {
        auto *target = qobject_cast<audio::AudioCue *>(findCue(fadeCue->targetId()));
        if (m_audio && target && target->currentVoiceId() != 0
            && fadeCue->parameter() == QLatin1String("gainDb")) {
            m_audio->fadeGain(target->currentVoiceId(),
                              fadeCue->targetValue(), fadeCue->durationSeconds());
            status(tr("Fade → %1 dB over %2 s")
                .arg(fadeCue->targetValue()).arg(fadeCue->durationSeconds()));
        } else {
            status(tr("Fade: target not playing"));
        }
    } else if (qobject_cast<cues::WaitCue *>(cue) != nullptr) {
        status(tr("Wait %1 s")
            .arg(qobject_cast<cues::WaitCue *>(cue)->durationSeconds()));
    } else if (auto *startCue = qobject_cast<cues::StartCue *>(cue)) {
        if (auto *target = findCue(startCue->targetId())) {
            // If the target is an audio cue currently paused, Start
            // resumes it from the pause point rather than firing a
            // fresh voice. Operators expect this — Start after Pause
            // means "go again" not "start over."
            if (auto *ac = qobject_cast<audio::AudioCue *>(target);
                ac && m_audio && ac->currentVoiceId() != 0
                && m_audio->isPaused(ac->currentVoiceId()))
            {
                m_audio->resume(ac->currentVoiceId());
                status(tr("Start (resume) → %1").arg(
                    ac->name().isEmpty() ? ac->typeName() : ac->name()));
            } else {
                status(tr("Start → %1").arg(
                    target->name().isEmpty() ? target->typeName() : target->name()));
                fire(target);
            }
        } else {
            status(tr("Start: target not found"));
        }
    } else if (auto *stopCue = qobject_cast<cues::StopCue *>(cue)) {
        if (auto *ac = qobject_cast<audio::AudioCue *>(findCue(stopCue->targetId()))) {
            if (m_audio && ac->currentVoiceId() != 0) {
                m_audio->stop(ac->currentVoiceId(), 0.1);
                status(tr("Stop → %1").arg(
                    ac->name().isEmpty() ? ac->typeName() : ac->name()));
            }
        } else {
            status(tr("Stop: target not found / not playing"));
        }
    } else if (auto *gotoCue = qobject_cast<cues::GotoCue *>(cue)) {
        if (auto *target = findCue(gotoCue->targetId())) {
            emit gotoRequested(target->id());
            status(tr("Goto %1").arg(QString::number(target->number(), 'f', 2)));
        }
    } else if (auto *pauseCue = qobject_cast<cues::PauseCue *>(cue)) {
        // Real pause: voice keeps its read position and rejoins the mix
        // unchanged when a Start cue targeting it fires.
        if (auto *ac = qobject_cast<audio::AudioCue *>(findCue(pauseCue->targetId()))) {
            if (m_audio && ac->currentVoiceId() != 0
                && m_audio->pause(ac->currentVoiceId())) {
                status(tr("Pause → %1").arg(
                    ac->name().isEmpty() ? ac->typeName() : ac->name()));
            } else {
                status(tr("Pause: target not playing"));
            }
        } else {
            status(tr("Pause: target not found"));
        }
    } else if (auto *loadCue = qobject_cast<cues::LoadCue *>(cue)) {
        if (auto *ac = qobject_cast<audio::AudioCue *>(findCue(loadCue->targetId()))) {
            ac->prepare();
            status(tr("Load → %1").arg(
                ac->name().isEmpty() ? ac->typeName() : ac->name()));
        } else {
            status(tr("Load: target not an audio cue"));
        }
    } else if (auto *resetCue = qobject_cast<cues::ResetCue *>(cue)) {
        if (auto *ac = qobject_cast<audio::AudioCue *>(findCue(resetCue->targetId()))) {
            if (m_audio && ac->currentVoiceId() != 0) {
                m_audio->stop(ac->currentVoiceId(), 0.0);
            }
            ac->prepare();      // re-decode head so next fire is instant
            status(tr("Reset → %1").arg(
                ac->name().isEmpty() ? ac->typeName() : ac->name()));
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
        switch (groupCue->mode()) {
        case cues::GroupCue::Mode::Parallel:
            status(tr("Group ▶ %1 children (parallel)").arg(kids.size()));
            for (const auto &id : kids) if (auto *c = findCue(id)) fire(c);
            break;
        case cues::GroupCue::Mode::Sequential: {
            status(tr("Group ▶ %1 children (sequential)").arg(kids.size()));
            double delay = 0.0;
            const double step = std::max(0.0, groupCue->stepInterval());
            for (const auto &id : kids) {
                auto *child = findCue(id);
                if (!child) continue;
                if (delay <= 0.0) {
                    fire(child);
                } else {
                    auto *t = new QTimer(this);
                    t->setSingleShot(true);
                    m_pending.append(t);
                    QPointer<cues::Cue> guard(child);
                    connect(t, &QTimer::timeout, this, [this, t, guard] {
                        m_pending.removeAll(t);
                        t->deleteLater();
                        if (guard) fire(guard);
                    });
                    t->start(static_cast<int>(delay * 1000.0));
                }
                delay += step;
            }
            break;
        }
        case cues::GroupCue::Mode::StartFirst:
            if (!kids.isEmpty()) {
                if (auto *c = findCue(kids.first())) {
                    status(tr("Group ▶ first child"));
                    fire(c);
                }
            }
            break;
        case cues::GroupCue::Mode::StartRandom:
            if (!kids.isEmpty()) {
                const int idx = QRandomGenerator::global()->bounded(kids.size());
                if (auto *c = findCue(kids[idx])) {
                    status(tr("Group ▶ random child %1/%2").arg(idx + 1).arg(kids.size()));
                    fire(c);
                }
            }
            break;
        case cues::GroupCue::Mode::Timeline: {
            status(tr("Group ▶ %1 children (timeline)").arg(kids.size()));
            for (int i = 0; i < kids.size(); ++i) {
                auto *child = findCue(kids[i]);
                if (!child) continue;
                const double off = (i < offs.size()) ? std::max(0.0, offs[i]) : 0.0;
                if (off <= 0.0) {
                    fire(child);
                } else {
                    auto *t = new QTimer(this);
                    t->setSingleShot(true);
                    m_pending.append(t);
                    QPointer<cues::Cue> guard(child);
                    connect(t, &QTimer::timeout, this, [this, t, guard] {
                        m_pending.removeAll(t);
                        t->deleteLater();
                        if (guard) fire(guard);
                    });
                    t->start(static_cast<int>(off * 1000.0));
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
    // their engine's voiceFinished fires (handled by MainWindow);
    // everything else either has a known duration (fold into a
    // QTimer::singleShot) or is "instant" (the cue's effect IS the
    // GO press, so emit immediately on the next event-loop turn so
    // OSC subscribers see fired→finished in order).
    double finishedDelay = -1.0;
    bool   isInstant     = false;
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
        // Instant cues — the GO press IS the cue's effect. Light
        // (static, not fade) lays down its channel values on the
        // next tick and is done; everything in this branch is the
        // same shape.
        isInstant = true;
    }
    if (isInstant) {
        QPointer<cues::Cue> safe(cue);
        QTimer::singleShot(0, this, [this, safe] {
            if (safe) emit cueFinished(safe.data());
        });
    } else if (finishedDelay >= 0.0) {
        QPointer<cues::Cue> safe(cue);
        QTimer::singleShot(int(finishedDelay * 1000.0), this,
            [this, safe] {
                if (safe) emit cueFinished(safe.data());
            });
    }

    // Continue logic. Wait cues fold their duration into the chain delay.
    double waitExtra = 0.0;
    if (auto *w = qobject_cast<cues::WaitCue *>(cue)) waitExtra = w->durationSeconds();

    switch (cue->continueMode()) {
    case cues::ContinueMode::DoNotContinue: break;
    case cues::ContinueMode::AutoFollow:    scheduleContinue(cue, waitExtra); break;
    case cues::ContinueMode::AutoContinue:  scheduleContinue(cue, waitExtra + cue->postWait()); break;
    }
}

void GoEngine::scheduleContinue(cues::Cue *cue, double delaySeconds)
{
    auto *next = nextCueAfter(cue);
    if (!next) return;
    QPointer<cues::Cue> guard(next);
    if (delaySeconds <= 0.0) {
        // Zero-delay continue still goes through the event loop rather
        // than calling doFire() inline. A list of all-AutoFollow,
        // zero-wait cues would otherwise recurse
        // doFire→scheduleContinue→doFire for the whole list on one
        // stack frame (stack growth on long lists), and a cue that
        // GOTOs backward into such a chain would infinite-loop
        // synchronously and hang the UI. singleShot(0) lets each cue
        // fire on a fresh event-loop turn, bounding stack depth and
        // keeping the app responsive.
        QTimer::singleShot(0, this, [this, guard] {
            if (guard) fire(guard);
        });
        return;
    }
    auto *t = new QTimer(this);
    t->setSingleShot(true);
    m_pending.append(t);
    connect(t, &QTimer::timeout, this, [this, t, guard] {
        m_pending.removeAll(t);
        t->deleteLater();
        if (guard) fire(guard);
    });
    t->start(static_cast<int>(delaySeconds * 1000.0));
}

void GoEngine::cancelAll(double fadeOutSeconds)
{
    for (auto *t : m_pending) { t->stop(); t->deleteLater(); }
    m_pending.clear();
    if (m_audio)    m_audio->stopAll(fadeOutSeconds);
    if (m_lighting) m_lighting->blackout();
    if (m_video)    m_video->stopAll();
    m_trajectories.clear();
    if (m_trajectoryTimer) m_trajectoryTimer->stop();
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
