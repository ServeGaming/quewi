#pragma once

#include <QDialog>

namespace quewi::ui {

// Skeleton — just the shell. Real preference categories (Audio, OSC,
// MIDI, Lighting, Theme, Show Mode) populate as their subsystems
// come online in Phase 2-7.
class PreferencesDialog : public QDialog {
    Q_OBJECT
public:
    explicit PreferencesDialog(QWidget *parent = nullptr);
    ~PreferencesDialog() override;
};

} // namespace quewi::ui
