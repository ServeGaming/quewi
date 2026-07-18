#pragma once

#include "mix/ConsoleLink.h"

#include <QPointer>
#include <QWidget>
#include <memory>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTableView;

namespace quewi::core { class CueList; class Workspace; }
namespace quewi::mix  { class MixCue; }

namespace quewi::ui {

class MixGridModel;

// The live-mixing page: a DCA cue grid plus the console connection.
//
// The workflow this implements, and the reason it's worth building: the
// software owns the bookkeeping — which mics are on which faders for this
// scene, everything else muted — and the human owns the mix. quewi Mix never
// recalls DCA fader levels. That restraint is the entire product.
class MixView : public QWidget {
    Q_OBJECT
public:
    explicit MixView(QWidget *parent = nullptr);
    ~MixView() override;

    void setWorkspace(core::Workspace *ws);
    void setCueList(core::CueList *list);

    // Fire the selected cue at the console, and advance. Returns false if
    // there's nothing to fire or no console.
    bool fireSelected();

    // ── State for the main transport bar's DCA GO button ──────────────
    // MixView owns the mix list + console link, so it answers whether a DCA
    // GO is possible and what the next cue is. The transport bar only shows
    // the result — it stays free of mix types. mixStateChanged() fires
    // whenever any of these answers could have changed.
    bool    canFireNext() const;   // console connected AND a cue is queued
    QString dcaGoTooltip() const;  // describes the next cue, or why disabled

    // Add a mix cue after the selection (or at the end), and start editing it.
    void addCue();
    // Delete the selected cue.
    void deleteSelectedCue();

signals:
    void statusMessage(const QString &text);
    // Emitted when the DCA GO's availability or next-cue label could have
    // changed: selection moved, a console connected/dropped, or the mix list
    // was swapped. The main window refreshes the transport bar's DCA GO on it.
    void mixStateChanged();

private slots:
    void onAddCue()    { addCue(); }
    void onDeleteCue() { deleteSelectedCue(); }
    void onEditChannels();
    void onConnectClicked();
    void onLinkState(quewi::mix::ConsoleLink::State state);
    void onResyncRequired(const QString &reason);
    void onCueEdited(quewi::mix::MixCue *cue);

private:
    void buildUi();
    void refreshConnectionUi();
    // Sets both our pointer and the grid's live-row marker, so the row painted
    // "on the desk" is always the cue we're actually pushing.
    void setLiveCue(mix::MixCue *cue);
    void checkSceneSafe();
    mix::MixCue *selectedCue() const;

    QPointer<core::Workspace> m_workspace;

    QTableView   *m_table   = nullptr;
    MixGridModel *m_model   = nullptr;

    QComboBox   *m_protocol = nullptr;
    QLineEdit   *m_host     = nullptr;
    QPushButton *m_connect  = nullptr;
    QLabel      *m_status   = nullptr;
    QLabel      *m_warning  = nullptr;   // the Scene Safe / registration banner
    QSpinBox    *m_dcaCount = nullptr;
    QPushButton *m_go       = nullptr;

    std::unique_ptr<mix::ConsoleLink> m_link;

    // The cue currently applied to the console. Editing THIS cue live-updates
    // the desk; editing any other one only edits the show.
    QPointer<QObject> m_liveCue;
};

} // namespace quewi::ui
