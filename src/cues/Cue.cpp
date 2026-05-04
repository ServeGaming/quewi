#include "cues/Cue.h"

namespace quewi::cues {

Cue::Cue(QObject *parent) : QObject(parent), m_id(QUuid::createUuid()) {}
Cue::~Cue() = default;

} // namespace quewi::cues
