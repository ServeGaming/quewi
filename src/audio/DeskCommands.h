#pragma once

#include "audio/LightTrigger.h"
#include "core/LightingDesk.h"

#include <QByteArray>
#include <QString>
#include <QVariantList>

#include <vector>

namespace quewi::audio {

// Turns a simple-mode trigger action ("GO", "bump sub 3") into exactly what
// the show's lighting desk understands. Pure, so every desk's wire format is
// unit-tested; GoEngine does the sending.
//
// ETC Eos family (Eos, Ion, Element, Nomad) — OSC over UDP, per ETC's Show
// Control guide and what HeliOSC drives an Ion with:
//   keys      /eos/key/<name> 1.0, then 0.0 50 ms later (press + release)
//   GO        key go_0 · Stop: key stop · Back: key back
//   cue       /eos/cue/<list>/<cue>/fire
//   sub       /eos/sub/<n> <0..1> · bump: /eos/sub/<n>/fire 1.0 … 0.0
//   fader     /eos/fader/<bank>/config/<page>/<count>, then
//             /eos/fader/<bank>/<n> <0..1> · bump: …/<n>/fire 1.0 … 0.0
//   macro     /eos/macro/<n>/fire 1.0 … 0.0
//   command   /eos/newcmd "<text> Enter"
//   GO list   the GO button of the fader the list is on: config, then
//             /eos/fader/<bank>/<n>/fire 1.0 … 0.0 (Eos has no "GO list N")
// grandMA3 — its command line over OSC: [/<prefix>]/cmd "<command>"
//   Go+ · Go- · Go+ Sequence <n> · Goto Cue <n> Sequence <list> · any command
// grandMA2 / MSC — MIDI Show Control: GO (next, or cue n list m), STOP,
//   FIRE (macro n).
struct DeskSend {
    enum class Type { Osc, Msc };
    Type         type = Type::Osc;
    int          delayMs = 0;     // after the first send (bump releases)
    QString      address;         // OSC
    QVariantList args;            // OSC: float / int / QString
    int          mscCommand = 0x01;
    QByteArray   mscPayload;
};

// Can this desk do that? (The editor only offers what it can.)
bool deskSupports(core::LightingDesk::Type type, TriggerAction::DeskDo what);

// The sends for `action` (Kind::Desk) on `desk`. Empty + *error set when it
// can't be done (unsupported on this desk, or a number is missing).
std::vector<DeskSend> deskSends(const core::LightingDesk &desk, const TriggerAction &action,
                                QString *error = nullptr);

} // namespace quewi::audio
