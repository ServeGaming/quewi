#include "audio/AudioEffect.h"
#include "audio/effects/EqEffect.h"
#include "audio/effects/CompressorEffect.h"
#include "audio/effects/ReverbEffect.h"
#include "audio/effects/DelayEffect.h"
#include "audio/effects/DistortionEffect.h"
#include "audio/effects/LoFiEffect.h"
#include "audio/effects/PitchShiftEffect.h"
#include "audio/effects/TremoloEffect.h"

namespace quewi::audio {

std::unique_ptr<AudioEffect> AudioEffect::create(Type t, QObject *parent) {
    switch (t) {
    case Type::Eq:         return std::make_unique<EqEffect>(parent);
    case Type::Compressor: return std::make_unique<CompressorEffect>(parent);
    case Type::Reverb:     return std::make_unique<ReverbEffect>(parent);
    case Type::Delay:      return std::make_unique<DelayEffect>(parent);
    case Type::Distortion: return std::make_unique<DistortionEffect>(parent);
    case Type::LoFi:       return std::make_unique<LoFiEffect>(parent);
    case Type::PitchShift: return std::make_unique<PitchShiftEffect>(parent);
    case Type::Tremolo:    return std::make_unique<TremoloEffect>(parent);
    }
    return nullptr;
}

QList<AudioEffect::Type> AudioEffect::allTypes() {
    return { Type::Eq, Type::Compressor, Type::Reverb, Type::Delay,
             Type::Distortion, Type::LoFi, Type::PitchShift, Type::Tremolo };
}

std::optional<AudioEffect::Type> AudioEffect::typeFromKey(const QString &key) {
    for (const Type t : allTypes())
        if (key == typeKey(t)) return t;
    return std::nullopt;
}

QString AudioEffect::typeKey(Type t) {
    switch (t) {
    case Type::Eq:         return QStringLiteral("eq");
    case Type::Compressor: return QStringLiteral("compressor");
    case Type::Reverb:     return QStringLiteral("reverb");
    case Type::Delay:      return QStringLiteral("delay");
    case Type::Distortion: return QStringLiteral("distortion");
    case Type::LoFi:       return QStringLiteral("lofi");
    case Type::PitchShift: return QStringLiteral("pitch");
    case Type::Tremolo:    return QStringLiteral("tremolo");
    }
    return {};
}

QJsonObject AudioEffect::toJson() const {
    QJsonObject o;
    o[QStringLiteral("type")]    = typeKey(type());
    o[QStringLiteral("enabled")] = isEnabled();
    QJsonObject params;
    for (const QString &pid : parameterIds())
        params[pid] = double(parameterValue(pid));
    o[QStringLiteral("params")] = params;
    return o;
}

std::unique_ptr<AudioEffect> AudioEffect::fromJson(const QJsonObject &o) {
    const auto typeOpt = typeFromKey(o.value(QStringLiteral("type")).toString());
    if (!typeOpt) return nullptr;
    auto fx = create(*typeOpt, nullptr);
    if (!fx) return nullptr;
    fx->setEnabled(o.value(QStringLiteral("enabled")).toBool(true));
    const QStringList known = fx->parameterIds();
    const QJsonObject params = o.value(QStringLiteral("params")).toObject();
    for (auto it = params.begin(); it != params.end(); ++it)
        if (known.contains(it.key()))
            fx->setParameterValue(it.key(), float(it.value().toDouble()));
    return fx;
}

} // namespace quewi::audio
