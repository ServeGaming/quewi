#pragma once

#include <QPointer>
#include <QUuid>
#include <QWidget>

class QGridLayout;

namespace quewi { class GoEngine; }
namespace quewi::core { class CartGrid; class Workspace; }
namespace quewi::cues { class Cue; }

namespace quewi::ui {

class CartCellButton;

// Alternate "cart" UI for SFX-style shows: a tap-to-fire grid of named
// buttons. Sits in the same central area as the cue list — the View
// menu toggles between them. Each cell points at a cue id stored in
// CartGrid; the underlying cue is still owned by a CueList, so colour,
// number, name and notes stay in sync between the cart and the list.
//
// Drag-drop a sound file onto an empty cell to create a new audio cue
// at the end of the active cue list and bind it to that cell.
class CartView : public QWidget {
    Q_OBJECT
public:
    explicit CartView(QWidget *parent = nullptr);
    ~CartView() override;

    void setWorkspace(core::Workspace *ws);
    void setGoEngine(GoEngine *engine);

signals:
    // Operator clicked a bound cell — parent fires the cue.
    void fireRequested(quewi::cues::Cue *cue);
    // Operator dropped a file on an empty cell — parent creates an
    // audio cue and binds it. Parent owns audio-cue creation so the
    // cart doesn't have to know about AudioCue / GoEngine.
    void fileDropped(int row, int col, const QString &filePath);

protected:
    void contextMenuEvent(QContextMenuEvent *event) override;

private slots:
    void onLayoutChanged();
    void onCueChanged();

private:
    void rebuildGrid();
    cues::Cue *cueForCellId(const QUuid &id) const;

    QPointer<core::Workspace> m_workspace;
    QPointer<GoEngine>        m_goEngine;
    QGridLayout              *m_grid = nullptr;
    QList<CartCellButton *>   m_cells;
};

} // namespace quewi::ui
