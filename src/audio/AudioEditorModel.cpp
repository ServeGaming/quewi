#include "audio/AudioEditorModel.h"

#include <QUndoCommand>
#include <QJsonArray>
#include <algorithm>
#include <cassert>

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
    return o;
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
    auto &r = m_tracks[ti]->regions()[ri];
    if (splitAtTimeline <= r.timelinePosSamples || splitAtTimeline >= r.timelineEndSamples()) return;

    qint64 offsetInSrc = r.srcInSamples + (splitAtTimeline - r.timelinePosSamples);
    AudioRegion right = r;
    right.id = QUuid::createUuid();
    right.timelinePosSamples = splitAtTimeline;
    right.srcInSamples = offsetInSrc;
    r.srcOutSamples = offsetInSrc;

    m_tracks[ti]->regions().push_back(std::move(right));
    std::sort(m_tracks[ti]->regions().begin(), m_tracks[ti]->regions().end(),
        [](auto &a, auto &b){ return a.timelinePosSamples < b.timelinePosSamples; });
    setDirty();
    emit tracksChanged();
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
    // Deserialization of regions with file reloading is left as a no-op for now;
    // the editor always re-initialises from the cue's file path when it opens.
    m_dirty = false;
}

} // namespace quewi::audio
