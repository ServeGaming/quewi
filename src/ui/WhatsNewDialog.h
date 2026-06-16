#pragma once

#include <QDialog>

namespace quewi::ui {

// A "what's new" sheet shown once after the app updates to a new version.
// MainWindow::maybeShowWhatsNew() decides when to pop it (only on an actual
// version bump, never on a fresh install), and it's also reachable any time
// from Help → What's new. Styled to match the app — a real release-notes
// card, not a wall of bullet points.
class WhatsNewDialog : public QDialog {
    Q_OBJECT
public:
    explicit WhatsNewDialog(QWidget *parent = nullptr);

    // Pops the dialog if the running version differs from the one last shown
    // to this user AND a version was previously recorded (so a first-ever
    // install stays quiet). Records the running version either way. Returns
    // true if it actually showed. Call ~once, shortly after startup.
    static bool maybeShowForThisVersion(QWidget *parent);
};

} // namespace quewi::ui
