#include "audio/AudioEditorModel.h"
#include "audio/AudioEffect.h"

#include <QJsonObject>
#include <QUndoCommand>
#include <QJsonArray>
#include <QHash>
#include <algorithm>
#include <cassert>
#include <optional>

namespace {
// Stable string keys for AudioEffect::Type so saved JSON survives any
// future re-ordering of the enum.
QString effectTypeKey(quewi::audio::AudioEffect::Type t) {
    using T = quewi::audio::AudioEffect::Type;
    switch (t) {
    case T::Eq:         return QStringLiteral("eq");
    case T::Compressor: return QStringLiteral("compressor");
    case T::Reverb:     return QStringLiteral("reverb");
    case T::Delay:      return QStringLiteral("delay");
    }
    return QStringLiteral("eq");
}
std::optional<quewi::audio::AudioEffect::Type> effectTypeFromKey(const QString &k) {
    using T = quewi::audio::AudioEffect::Type;
    if (k == QLatin1String("eq"))         return T::Eq;
    if (k == QLatin1String("compressor")) return T::Compressor;
    if (k == QLatin1String("reverb"))     return T::Reverb;
    if (k == QLatin1String("delay"))      return T::Delay;
    return std::nullopt;
}
} // namespace

namespace quewi::audio {

// ── AudioRegion ───────────────────────────────────────────────────────────────

qint64 AudioRegion::durationSamples() const {
    if (!sourceFile) return 0;
    qint64 out = (srcOutSamples < 0) ? sourceFile->frameCount() : srcOutSamples;
    return std::max(qint64(0), out - srcInSamples);
}

qint64 AudioRegion::timelineEndSamples() const {
    return timelinePosSamples + durationSamples();
}

QJsonObject AudioRegion::toJson() const {
    QJsonObject o;
    o[QStringLiteral("id")]   = id.toString();
    o[QStringLiteral("name")] = name;
    o[QStringLiteral("path")] = sourceFile ? sourceFile->path() : QString();
    o[QStringLiteral("tPos")]     = timelinePosSamples;
    o[QStringLiteral("srcIn")]    = srcInSamples;
    o[QStringLiteral("srcOut")]   = srcOutSamples;
    o[QStringLiteral("gainDb")]   = double(gainDb);
    o[QStringLiteral("fiDur")]    = fadeIn.durationSamples;
    o[QStringLiteral("fiType")]   = int(fadeIn.type);
    o[QStringLiteral("foDur")]    = fadeOut.durationSamples;
    o[QStringLiteral("foType")]   = int(fadeOut.type);
    o[QStringLiteral("color")]    = color.name();
    return o;
}

AudioRegion AudioRegion::fromJson(const QJsonObject &o, std::shared_ptr<AudioFile> file) {
    AudioRegion r;
    r.id          = QUuid::fromString(o[QStringLiteral("id")].toString());
    r.name        = o[QStringLiteral("name")].toString();
    r.sourceFile  = file;
    r.timelinePosSamples = qint64(o[QStringLiteral("tPos")].toDouble());
    r.srcInSamples       = qint64(o[QStringLiteral("srcIn")].toDouble());
    r.srcOutSamples      = qint64(o[QStringLiteral("srcOut")].toDouble());
    r.gainDb       = float(o[QStringLiteral("gainDb")].toDouble());
    r.fadeIn.durationSamples  = qint64(o[QStringLiteral("fiDur")].toDouble());
    r.fadeIn.type  = FadeCurve::Type(o[QStringLiteral("fiType")].toInt());
    r.fadeOut.durationSamples = qint64(o[QStringLiteral("foDur")].toDouble());
    r.fadeOut.type = FadeCurve::Type(o[QStringLiteral("foType")].toInt());
    r.color        = QColor(o[QStringLiteral("color")].toString());
    return r;
}

// ── AudioEditorTrack ──────────────────────────────────────────────────────────

AudioEditorTrack::AudioEditorTrack(QObject *parent)
    : QObject(parent), m_id(QUuid::createUuid()), m_name(QStringLiteral("Track"))
{}

AudioEditorTrack::~AudioEditorTrack() = default;

AudioEffect *AudioEditorTrack::addEffect(AudioEffect::Type t) {
    auto fx = AudioEffect::create(t, nullptr);
    auto *ptr = fx.get();
    m_effects.push_back(std::move(fx));
    emit changed();
    return ptr;
}

void AudioEditorTrack::removeEffect(int idx) {
    if (idx < 0 || idx >= int(m_effects.size())) return;
    m_effects.erase(m_effects.begin() + idx);
    emit changed();
}

void AudioEditorTrack::moveEffect(int from, int to) {
    if (from < 0 || to < 0 || from >= int(m_effects.size()) || to >= int(m_effects.size())) return;
    auto fx = std::move(m_effects[from]);
    m_effects.erase(m_effects.begin() + from);
    m_effects.insert(m_effects.begin() + to, std::move(fx));
    emit changed();
}

QJsonObject AudioEditorTrack::toJson() const {
    QJsonObject o;
    o[QStringLiteral("id")]     = m_id.toString();
    o[QStringLiteral("name")]   = m_name;
    o[QStringLiteral("volume")] = double(m_volume);
    o[QStringLiteral("muted")]  = m_muted;
    o[QStringLiteral("soloed")] = m_soloed;
    QJsonArray regions;
    for (auto &r : m_regions) regions.append(r.toJson());
    o[QStringLiteral("regions")] = regions;

    // Effects chain — stable type keys + parameter map. Previously we
    // dropped this on the floor, so any FX the user dialled in vanished
    // on save/reload (one of the recurring "the rack does nothing"
    // reports).
    QJsonArray fxArr;
    for (const auto &fx : m_effects) {
        if (!fx) continue;
        QJsonObject fxo;
        fxo[QStringLiteral("type")]    = effectTypeKey(fx->type());
        fxo[QStringLiteral("enabled")] = fx->isEnabled();
        QJsonObject params;
        for (const QString &pid : fx->parameterIds())
            params[pid] = double(fx->parameterValue(pid));
        fxo[QStringLiteral("params")] = params;
        fxArr.append(fxo);
    }
    o[QStringLiteral("effects")] = fxArr;
    return o;
}

void AudioEditorTrack::fromJson(const QJsonObject &o) {
    m_id     = QUuid(o.value(QStringLiteral("id")).toString());
    m_name   = o.value(QStringLiteral("name")).toString();
    m_volume = float(o.value(QStringLiteral("volume")).toDouble(1.0));
    m_muted  = o.value(QStringLiteral("muted")).toBool(false);
    m_soloed = o.value(QStringLiteral("soloed")).toBool(false);

    // Regions: not restored here; AudioEditorWindow re-initialises
    // them from the cue's audio file when the editor opens. Effects
    // ARE restored so a user re-opening the editor finds their chain
    // intact rather than blank.
    m_effects.clear();
    const auto fxArr = o.value(QStringLiteral("effects")).toArray();
    for (const auto &v : fxArr) {
        const QJsonObject fxo = v.toObject();
        const auto typeOpt = effectTypeFromKey(
            fxo.value(QStringLiteral("type")).toString());
        if (!typeOpt) continue;
        auto fx = AudioEffect::create(*typeOpt, nullptr);
        if (!fx) continue;
        fx->setEnabled(fxo.value(QStringLiteral("enabled")).toBool(true));
        const QJsonObject params = fxo.value(QStringLiteral("params")).toObject();
        for (auto it = params.begin(); it != params.end(); ++it)
            fx->setParameterValue(it.key(), float(it.value().toDouble()));
        m_effects.push_back(std::move(fx));
    }
    emit changed();
}

// ── Undo commands ─────────────────────────────────────────────────────────────

class MoveRegionCmd : public QUndoCommand {
    AudioEditorModel *m; QUuid id; qint64 oldPos, newPos;
public:
    MoveRegionCmd(AudioEditorModel *m, QUuid id, qint64 o, qint64 n)
        : m(m), id(id), oldPos(o), newPos(n) { setText(QStringLiteral("Move Region")); }
    void redo() override { apply(newPos); }
    void undo() override { apply(oldPos); }
private:
    void apply(qint64 pos) {
        auto [ti, ri] = m->findRegion(id);
        if (ti < 0) return;
        m->m_tracks[ti]->regions()[ri].timelinePosSamples = pos;
        emit m->regionMoved(id);
        m->setDirty();
    }
};

class TrimRegionCmd : public QUndoCommand {
    AudioEditorModel *m; QUuid id; bool left; qint64 oldV, newV;
public:
    TrimRegionCmd(AudioEditorModel *m, QUuid id, bool left, qint64 o, qint64 n)
        : m(m), id(id), left(left), oldV(o), newV(n) { setText(QStringLiteral("Trim Region")); }
    void redo() override { apply(newV); }
    void undo() override { apply(oldV); }
private:
    void apply(qint64 v) {
        auto [ti, ri] = m->findRegion(id);
        if (ti < 0) return;
        auto &r = m->m_tracks[ti]->regions()[ri];
        if (left) {
            qint64 delta = v - r.srcInSamples;
            r.srcInSamples      = v;
            r.timelinePosSamples += delta;
        } else {
            r.srcOutSamples = v;
        }
        emit m->regionMoved(id);
        m->setDirty();
    }
};

class SplitRegionCmd : public QUndoCommand {
    AudioEditorModel *m; QUuid id; qint64 origOut; AudioRegion rightSaved;
public:
    // id        – the original (left-hand) region being split
    // origOut   – its srcOutSamples before the split, for restoring on undo
    // right     – the fully-formed right-hand region the split produces; it
    //             carries a fixed id so redo re-creates the same region.
    SplitRegionCmd(AudioEditorModel *m, QUuid id, qint64 origOut, AudioRegion right)
        : m(m), id(id), origOut(origOut), rightSaved(std::move(right)) { setText(QStringLiteral("Split Region")); }
    void redo() override {
        auto [ti, ri] = m->findRegion(id);
        if (ti < 0) return;
        // Left region now ends where the right one begins.
        m->m_tracks[ti]->regions()[ri].srcOutSamples = rightSaved.srcInSamples;
        m->m_tracks[ti]->regions().push_back(rightSaved);
        std::sort(m->m_tracks[ti]->regions().begin(), m->m_tracks[ti]->regions().end(),
            [](auto &a, auto &b){ return a.timelinePosSamples < b.timelinePosSamples; });
        emit m->tracksChanged();
        m->setDirty();
    }
    void undo() override {
        // Drop the right-hand region, then heal the left one back to full length.
        auto [tiR, riR] = m->findRegion(rightSaved.id);
        if (tiR >= 0) {
            auto &regs = m->m_tracks[tiR]->regions();
            regs.erase(regs.begin() + riR);
        }
        auto [ti, ri] = m->findRegion(id);
        if (ti >= 0) m->m_tracks[ti]->regions()[ri].srcOutSamples = origOut;
        emit m->tracksChanged();
        m->setDirty();
    }
};

class RemoveRegionCmd : public QUndoCommand {
    AudioEditorModel *m; int ti; AudioRegion saved;
public:
    RemoveRegionCmd(AudioEditorModel *m, int ti, AudioRegion r)
        : m(m), ti(ti), saved(std::move(r)) { setText(QStringLiteral("Remove Region")); }
    void redo() override {
        auto &regs = m->m_tracks[ti]->regions();
        auto it = std::find_if(regs.begin(), regs.end(), [&](auto &r){ return r.id == saved.id; });
        if (it != regs.end()) { regs.erase(it); emit m->tracksChanged(); m->setDirty(); }
    }
    void undo() override {
        m->m_tracks[ti]->regions().push_back(saved);
        std::sort(m->m_tracks[ti]->regions().begin(), m->m_tracks[ti]->regions().end(),
            [](auto &a, auto &b){ return a.timelinePosSamples < b.timelinePosSamples; });
        emit m->tracksChanged();
        m->setDirty();
    }
};

class SetRegionGainCmd : public QUndoCommand {
    AudioEditorModel *m; QUuid id; float oldG, newG;
public:
    SetRegionGainCmd(AudioEditorModel *m, QUuid id, float o, float n)
        : m(m), id(id), oldG(o), newG(n) { setText(QStringLiteral("Set Region Gain")); }
    void redo() override { apply(newG); }
    void undo() override { apply(oldG); }
private:
    void apply(float g) {
        auto [ti, ri] = m->findRegion(id);
        if (ti < 0) return;
        m->m_tracks[ti]->regions()[ri].gainDb = g;
        m->setDirty();
    }
};

// ── AudioEditorModel ──────────────────────────────────────────────────────────

AudioEditorModel::AudioEditorModel(QObject *parent) : QObject(parent) {}
AudioEditorModel::~AudioEditorModel() = default;

void AudioEditorModel::initFromFile(const QString &path, int sampleRate) {
    m_sampleRate = sampleRate;
    m_tracks.clear();
    m_undoStack.clear();

    auto *track = addTrack(QStringLiteral("Track 1"));
    if (!path.isEmpty()) {
        auto file = std::make_shared<AudioFile>();
        // The decode is async; when it advances (Loaded → peaks ready) the
        // editor must repaint or the timeline can sit showing a flat line
        // until some unrelated event triggers a redraw. tracksChanged is
        // what the timeline canvas already listens to.
        connect(file.get(), &AudioFile::stateChanged, this,
                [this](AudioFile::State){ emit tracksChanged(); });
        file->load(path);

        AudioRegion r;
        r.id         = QUuid::createUuid();
        r.name       = QFileInfo(path).baseName();
        r.sourceFile = file;
        track->regions().push_back(std::move(r));
    }
    m_dirty = false;
    emit tracksChanged();
}

void AudioEditorModel::setDirty() {
    if (!m_dirty) { m_dirty = true; emit dirtyChanged(true); }
}

void AudioEditorModel::setSampleRate(int sr) {
    if (sr <= 0 || sr == m_sampleRate) return;
    m_sampleRate = sr;
    emit tracksChanged(); // trigger UI refresh of ruler / scaling
}

AudioEditorTrack *AudioEditorModel::track(int i) const {
    if (i < 0 || i >= int(m_tracks.size())) return nullptr;
    return m_tracks[i].get();
}

AudioEditorTrack *AudioEditorModel::addTrack(const QString &name) {
    auto t = std::make_unique<AudioEditorTrack>(nullptr);
    if (!name.isEmpty()) t->setName(name);
    connect(t.get(), &AudioEditorTrack::changed, this, &AudioEditorModel::tracksChanged);
    auto *ptr = t.get();
    m_tracks.push_back(std::move(t));
    emit tracksChanged();
    return ptr;
}

void AudioEditorModel::removeTrack(int idx) {
    if (idx < 0 || idx >= int(m_tracks.size())) return;
    m_tracks.erase(m_tracks.begin() + idx);
    setDirty();
    emit tracksChanged();
}

qint64 AudioEditorModel::totalDurationSamples() const {
    qint64 dur = 0;
    for (auto &t : m_tracks)
        for (auto &r : t->regions())
            dur = std::max(dur, r.timelineEndSamples());
    return dur;
}

std::pair<int,int> AudioEditorModel::findRegion(const QUuid &id) const {
    for (int ti = 0; ti < int(m_tracks.size()); ++ti)
        for (int ri = 0; ri < int(m_tracks[ti]->regions().size()); ++ri)
            if (m_tracks[ti]->regions()[ri].id == id)
                return {ti, ri};
    return {-1,-1};
}

void AudioEditorModel::moveRegion(QUuid id, qint64 newPos) {
    auto [ti, ri] = findRegion(id);
    if (ti < 0) return;
    qint64 oldPos = m_tracks[ti]->regions()[ri].timelinePosSamples;
    if (oldPos == newPos) return;
    m_undoStack.push(new MoveRegionCmd(this, id, oldPos, newPos));
}

void AudioEditorModel::trimRegion(QUuid id, bool leftEdge, qint64 newSample) {
    auto [ti, ri] = findRegion(id);
    if (ti < 0) return;
    auto &r = m_tracks[ti]->regions()[ri];
    qint64 old = leftEdge ? r.srcInSamples : r.srcOutSamples;
    m_undoStack.push(new TrimRegionCmd(this, id, leftEdge, old, newSample));
}

void AudioEditorModel::splitRegion(QUuid regionId, qint64 splitAtTimeline) {
    auto [ti, ri] = findRegion(regionId);
    if (ti < 0) return;
    const auto &r = m_tracks[ti]->regions()[ri];
    if (splitAtTimeline <= r.timelinePosSamples || splitAtTimeline >= r.timelineEndSamples()) return;

    qint64 offsetInSrc = r.srcInSamples + (splitAtTimeline - r.timelinePosSamples);
    qint64 origOut = r.srcOutSamples;
    AudioRegion right = r;                  // inherits gain/fades/color/srcOut
    right.id = QUuid::createUuid();
    right.timelinePosSamples = splitAtTimeline;
    right.srcInSamples = offsetInSrc;       // srcOutSamples (== origOut) kept from copy

    m_undoStack.push(new SplitRegionCmd(this, regionId, origOut, std::move(right)));
}

void AudioEditorModel::removeRegion(QUuid id) {
    auto [ti, ri] = findRegion(id);
    if (ti < 0) return;
    m_undoStack.push(new RemoveRegionCmd(this, ti, m_tracks[ti]->regions()[ri]));
}

void AudioEditorModel::setRegionGain(QUuid id, float gainDb) {
    auto [ti, ri] = findRegion(id);
    if (ti < 0) return;
    float old = m_tracks[ti]->regions()[ri].gainDb;
    m_undoStack.push(new SetRegionGainCmd(this, id, old, gainDb));
}

QJsonObject AudioEditorModel::toJson() const {
    QJsonObject o;
    o[QStringLiteral("sampleRate")] = m_sampleRate;
    QJsonArray tracks;
    for (auto &t : m_tracks) tracks.append(t->toJson());
    o[QStringLiteral("tracks")] = tracks;
    return o;
}

void AudioEditorModel::fromJson(const QJsonObject &o) {
    m_sampleRate = o[QStringLiteral("sampleRate")].toInt(48000);
    m_tracks.clear();
    m_undoStack.clear();   // stale commands would reference now-dead regions

    // Decode each distinct source file once and share it across every region
    // that references it (split halves, repeats). Decoding is async — each
    // file's stateChanged drives a repaint so peaks appear once it's ready.
    QHash<QString, std::shared_ptr<AudioFile>> fileCache;

    const QJsonArray tracks = o.value(QStringLiteral("tracks")).toArray();
    for (const auto &tv : tracks) {
        const QJsonObject to = tv.toObject();
        AudioEditorTrack *track = addTrack();
        track->fromJson(to);   // id / name / volume / mute / solo + effects chain

        // Regions are restored here (track->fromJson intentionally skips them
        // since it has no file-decoding context).
        const QJsonArray regs = to.value(QStringLiteral("regions")).toArray();
        for (const auto &rv : regs) {
            const QJsonObject ro = rv.toObject();
            const QString path = ro.value(QStringLiteral("path")).toString();
            std::shared_ptr<AudioFile> file;
            if (!path.isEmpty()) {
                auto it = fileCache.find(path);
                if (it != fileCache.end()) {
                    file = it.value();
                } else {
                    file = std::make_shared<AudioFile>();
                    connect(file.get(), &AudioFile::stateChanged, this,
                            [this](AudioFile::State){ emit tracksChanged(); });
                    file->load(path);
                    fileCache.insert(path, file);
                }
            }
            track->regions().push_back(AudioRegion::fromJson(ro, file));
        }
    }

    m_dirty = false;
    emit tracksChanged();   // rebuild scrollbars + repaint the timeline
}

} // namespace quewi::audio
