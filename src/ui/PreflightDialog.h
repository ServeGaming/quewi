#pragma once

#include <QDialog>

class QListWidget;
class QLabel;
class QPushButton;

namespace quewi::core { class Workspace; }

namespace quewi::ui {

// Pre-show validation. Walks every cue and reports problems the
// operator should know about before the audience arrives:
//   - missing audio/video/image files
//   - audio that failed to decode
//   - OSC cues with empty host or port 0
//   - light cues with no channels
//   - fade / start / stop / goto cues with unresolved targets
//   - group cues with empty children list
//
// Items are colour-coded — green ✓ for ready, red ✕ for blocking, yellow
// ⚠ for warnings (e.g. very long pre-wait that might be a typo).
class PreflightDialog : public QDialog {
    Q_OBJECT
public:
    explicit PreflightDialog(core::Workspace *workspace, QWidget *parent = nullptr);
    ~PreflightDialog() override;

private slots:
    void runChecks();

private:
    core::Workspace *m_workspace = nullptr;
    QLabel       *m_summary = nullptr;
    QListWidget  *m_results = nullptr;
    QPushButton  *m_recheck = nullptr;
};

} // namespace quewi::ui
