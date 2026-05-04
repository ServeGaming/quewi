#pragma once

#include <QDialog>

class QTableWidget;
class QPushButton;

namespace quewi::ui {

class ShortcutManager;

// Editor for user-rebindable shortcuts. Two-column table: action name +
// its current key sequence. Click the sequence cell, press the new
// combination, click Apply (or hit Enter). Reset button restores all
// defaults.
class ShortcutsDialog : public QDialog {
    Q_OBJECT
public:
    explicit ShortcutsDialog(ShortcutManager *mgr, QWidget *parent = nullptr);
    ~ShortcutsDialog() override;

private slots:
    void rebuild();
    void resetAll();

private:
    ShortcutManager *m_mgr = nullptr;
    QTableWidget    *m_table = nullptr;
};

} // namespace quewi::ui
