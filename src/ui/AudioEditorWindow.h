#pragma once

#include <QDialog>
#include <QPointer>

namespace quewi::audio { class AudioCue; }

namespace quewi::ui {

// Placeholder for the full audio editor that lands in Phase 9.
//
// Today: opens a window scoped to the double-clicked Audio cue,
// renders a large waveform preview, lists the sample-level features
// the editor will own, and links to the roadmap entry. The intent is
// that future work *replaces this dialog's body* with the real editor
// without re-plumbing the UI — keep the entry point stable.
class AudioEditorWindow : public QDialog {
    Q_OBJECT
public:
    explicit AudioEditorWindow(audio::AudioCue *cue, QWidget *parent = nullptr);
    ~AudioEditorWindow() override;

private:
    QPointer<audio::AudioCue> m_cue;
};

} // namespace quewi::ui
