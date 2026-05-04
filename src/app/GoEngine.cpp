#include "GoEngine.h"

#include "audio/AudioCue.h"
#include "audio/AudioEngine.h"
#include "core/CueList.h"
#include "core/Workspace.h"
#include "cues/Cue.h"
#include "cues/FadeCue.h"
#include "cues/GroupCue.h"
#include "cues/TargetingCue.h"
#include "cues/WaitCue.h"
#include "lighting/LightCue.h"
#include "lighting/LightingEngine.h"
#include "osc/OscCue.h"
#include "osc/OscEngine.h"
#include "video/VideoCue.h"
#include "video/VideoEngine.h"

#include <QRandomGenerator>
#include <QTimer>

namespace quewi {

GoEngine::GoEngine(QObject *parent) : QObject(parent) {}
GoEngine::~GoEngine() { cancelAll(0.0); }

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
    for (int row = 0; row < list->cueCount() - 1; ++row) {
        if (list->cueAt(row) == cue) return list->cueAt(row + 1);
    }
    return nullptr;
}

void GoEngine::fire(cues::Cue *cue)
{
    if (!cue || !cue->isArmed()) return;

    const double preWait = cue->preWait();
    if (preWait > 0.0) {
        auto *t = new QTimer(this);
        t->setSingleShot(true);
        m_pending.append(t);
        QPointer<cues::Cue> guard(cue);
        connect(t, &QTimer::timeout, this, [this, t, guard] {
            m_pending.removeAll(t);
            t->deleteLater();
            if (guard) doFire(guard);
        });
        t->start(static_cast<int>(preWait * 1000.0));
    } else {
        doFire(cue);
    }
}

void GoEngine::doFire(cues::Cue *cue)
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
            } else if (file->state() != audio::AudioFile::State::Loaded) {
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
                p.outputDeviceId = audioCue->outputDeviceId();
                const auto vid = m_audio->fire(file, p);
                audioCue->setCurrentVoiceId(vid);
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
            m_video->fire(p);
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
            status(tr("Start → %1").arg(
                target->name().isEmpty() ? target->typeName() : target->name()));
            fire(target);
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
        if (guard) doFire(guard);
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
}

} // namespace quewi
