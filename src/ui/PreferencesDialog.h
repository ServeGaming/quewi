#pragma once

#include <QDialog>

namespace quewi::audio { class AudioEngine; }
namespace quewi::midi  { class MidiInputEngine; }

namespace quewi::ui {

// Application-level preferences. Phase 3 has a working Audio page;
// other categories show placeholders until their subsystem lands.
class PreferencesDialog : public QDialog {
    Q_OBJECT
public:
    explicit PreferencesDialog(audio::AudioEngine *audioEngine,
                               midi::MidiInputEngine *midiInput,
                               QWidget *parent = nullptr);
    ~PreferencesDialog() override;

signals:
    // Emitted when the user toggles cue-list column visibility, so the
    // owning window can refresh its open list views without a restart.
    void cueListColumnsChanged();
    // Theme picker on the Theme page. MainWindow reapplies the QSS.
    void themeChanged(const QString &name);
    // Cue row density change on the Theme page. MainWindow rewires the
    // CueListView's row height delegate.
    void rowDensityChanged(const QString &density);
};

} // namespace quewi::ui
