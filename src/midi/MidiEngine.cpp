#include "midi/MidiEngine.h"

namespace quewi::midi {

MidiEngine::MidiEngine(QObject *parent) : QObject(parent) {}
MidiEngine::~MidiEngine() = default;

} // namespace quewi::midi
