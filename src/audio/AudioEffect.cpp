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

} // namespace quewi::audio
