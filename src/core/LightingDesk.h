#pragma once

#include <QJsonObject>
#include <QString>

namespace quewi::core {

// The lighting console lighting triggers talk to — set once in Preferences →
// Lighting (per computer, like the rest of the network setup: the desk's IP
// belongs to the venue, not the show). A trigger then only says *what* to do
// ("GO", "bump sub 3") and audio/DeskCommands works out this desk's OSC / MSC;
// a custom OSC trigger with no host of its own also goes here.
struct LightingDesk {
    enum class Type {
        Eos,        // ETC Eos family: Eos, Ion, Ion Xe, Element, Nomad — OSC
        Ma3,        // grandMA3 — OSC command line (/cmd)
        Ma2Msc,     // grandMA2 (or anything MSC) — MIDI Show Control
    };

    Type    type = Type::Eos;
    QString host = QStringLiteral("192.168.1.100");
    int     port = 8000;          // Eos "OSC UDP RX port"; MA3's OSC input port
    QString ma3Prefix;            // MA3 OSC prefix ("gma3" → /gma3/cmd); empty = /cmd
    QString midiPort;             // MSC: MIDI output (empty = first available)
    int     mscDeviceId = 0x7F;   // MSC: 0x7F = all-call
    int     eosFaderBank = 9;     // Eos OSC fader-bank slot quewi uses, so it
                                  // doesn't re-page a remote's (HeliOSC uses 1)

    static QString typeKey(Type t);
    static Type    typeFromKey(const QString &key);
    QString        typeName() const;   // "ETC Eos / Ion / Nomad"
    QString        summary() const;    // "ETC Eos / Ion / Nomad at 10.0.0.5:8000"

    QJsonObject toJson() const;
    static LightingDesk fromJson(const QJsonObject &o);

    // Preferences (QSettings "lighting/desk/*").
    static LightingDesk load();
    void save() const;

    bool operator==(const LightingDesk &o) const { return toJson() == o.toJson(); }
};

} // namespace quewi::core
