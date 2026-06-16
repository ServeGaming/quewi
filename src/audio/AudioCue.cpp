#include "audio/AudioCue.h"

#include "audio/AudioEffect.h"

#include <QJsonArray>
#include <QJsonObject>

#include <optional>

namespace quewi::audio {

AudioCue::AudioCue(QObject *parent) : cues::Cue(parent) {}
AudioCue::~AudioCue() = default;

QVariant AudioCue::field(const QString &key) const
{
    if (key == QLatin1String("filePath"))       return m_filePath;
    if (key == QLatin1String("gainDb"))         return m_gainDb;
    if (key == QLatin1String("fadeInSeconds"))  return m_fadeInSeconds;
    if (key == QLatin1String("fadeOutSeconds")) return m_fadeOutSeconds;
    if (key == QLatin1String("trimInSeconds"))  return m_trimInSeconds;
    if (key == QLatin1String("trimOutSeconds")) return m_trimOutSeconds;
    if (key == QLatin1String("pan"))            return m_pan;
    if (key == QLatin1String("loop"))           return m_loop;
    if (key == QLatin1String("outputDeviceId")) return m_outputDeviceId;
    if (key == QLatin1String("objectAudio"))    return m_objAudio;
    if (key == QLatin1String("speakerPatchId")) return m_speakerPatchId;
    if (key == QLatin1String("objAzimuth"))     return m_objAzimuth;
    if (key == QLatin1String("objElevation"))   return m_objElevation;
    if (key == QLatin1String("objSpread"))      return m_objSpread;
    return cues::Cue::field(key);
}

void AudioCue::setField(const QString &key, const QVariant &value)
{
    auto setDouble = [&](double &target) {
        if (qFuzzyCompare(target + 1.0, value.toDouble() + 1.0)) return false;
        target = value.toDouble();
        emitChanged();
        return true;
    };
    if (key == QLatin1String("filePath")) {
        const auto v = value.toString();
        if (m_filePath == v) return;
        m_filePath = v;
        m_file.reset();
        emitChanged();
        return;
    }
    if (key == QLatin1String("gainDb"))         { setDouble(m_gainDb);         return; }
    if (key == QLatin1String("fadeInSeconds"))  { setDouble(m_fadeInSeconds);  return; }
    if (key == QLatin1String("fadeOutSeconds")) { setDouble(m_fadeOutSeconds); return; }
    if (key == QLatin1String("trimInSeconds"))  { setDouble(m_trimInSeconds);  return; }
    if (key == QLatin1String("trimOutSeconds")) { setDouble(m_trimOutSeconds); return; }
    if (key == QLatin1String("pan"))            { setDouble(m_pan);            return; }
    if (key == QLatin1String("loop")) {
        if (m_loop == value.toBool()) return;
        m_loop = value.toBool();
        emitChanged();
        return;
    }
    if (key == QLatin1String("outputDeviceId")) {
        const auto v = value.toByteArray();
        if (m_outputDeviceId == v) return;
        m_outputDeviceId = v;
        emitChanged();
        return;
    }
    if (key == QLatin1String("objectAudio")) {
        if (m_objAudio == value.toBool()) return;
        m_objAudio = value.toBool();
        emitChanged();
        return;
    }
    if (key == QLatin1String("speakerPatchId")) {
        const auto v = value.toUuid();
        if (m_speakerPatchId == v) return;
        m_speakerPatchId = v;
        emitChanged();
        return;
    }
    if (key == QLatin1String("objAzimuth"))   { setDouble(m_objAzimuth);   return; }
    if (key == QLatin1String("objElevation")) { setDouble(m_objElevation); return; }
    if (key == QLatin1String("objSpread"))    { setDouble(m_objSpread);    return; }
    cues::Cue::setField(key, value);
}

QJsonObject AudioCue::toPayload() const
{
    auto o = cues::Cue::toPayload();
    o.insert(QStringLiteral("filePath"),       m_filePath);
    // Total source-file duration when known. Derived from
    // file metadata, NOT a user-settable field — fromPayload
    // ignores it. OSC remotes use it to scale a transport
    // progress bar; without it `elapsed` from the playback
    // notification is meaningless on its own. Absent when the
    // file hasn't decoded yet or isn't decodable.
    if (m_file && m_file->durationSeconds() > 0.0) {
        o.insert(QStringLiteral("durationSeconds"),
                 m_file->durationSeconds());
    }
    o.insert(QStringLiteral("gainDb"),         m_gainDb);
    o.insert(QStringLiteral("fadeInSeconds"),  m_fadeInSeconds);
    o.insert(QStringLiteral("fadeOutSeconds"), m_fadeOutSeconds);
    o.insert(QStringLiteral("trimInSeconds"),  m_trimInSeconds);
    o.insert(QStringLiteral("trimOutSeconds"), m_trimOutSeconds);
    o.insert(QStringLiteral("pan"),            m_pan);
    o.insert(QStringLiteral("loop"),           m_loop);
    o.insert(QStringLiteral("outputDeviceId"), QString::fromLatin1(m_outputDeviceId));
    if (!m_outputGainsDb.isEmpty()) {
        QJsonArray g;
        for (double v : m_outputGainsDb) g.append(v);
        o.insert(QStringLiteral("outputGainsDb"), g);
    }
    if (m_objAudio) {
        o.insert(QStringLiteral("objectAudio"),    true);
        o.insert(QStringLiteral("speakerPatchId"), m_speakerPatchId.toString(QUuid::WithoutBraces));
        o.insert(QStringLiteral("objAzimuth"),     m_objAzimuth);
        o.insert(QStringLiteral("objElevation"),   m_objElevation);
        o.insert(QStringLiteral("objSpread"),      m_objSpread);
        if (m_trajectory.keyframeCount() > 0) {
            o.insert(QStringLiteral("trajectory"), m_trajectory.toJson());
        }
    }
    // Editable audio-editor session — only written once the operator has
    // actually edited in the editor, to keep untouched cues' payloads lean.
    if (!m_editorModelJson.isEmpty()) {
        o.insert(QStringLiteral("editorModel"), m_editorModelJson);
    }
    return o;
}

void AudioCue::fromPayload(const QJsonObject &payload)
{
    cues::Cue::fromPayload(payload);
    m_filePath       = payload.value(QStringLiteral("filePath")).toString();
    m_gainDb         = payload.value(QStringLiteral("gainDb")).toDouble();
    m_fadeInSeconds  = payload.value(QStringLiteral("fadeInSeconds")).toDouble();
    m_fadeOutSeconds = payload.value(QStringLiteral("fadeOutSeconds")).toDouble();
    m_trimInSeconds  = payload.value(QStringLiteral("trimInSeconds")).toDouble();
    m_trimOutSeconds = payload.value(QStringLiteral("trimOutSeconds")).toDouble();
    m_pan            = payload.value(QStringLiteral("pan")).toDouble();
    m_loop           = payload.value(QStringLiteral("loop")).toBool();
    m_outputDeviceId = payload.value(QStringLiteral("outputDeviceId")).toString().toLatin1();
    m_objAudio       = payload.value(QStringLiteral("objectAudio")).toBool(false);
    m_speakerPatchId = QUuid(payload.value(QStringLiteral("speakerPatchId")).toString());
    m_objAzimuth     = payload.value(QStringLiteral("objAzimuth")).toDouble(0.0);
    m_objElevation   = payload.value(QStringLiteral("objElevation")).toDouble(0.0);
    m_objSpread      = payload.value(QStringLiteral("objSpread")).toDouble(0.0);
    m_outputGainsDb.clear();
    if (payload.contains(QStringLiteral("outputGainsDb"))) {
        const auto arr = payload.value(QStringLiteral("outputGainsDb")).toArray();
        for (const auto &v : arr) m_outputGainsDb.append(v.toDouble(0.0));
    }
    if (payload.contains(QStringLiteral("trajectory"))) {
        m_trajectory = AudioTrajectory::fromJson(
            payload.value(QStringLiteral("trajectory")).toObject());
    } else {
        m_trajectory = AudioTrajectory{};
    }
    m_editorModelJson = payload.value(QStringLiteral("editorModel")).toObject();
    m_file.reset();
}

std::vector<std::shared_ptr<AudioEffect>> AudioCue::buildEffectChain() const
{
    std::vector<std::shared_ptr<AudioEffect>> chain;

    // The cue's rack == track 0's effects in the saved editor session.
    // Mirrors AudioEditorTrack::fromJson's effects loop so live playback
    // and the editor stay in lockstep.
    const auto tracks = m_editorModelJson.value(QStringLiteral("tracks")).toArray();
    if (tracks.isEmpty()) return chain;
    const auto fxArr = tracks.at(0).toObject()
                           .value(QStringLiteral("effects")).toArray();
    for (const auto &v : fxArr) {
        const QJsonObject fxo = v.toObject();
        const auto typeOpt = AudioEffect::typeFromKey(fxo.value(QStringLiteral("type")).toString());
        if (!typeOpt) continue;
        auto fx = AudioEffect::create(*typeOpt, nullptr);
        if (!fx) continue;
        fx->setEnabled(fxo.value(QStringLiteral("enabled")).toBool(true));
        const QJsonObject params = fxo.value(QStringLiteral("params")).toObject();
        for (auto it = params.begin(); it != params.end(); ++it)
            fx->setParameterValue(it.key(), float(it.value().toDouble()));
        chain.push_back(std::shared_ptr<AudioEffect>(std::move(fx)));
    }
    return chain;
}

bool AudioCue::setEffectParam(const QString &typeKey, const QString &paramId,
                              float value)
{
    const auto typeOpt = AudioEffect::typeFromKey(typeKey);
    if (!typeOpt) return false;

    // Validate the param against the effect's real parameter list (and allow
    // the special "enabled" bypass flag) so a remote can't silently no-op.
    const bool isEnabled = (paramId == QLatin1String("enabled"));
    if (!isEnabled) {
        auto probe = AudioEffect::create(*typeOpt, nullptr);
        if (!probe || !probe->parameterIds().contains(paramId)) return false;
    }

    // Mutate a copy of the editor model, synthesising tracks[0] for a cue that
    // was never opened in the editor (so the structure buildEffectChain reads
    // is created from scratch on first remote edit).
    QJsonObject model = m_editorModelJson;
    QJsonArray tracks = model.value(QStringLiteral("tracks")).toArray();
    if (tracks.isEmpty()) tracks.append(QJsonObject{});
    QJsonObject track0 = tracks.at(0).toObject();
    QJsonArray fxArr = track0.value(QStringLiteral("effects")).toArray();

    int idx = -1;
    for (int i = 0; i < fxArr.size(); ++i)
        if (fxArr.at(i).toObject().value(QStringLiteral("type")).toString() == typeKey)
            { idx = i; break; }

    QJsonObject fxo = (idx >= 0) ? fxArr.at(idx).toObject() : QJsonObject{};
    if (idx < 0) {
        // Create the effect with its default params so the rack is complete.
        fxo.insert(QStringLiteral("type"), typeKey);
        fxo.insert(QStringLiteral("enabled"), true);
        QJsonObject params;
        if (auto fx = AudioEffect::create(*typeOpt, nullptr))
            for (const QString &pid : fx->parameterIds())
                params.insert(pid, double(fx->parameterDefault(pid)));
        fxo.insert(QStringLiteral("params"), params);
    }

    if (isEnabled) {
        fxo.insert(QStringLiteral("enabled"), value != 0.f);
    } else {
        QJsonObject params = fxo.value(QStringLiteral("params")).toObject();
        params.insert(paramId, double(value));
        fxo.insert(QStringLiteral("params"), params);
    }

    if (idx >= 0) fxArr.replace(idx, fxo); else fxArr.append(fxo);
    track0.insert(QStringLiteral("effects"), fxArr);
    tracks.replace(0, track0);
    model.insert(QStringLiteral("tracks"), tracks);
    setEditorModelJson(model);   // emits changed()
    return true;
}

QJsonObject AudioCue::effectChainSummary() const
{
    QJsonArray effects;
    for (const auto &fx : buildEffectChain()) {
        if (!fx) continue;
        QJsonObject e;
        e.insert(QStringLiteral("type"),    AudioEffect::typeKey(fx->type()));
        e.insert(QStringLiteral("name"),    fx->name());
        e.insert(QStringLiteral("enabled"), fx->isEnabled());
        QJsonArray params;
        for (const QString &pid : fx->parameterIds()) {
            const auto range = fx->parameterRange(pid);
            QJsonObject p;
            p.insert(QStringLiteral("id"),    pid);
            p.insert(QStringLiteral("label"), fx->parameterLabel(pid));
            p.insert(QStringLiteral("value"), double(fx->parameterValue(pid)));
            p.insert(QStringLiteral("min"),   double(range.first));
            p.insert(QStringLiteral("max"),   double(range.second));
            params.append(p);
        }
        e.insert(QStringLiteral("params"), params);
        effects.append(e);
    }
    QJsonObject o;
    o.insert(QStringLiteral("cue"),     number());
    o.insert(QStringLiteral("effects"), effects);
    return o;
}

void AudioCue::prepare()
{
    if (m_filePath.isEmpty()) return;

    if (!m_file) {
        m_file = std::make_shared<AudioFile>();
        m_file->load(m_filePath);
        // Bubble peak/state changes up so the inspector can repaint.
        connect(m_file.get(), &AudioFile::stateChanged,
                this, [this](AudioFile::State) { emitChanged(); });
    } else if (m_file->path() != m_filePath
               && m_file->state() != AudioFile::State::Loading) {
        m_file->load(m_filePath);
    }
}

} // namespace quewi::audio
