#pragma once

#include <QDialog>

class QListWidget;
class QPushButton;

namespace quewi::ui {

// Help → Notifications. Shows the full history collected by
// Notifications::instance() with timestamps + level icons. Also
// reachable from the status bar's notification badge — clicking the
// badge opens this dialog with the unread count cleared.
class NotificationsDialog : public QDialog {
    Q_OBJECT
public:
    explicit NotificationsDialog(QWidget *parent = nullptr);

private slots:
    void rebuild();
    void onClearClicked();

private:
    QListWidget *m_list   = nullptr;
    QPushButton *m_clear  = nullptr;
};

} // namespace quewi::ui
