#pragma once

#include "audio/Vbap.h"
#include <QDialog>
#include <QList>
#include <QUuid>

class QComboBox;
class QListWidget;
class QPushButton;
class QSpinBox;
class QDoubleSpinBox;
class QLineEdit;

namespace quewi::core { class PatchManager; }

namespace quewi::ui {

// Editor for SpeakerArray patches. Shows the list of speaker patches
// in the workspace, lets the user pick a built-in template (Stereo /
// 5.1 / 7.1 / 7.1.4 / Custom), and edit individual speakers (label,
// output channel, azimuth, elevation, distance).
//
// Mirrors the ergonomics of PatchEditorDialog but knows about the
// nested speaker structure that wouldn't fit the generic key/value
// fields shape PatchEditorDialog uses.
class SpeakerPatchDialog : public QDialog {
    Q_OBJECT
public:
    explicit SpeakerPatchDialog(core::PatchManager *patches,
                                QWidget *parent = nullptr);

private slots:
    void onPatchSelected();
    void onAddPatchClicked();
    void onRemovePatchClicked();
    void onRenameClicked();
    void onTemplateChanged(int index);
    void onSpeakerSelected();
    void onAddSpeakerClicked();
    void onRemoveSpeakerClicked();
    void onSpeakerEdited();

private:
    void rebuildPatchList();
    void rebuildSpeakerList();
    QList<audio::Speaker> currentSpeakers() const;
    void writeBackSpeakers();
    void setEditableEnabled(bool enabled);

    core::PatchManager *m_patches;

    // Patch list (left column).
    QListWidget *m_patchList = nullptr;
    QPushButton *m_addPatchBtn    = nullptr;
    QPushButton *m_removePatchBtn = nullptr;
    QPushButton *m_renameBtn      = nullptr;

    // Per-patch editor (right column).
    QComboBox    *m_templateCombo = nullptr;
    QListWidget  *m_speakerList   = nullptr;
    QPushButton  *m_addSpeakerBtn    = nullptr;
    QPushButton  *m_removeSpeakerBtn = nullptr;

    // Per-speaker editor (right of the speaker list).
    QLineEdit       *m_spkLabel     = nullptr;
    QSpinBox        *m_spkChannel   = nullptr;
    QDoubleSpinBox  *m_spkAzimuth   = nullptr;
    QDoubleSpinBox  *m_spkElevation = nullptr;
    QDoubleSpinBox  *m_spkDistance  = nullptr;

    QUuid m_currentPatchId;
    QList<audio::Speaker> m_workingSpeakers;  // edited copy until Apply
    bool  m_suppressSignals = false;
};

} // namespace quewi::ui
