#include "audio/AudioEngine.h"

namespace quewi::audio {

AudioEngine::AudioEngine(QObject *parent) : QObject(parent) {}
AudioEngine::~AudioEngine() = default;

} // namespace quewi::audio
