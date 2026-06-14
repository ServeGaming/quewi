#pragma once

#include "core/CartGrid.h"

#include <QPointer>
#include <QUuid>
#include <QWidget>
#include <functional>

class QGridLayout;
class QPushButton;
class QShortcut;
class QTimer;

namespace quewi { class GoEngine; }
namespace quewi::core { class Workspace; }
namespace quewi::cues { class Cue; }

namespace quewi::ui {

class CartPad;

// The sound-effect board: a tap-to-fire grid of vibrant pads, MIDI-pad style.
// Each pad fires a cue and can be customised per operator — colour, label,
// a keyboard hotkey, and a MIDI note (so a Launchpad / pad controller fires
// it). Pads glow while their cue is playing. An Edit toggle flips between
// performing (tap to fire) and laying out (tap to customise). OSC can fire
// pads remotely too (see docs/osc-control). Lives in the same central area
// as the cue list; the View menu toggles between them.
class CartView : public QWidget {
    Q_OBJECT
public:
    explicit CartView(QWidget *parent = nullptr);
    ~CartView() override;

    void setWorkspace(core::Workspace *ws);
    void setGoEngine(GoEngine *engine);

    // Fire the pad bound to a MIDI note. Called by MainWindow when a note-on
    // arrives while the soundboard is the active view. If a MIDI-learn is in
    // progress (the customise dialog is waiting for a note), the note is
    // assigned to the pad being edited instead. Returns true if it consumed
    // the note.
    bool handleMidiNote(int note);

    // Fire a pad by row/col or by flat index (row-major). Used by OSC.
    bool firePadAt(int row, int col);
    bool firePadIndex(int index);

    bool isEditMode() const { return m_editMode; }

    // MIDI learn — the customise dialog parks a target so the next incoming
    // note is assigned to that pad (and reported back via the callback)
    // instead of firing a cue.
    void beginMidiLearn(int row, int col, std::function<void(int)> onLearned);
    void cancelMidiLearn();

signals:
    void fireRequested(quewi::cues::Cue *cue);
    void fileDropped(int row, int col, const QString &filePath);
    void stopAllRequested();

protected:
    void showEvent(QShowEvent *event) override;

private slots:
    void onLayoutChanged();
    void onCueChanged();
    void onPollPlaying();
    void toggleEditMode();
    void resizeBoard();

private:
    void rebuildGrid();
    void rebuildHotkeys();
    cues::Cue *cueForCellId(const QUuid &id) const;
    void onPadClicked(int row, int col);
    void onPadEdit(int row, int col);
    void editPad(int row, int col);

    QPointer<core::Workspace> m_workspace;
    QPointer<GoEngine>        m_goEngine;

    QGridLayout       *m_grid = nullptr;
    QWidget           *m_gridHost = nullptr;
    QPushButton       *m_editBtn = nullptr;
    QList<CartPad *>   m_pads;
    QList<QShortcut *> m_shortcuts;
    QTimer            *m_pollTimer = nullptr;
    bool               m_editMode = false;

    // MIDI learn: when the customise dialog asks to learn a note, it parks a
    // target here. The next handleMidiNote() assigns to it instead of firing.
    int                     m_learnRow = -1;
    int                     m_learnCol = -1;
    std::function<void(int)> m_learnCallback;
};

} // namespace quewi::ui
