#pragma once

#include <QPointer>
#include <QWidget>

class QLabel;
class QLineEdit;
class QSpinBox;
class QDoubleSpinBox;
class QPlainTextEdit;
class QGroupBox;
class QComboBox;

namespace quewi::core { class Workspace; }
namespace quewi::cues { class Cue; }
namespace quewi::osc  { class OscCue; }

namespace quewi::ui {

// Right-pane editor for the currently-selected cue. Common header
// (number, name, pre/post wait, notes) for all types; type-specific
// groups appear below for cue types we recognise (currently OSC).
//
// Edits are routed through EditCueFieldCommand on the workspace's
// undo stack so they're undoable.
class Inspector : public QWidget {
    Q_OBJECT
public:
    explicit Inspector(QWidget *parent = nullptr);
    ~Inspector() override;

    void setWorkspace(core::Workspace *workspace);

public slots:
    void setCue(quewi::cues::Cue *cue);

private slots:
    void onCueChanged();
    void commitNumber();
    void commitName();
    void commitPreWait();
    void commitPostWait();
    void commitNotes();

    // OSC-specific field commits (only used when current cue is an OscCue)
    void commitOscAddress();
    void commitOscHost();
    void commitOscPort();
    void commitOscArgs();

private:
    void rebuild();
    void pushFieldEdit(const QString &field, const QVariant &newValue);

    QPointer<core::Workspace> m_workspace;
    QPointer<cues::Cue>       m_cue;

    QLabel         *m_typeLabel = nullptr;
    QDoubleSpinBox *m_number    = nullptr;
    QLineEdit      *m_name      = nullptr;
    QDoubleSpinBox *m_preWait   = nullptr;
    QDoubleSpinBox *m_postWait  = nullptr;
    QPlainTextEdit *m_notes     = nullptr;

    QGroupBox      *m_oscGroup    = nullptr;
    QLineEdit      *m_oscAddress  = nullptr;
    QLineEdit      *m_oscHost     = nullptr;
    QSpinBox       *m_oscPort     = nullptr;
    QLineEdit      *m_oscArgs     = nullptr;

    bool m_loading = false;
};

} // namespace quewi::ui
