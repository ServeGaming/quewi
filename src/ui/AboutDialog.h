#pragma once

#include <QDialog>

namespace quewi::ui {

// Modal "About quewi" dialog: kiwi icon, version, build info, AGPL
// summary, and a link to the GitHub repo. Triggered from Help → About
// and Ctrl+? — both wired in MainWindow::buildMenus.
class AboutDialog : public QDialog {
    Q_OBJECT
public:
    explicit AboutDialog(QWidget *parent = nullptr);
};

} // namespace quewi::ui
