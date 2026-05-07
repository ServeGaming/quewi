#pragma once

#include <QObject>
#include <QString>
#include <QUndoStack>
#include <QUuid>
#include <memory>
#include <vector>

namespace quewi::cues { class Cue; }

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

    bool isDirty() const { return m_undoStack.isClean() == false; }
    void markClean() { m_undoStack.setClean(); }

signals:
    void nameChanged();
    void cueListsChanged();
    void activeCueListChanged();
    void dirtyChanged();

private:
    QString m_name;
    std::vector<std::unique_ptr<CueList>> m_cueLists;
    CueList *m_activeCueList = nullptr;
    QUndoStack m_undoStack;
    std::unique_ptr<PatchManager> m_patches;
    std::unique_ptr<ScriptModel> m_script;
    std::unique_ptr<CartGrid>    m_cart;
};

} // namespace quewi::core
