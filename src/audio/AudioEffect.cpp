#include "audio/AudioEffect.h"
#include "audio/effects/EqEffect.h"
#include "audio/effects/CompressorEffect.h"
#include "audio/effects/ReverbEffect.h"
#include "audio/effects/DelayEffect.h"

namespace quewi::audio {

std::unique_ptr<AudioEffect> AudioEffect::create(Type t, QObject *parent) {
    switch (t) {
    case Type::Eq:         return std::make_unique<EqEffect>(parent);
    case Type::Compressor: return std::make_unique<CompressorEffect>(parent);
    case Type::Reverb:     return std::make_unique<ReverbEffect>(parent);
    case Type::Delay:      return std::make_unique<DelayEffect>(parent);
    }
    return nullptr;
}

std::optional<AudioEffect::Type> AudioEffect::typeFromKey(const QString &key) {
    if (key == QLatin1String("eq"))         return Type::Eq;
    if (key == QLatin1String("compressor")) return Type::Compressor;
    if (key == QLatin1String("reverb"))     return Type::Reverb;
    if (key == QLatin1String("delay"))      return Type::Delay;
    return std::nullopt;
}

QString AudioEffect::typeKey(Type t) {
    switch (t) {
    case Type::Eq:         return QStringLiteral("eq");
    case Type::Compressor: return QStringLiteral("compressor");
    case Type::Reverb:     return QStringLiteral("reverb");
    case Type::Delay:      return QStringLiteral("delay");
    }
    return {};
}

} // namespace quewi::audio
