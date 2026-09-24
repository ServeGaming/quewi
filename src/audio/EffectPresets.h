#pragma once

#include <QJsonArray>
#include <QList>
#include <QString>

namespace quewi::audio {

// A named effects chain for the rack: the same {type, enabled, params} JSON
// the show file stores per track, so applying a preset is just loading a
// chain. Parameters a preset doesn't mention keep the effect's defaults.
struct EffectPreset {
    QString    name;
    QString    group;        // menu section: Voice / Space / Character / Mix; "" for user presets
    QString    description;  // tooltip
    QJsonArray effects;
    bool       builtIn = true;
};

namespace EffectPresets {
    // Shipped presets, in menu order.
    QList<EffectPreset> builtIns();
    // The user's saved presets (QSettings "effects/userPresets"), by name.
    QList<EffectPreset> userPresets();
    // Save (or overwrite, by name) a user preset.
    void saveUserPreset(const QString &name, const QJsonArray &effects);
    void removeUserPreset(const QString &name);
}

} // namespace quewi::audio
