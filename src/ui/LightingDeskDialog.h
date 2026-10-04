#pragma once

#include "core/LightingDesk.h"

#include <QDialog>

class QButtonGroup;
class QCheckBox;
class QComboBox;
class QDialogButtonBox;
class QFrame;
class QGroupBox;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QSpinBox;
class QVBoxLayout;

namespace quewi::ui {

// "Lighting Desk" — where quewi sends lighting triggers (core::LightingDesk).
//
// A proper modal setup dialog rather than a form crammed into Preferences:
// pick the desk, give its address, read the matching "on the desk" steps,
// then Save. Nothing is written to QSettings until Save; Cancel discards.
//
// The one thing it goes out of its way to make clear: the port here is the
// DESK's receive port. quewi's own OSC port (Preferences → OSC) is where
// remotes talk to quewi, and setting that one doesn't change where triggers
// go — the confusion this dialog exists to end.
class LightingDeskDialog : public QDialog {
    Q_OBJECT
public:
    explicit LightingDeskDialog(QWidget *parent = nullptr);

    // Opens the dialog modally. Returns true if the user pressed Save.
    static bool edit(QWidget *parent);

    // What Save would write right now (fields read live, eosFaderBank kept
    // as loaded). Exposed so tests can inspect it without touching settings.
    core::LightingDesk currentDesk() const;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    using Type = core::LightingDesk::Type;

    QFrame *makeDeskCard(Type type, const QString &name, const QString &blurb);
    Type    selectedType() const;
    void    selectType(Type type);
    void    refreshForType();
    void    validateHost();
    void    rebuildSteps();
    void    save();

    core::LightingDesk m_loaded;            // what was in settings when opened

    QButtonGroup     *m_typeGroup   = nullptr;
    QList<QFrame *>   m_cards;

    QGroupBox        *m_oscGroup    = nullptr;
    QLineEdit        *m_host        = nullptr;
    QLabel           *m_hostError   = nullptr;
    QLabel           *m_hostNote    = nullptr;   // "127.0.0.1 is this computer…"
    QSpinBox         *m_port        = nullptr;
    QCheckBox        *m_feedback    = nullptr;
    QSpinBox         *m_feedbackPort = nullptr;
    QWidget          *m_feedbackRow = nullptr;
    QLabel           *m_prefixLabel = nullptr;
    QLineEdit        *m_prefix      = nullptr;
    QLabel           *m_ownPortNote = nullptr;

    QGroupBox        *m_mscGroup    = nullptr;
    QComboBox        *m_midiPort    = nullptr;
    QSpinBox         *m_deviceId    = nullptr;

    QLabel           *m_stepsTitle  = nullptr;
    QVBoxLayout      *m_stepsLayout = nullptr;
    QList<QHBoxLayout *> m_stepRows;

    QDialogButtonBox *m_buttons     = nullptr;
    bool              m_hostValid   = true;
};

} // namespace quewi::ui
