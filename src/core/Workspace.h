#pragma once

#include <QObject>
#include <QString>
#include <QUndoStack>
#include <QUuid>
#include <memory>
#include <vector>

namespace quewi::cues { class Cue; }
namespace quewi::mix  { class MixShow; }

namespace quewi::core {

class PatchManager;
class ScriptModel;
class CartGrid;

using CueId = QUuid;
using CueListId = QUuid;

class CueList;

// Root of the show document. Owns CueLists, the undo stack, and
// dirty-tracking. Edited via undo commands (see UndoCommands.h).
class Workspace : public QObject {
    Q_OBJECT
public:
    explicit Workspace(QObject *parent = nullptr);
    ~Workspace() override;

    QString name() const { return m_name; }
    void setName(QString name);

    const std::vector<std::unique_ptr<CueList>> &cueLists() const { return m_cueLists; }
    CueList *activeCueList() const { return m_activeCueList; }
    void setActiveCueList(CueList *list);

    // Used by ShowFile during load and by undo commands.
    CueList *addCueList(std::unique_ptr<CueList> list);
    std::unique_ptr<CueList> takeCueList(CueListId id);
    // Reorder: move the cue list at index `from` to index `to`. The tab
    // order persists via the show file's per-list `ord`. Used by drag-reorder.
    void moveCueList(int from, int to);

    QUndoStack *undoStack() { return &m_undoStack; }

    // Named, reusable patches (audio outputs, OSC destinations, MIDI ports,
    // DMX universes, video surfaces). The workspace owns one manager that
    // ships with the show file.
    PatchManager *patches() const { return m_patches.get(); }

    // The stage manager's annotated script. One per workspace; lazy —
    // empty until a script is loaded via ScriptModel::loadFromFile.
    ScriptModel *scriptModel() const { return m_script.get(); }

    // Cart-view layout — alternate UI for SFX shows. Cells point at
    // cue ids that still live in a CueList, so the cart never
    // duplicates show data.
    CartGrid *cart() const { return m_cart.get(); }

    // Live-mixing model: controlled channels, actors, ensembles. The
    // per-cue DCA assignments live in MixCues inside a CueList of
    // Kind::Mix, so this holds only what isn't per-cue. Empty until the
    // user sets up a console.
    mix::MixShow *mixShow() const { return m_mixShow.get(); }

    // Unsaved = the undo stack moved since the last save, OR something that
    // bypasses the undo stack changed: soundboard layout/keys, mix channels and
    // ensembles, patches, script notes, cue-list add/remove/rename/reorder, or
    // a mix-grid assignment. Without the second half those edits never raised
    // the close-without-saving prompt and were silently lost.
    bool isDirty() const { return !m_undoStack.isClean() || m_modified; }
    void markModified();
    void markClean();

signals:
    void nameChanged();
    void cueListsChanged();
    void activeCueListChanged();
    void dirtyChanged();
    // Every non-undoable edit (markModified), not just the first — drives
    // the crash journal.
    void contentModified();

private:
    QString m_name;
    std::vector<std::unique_ptr<CueList>> m_cueLists;
    CueList *m_activeCueList = nullptr;
    QUndoStack m_undoStack;
    std::unique_ptr<PatchManager>  m_patches;
    std::unique_ptr<ScriptModel>   m_script;
    std::unique_ptr<CartGrid>      m_cart;
    std::unique_ptr<mix::MixShow>  m_mixShow;
    bool m_modified = false;   // a non-undoable edit since the last save/load
};

} // namespace quewi::core
