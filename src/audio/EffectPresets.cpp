#include "audio/EffectPresets.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>

namespace quewi::audio {

namespace {

const auto kUserKey = QStringLiteral("effects/userPresets");

using Params = std::initializer_list<std::pair<const char *, double>>;

QJsonObject fx(const char *type, Params params)
{
    QJsonObject p;
    for (const auto &[id, v] : params) p.insert(QString::fromLatin1(id), v);
    return QJsonObject{ {QStringLiteral("type"), QString::fromLatin1(type)},
                        {QStringLiteral("enabled"), true},
                        {QStringLiteral("params"), p} };
}

// EQ band helper. Types: 0 peak, 1 low shelf, 2 high shelf, 3 low-pass,
// 4 high-pass (EqEffect::FilterType). Bands a preset doesn't touch stay at
// 0 dB, which the EQ skips entirely.
enum : int { Peak = 0, LowShelf = 1, HighShelf = 2, LowPass = 3, HighPass = 4 };
QJsonObject eq(std::initializer_list<std::tuple<int, int, double, double, double>> bands)
{
    QJsonObject p;
    for (const auto &[n, type, freq, gain, q] : bands) {
        p.insert(QStringLiteral("eq%1_type").arg(n), type);
        p.insert(QStringLiteral("eq%1_freq").arg(n), freq);
        p.insert(QStringLiteral("eq%1_gain").arg(n), gain);
        p.insert(QStringLiteral("eq%1_q").arg(n),    q);
        p.insert(QStringLiteral("eq%1_enabled").arg(n), 1.0);
    }
    return QJsonObject{ {QStringLiteral("type"), QStringLiteral("eq")},
                        {QStringLiteral("enabled"), true},
                        {QStringLiteral("params"), p} };
}

QJsonObject reverb(double room, double damping, double width, double wet)
{
    return fx("reverb", { {"roomSize", room}, {"damping", damping},
                          {"width", width}, {"wet", wet} });
}
QJsonObject delay(double l, double r, double feedback, double wet)
{
    return fx("delay", { {"timeL", l}, {"timeR", r}, {"feedback", feedback}, {"wet", wet} });
}
QJsonObject comp(double thresh, double ratio, double attack, double release, double makeup)
{
    return fx("compressor", { {"threshold", thresh}, {"ratio", ratio}, {"attack", attack},
                              {"release", release}, {"knee", 6.0}, {"makeup", makeup} });
}

EffectPreset make(const QString &group, const QString &name, const QString &desc,
                  std::initializer_list<QJsonObject> chain)
{
    EffectPreset p;
    p.group = group; p.name = name; p.description = desc;
    for (const auto &o : chain) p.effects.append(o);
    return p;
}

} // namespace

QList<EffectPreset> EffectPresets::builtIns()
{
    const QString voice = QObject::tr("Voice"), space = QObject::tr("Space"),
                  character = QObject::tr("Character"), mix = QObject::tr("Mix");
    return {
        // ── Voice: through a device ────────────────────────────────────
        make(voice, QObject::tr("Telephone"), QObject::tr("Thin, band-limited phone line"),
             { eq({ {1, HighPass, 450, 0, 0.8}, {3, Peak, 1400, 4, 1.0}, {6, LowPass, 3200, 0, 0.8} }),
               fx("distortion", { {"drive", 0.2}, {"tone", 0.4}, {"mix", 0.6} }),
               comp(-24, 6, 5, 80, 4) }),
        make(voice, QObject::tr("Old radio"), QObject::tr("Warm, crackly vintage wireless"),
             { eq({ {1, HighPass, 250, 0, 0.7}, {3, Peak, 1800, 5, 0.9}, {6, LowPass, 5000, 0, 0.7} }),
               fx("distortion", { {"drive", 0.25}, {"tone", 0.5}, {"mix", 0.7} }),
               fx("lofi", { {"bits", 10}, {"downsample", 2}, {"mix", 0.6} }) }),
        make(voice, QObject::tr("Megaphone / PA"), QObject::tr("Honky, overdriven loudhailer"),
             { eq({ {1, HighPass, 700, 0, 1.0}, {3, Peak, 2000, 8, 1.2}, {6, LowPass, 3000, 0, 1.0} }),
               fx("distortion", { {"drive", 0.6}, {"tone", 0.5}, {"mix", 1.0}, {"output", -3} }),
               comp(-20, 8, 5, 80, 3) }),
        make(voice, QObject::tr("Walkie-talkie"), QObject::tr("Squashed, gritty two-way radio"),
             { eq({ {1, HighPass, 500, 0, 0.9}, {3, Peak, 1600, 6, 1.0}, {6, LowPass, 2600, 0, 0.9} }),
               fx("distortion", { {"drive", 0.45}, {"tone", 0.45}, {"mix", 0.9} }),
               fx("lofi", { {"bits", 8}, {"downsample", 3}, {"mix", 0.5} }),
               comp(-26, 10, 3, 60, 5) }),
        make(voice, QObject::tr("Voice clarity"), QObject::tr("Cleaner, more present dialogue"),
             { eq({ {1, HighPass, 90, 0, 0.7}, {2, Peak, 250, -3, 1.0},
                    {4, Peak, 3500, 3, 1.0}, {6, HighShelf, 10000, 2, 0.7} }),
               comp(-20, 3, 10, 120, 3) }),

        // ── Space: where the sound is ──────────────────────────────────
        make(space, QObject::tr("Next room"), QObject::tr("Muffled, through a wall"),
             { eq({ {1, LowShelf, 120, -2, 0.7}, {6, LowPass, 650, 0, 0.7} }),
               reverb(0.3, 0.8, 0.6, 0.25) }),
        make(space, QObject::tr("Far away"), QObject::tr("Distant and thin"),
             { eq({ {1, HighPass, 250, 0, 0.7}, {6, LowPass, 3500, 0, 0.7} }),
               reverb(0.6, 0.6, 1.0, 0.35) }),
        make(space, QObject::tr("Small room"), QObject::tr("A little natural room tone"),
             { reverb(0.3, 0.6, 0.8, 0.18) }),
        make(space, QObject::tr("Concert hall"), QObject::tr("Big, smooth hall"),
             { reverb(0.75, 0.4, 1.0, 0.33) }),
        make(space, QObject::tr("Cathedral"), QObject::tr("Huge stone space, long tail"),
             { reverb(0.95, 0.2, 1.0, 0.5) }),
        make(space, QObject::tr("Slapback echo"), QObject::tr("One quick bounce off a wall"),
             { delay(110, 125, 0.15, 0.3) }),
        make(space, QObject::tr("Canyon echo"), QObject::tr("Repeating echoes across a valley"),
             { delay(450, 600, 0.55, 0.4), reverb(0.6, 0.5, 1.0, 0.2) }),
        make(space, QObject::tr("Stadium announcer"), QObject::tr("PA voice filling an arena"),
             { comp(-22, 4, 10, 120, 3), delay(160, 220, 0.3, 0.22), reverb(0.85, 0.3, 1.0, 0.3) }),

        // ── Character: changes what the sound is ───────────────────────
        make(character, QObject::tr("Monster"), QObject::tr("Deep, gravelly creature voice"),
             { fx("pitch", { {"semitones", -7}, {"mix", 1.0} }),
               eq({ {1, LowShelf, 150, 4, 0.7} }),
               fx("distortion", { {"drive", 0.15}, {"tone", 0.4}, {"mix", 0.3} }),
               reverb(0.4, 0.6, 1.0, 0.15) }),
        make(character, QObject::tr("Giant"), QObject::tr("An octave down in a big space"),
             { fx("pitch", { {"semitones", -12}, {"mix", 1.0} }), reverb(0.8, 0.4, 1.0, 0.3) }),
        make(character, QObject::tr("Chipmunk"), QObject::tr("High and squeaky"),
             { fx("pitch", { {"semitones", 8}, {"mix", 1.0} }) }),
        make(character, QObject::tr("Robot"), QObject::tr("Metallic, digital voice"),
             { delay(12, 12, 0.7, 0.5), fx("lofi", { {"bits", 8}, {"downsample", 2}, {"mix", 0.5} }) }),
        make(character, QObject::tr("Ghost"), QObject::tr("Ethereal, echoing, slightly detuned"),
             { fx("pitch", { {"semitones", -3}, {"mix", 0.4} }),
               delay(320, 480, 0.45, 0.3), reverb(0.95, 0.2, 1.0, 0.6) }),
        make(character, QObject::tr("Underwater"), QObject::tr("Submerged and wobbling"),
             { eq({ {6, LowPass, 450, 0, 1.5} }),
               fx("tremolo", { {"rate", 1.5}, {"depth", 0.35}, {"stereo", 0.0} }),
               reverb(0.5, 0.7, 1.0, 0.3) }),
        make(character, QObject::tr("Old record"), QObject::tr("Dusty gramophone"),
             { fx("lofi", { {"bits", 10}, {"downsample", 2}, {"mix", 0.7} }),
               eq({ {1, HighPass, 200, 0, 0.7}, {6, LowPass, 4500, 0, 0.7} }),
               fx("distortion", { {"drive", 0.1}, {"tone", 0.3}, {"mix", 0.3} }) }),
        make(character, QObject::tr("8-bit game"), QObject::tr("Crunchy retro console"),
             { fx("lofi", { {"bits", 4}, {"downsample", 8}, {"mix", 1.0} }) }),
        make(character, QObject::tr("Warble"), QObject::tr("Wobbly, failing speaker"),
             { fx("tremolo", { {"rate", 6}, {"depth", 0.6}, {"stereo", 0.0} }) }),
        make(character, QObject::tr("Auto-pan"), QObject::tr("Swings slowly left and right"),
             { fx("tremolo", { {"rate", 0.5}, {"depth", 1.0}, {"stereo", 1.0} }) }),

        // ── Mix: level and tone clean-up ───────────────────────────────
        make(mix, QObject::tr("Loud & punchy"), QObject::tr("Heavy compression for impact"),
             { comp(-24, 6, 5, 80, 6) }),
        make(mix, QObject::tr("Gentle leveller"), QObject::tr("Evens out loud and quiet parts"),
             { comp(-18, 2.5, 20, 200, 2) }),
        make(mix, QObject::tr("Warm"), QObject::tr("More low end, softer top"),
             { eq({ {1, LowShelf, 150, 3, 0.7}, {6, HighShelf, 9000, -3, 0.7} }) }),
        make(mix, QObject::tr("Bright"), QObject::tr("More air and sparkle"),
             { eq({ {1, HighPass, 60, 0, 0.7}, {6, HighShelf, 8000, 4, 0.7} }) }),
        make(mix, QObject::tr("Remove rumble"), QObject::tr("Cut low-end thumps and hum"),
             { eq({ {1, HighPass, 100, 0, 0.7} }) }),
    };
}

QList<EffectPreset> EffectPresets::userPresets()
{
    QList<EffectPreset> out;
    const QJsonObject all = QJsonDocument::fromJson(
        QSettings(QStringLiteral("ServeGaming"), QStringLiteral("quewi")).value(kUserKey).toByteArray()).object();
    for (auto it = all.begin(); it != all.end(); ++it) {
        EffectPreset p;
        p.name = it.key();
        p.effects = it.value().toArray();
        p.builtIn = false;
        out.append(p);
    }
    return out;   // QJsonObject keys come back sorted: alphabetical menu
}

void EffectPresets::saveUserPreset(const QString &name, const QJsonArray &effects)
{
    QSettings s(QStringLiteral("ServeGaming"), QStringLiteral("quewi"));
    QJsonObject all = QJsonDocument::fromJson(s.value(kUserKey).toByteArray()).object();
    all.insert(name, effects);
    s.setValue(kUserKey, QJsonDocument(all).toJson(QJsonDocument::Compact));
}

void EffectPresets::removeUserPreset(const QString &name)
{
    QSettings s(QStringLiteral("ServeGaming"), QStringLiteral("quewi"));
    QJsonObject all = QJsonDocument::fromJson(s.value(kUserKey).toByteArray()).object();
    all.remove(name);
    s.setValue(kUserKey, QJsonDocument(all).toJson(QJsonDocument::Compact));
}

} // namespace quewi::audio
