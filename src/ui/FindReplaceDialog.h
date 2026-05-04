#pragma once

#include <QDialog>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;

namespace quewi::core { class Workspace; }

namespace quewi::ui {

// Find-and-replace across cue names, notes, OSC addresses, and OSC raw
// args. Replacements push one undoable macro so the whole batch can be
// reversed with a single Ctrl+Z.
class FindReplaceDialog : public QDialog {
    Q_OBJECT
public:
    explicit FindReplaceDialog(core::Workspace *workspace, QWidget *parent = nullptr);
    ~FindReplaceDialog() override;

private slots:
    void doFind();
    void doReplaceAll();

private:
    int  apply(bool replace);

    core::Workspace *m_workspace = nullptr;
    QLineEdit   *m_find    = nullptr;
    QLineEdit   *m_replace = nullptr;
    QCheckBox   *m_caseSensitive = nullptr;
    QCheckBox   *m_scopeNames = nullptr;
    QCheckBox   *m_scopeNotes = nullptr;
    QCheckBox   *m_scopeOsc   = nullptr;
    QLabel      *m_summary = nullptr;
};

} // namespace quewi::ui
