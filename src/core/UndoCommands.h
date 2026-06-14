#pragma once

#include <QUndoCommand>
#include <QVariant>
#include <memory>

#include "core/Workspace.h"

namespace quewi::cues { class Cue; }

namespace quewi::core {

class CueList;

class InsertCueCommand : public QUndoCommand {
public:
    InsertCueCommand(CueList *list, int row, std::unique_ptr<cues::Cue> cue,
                     QUndoCommand *parent = nullptr);
    void undo() override;
    void redo() override;

private:
    CueList *m_list;
    int m_row;
    std::unique_ptr<cues::Cue> m_storage; // holds the cue while undone
};

class RemoveCueCommand : public QUndoCommand {
public:
    RemoveCueCommand(CueList *list, int row, QUndoCommand *parent = nullptr);
    void undo() override;
    void redo() override;

private:
    CueList *m_list;
    int m_row;
    std::unique_ptr<cues::Cue> m_storage;
};

// Generic field edit. The setter is invoked via cue->setField(name, value).
class EditCueFieldCommand : public QUndoCommand {
public:
    EditCueFieldCommand(cues::Cue *cue, QString field,
                        QVariant oldValue, QVariant newValue,
                        QUndoCommand *parent = nullptr);
    void undo() override;
    void redo() override;
    int  id() const override { return 1; }
    bool mergeWith(const QUndoCommand *other) override;

private:
    cues::Cue *m_cue;
    QString    m_field;
    QVariant   m_old;
    QVariant   m_new;
};

// Move a cue from one row to another. Uses takeCue + insertCue so the
// model already routes the right inserted/removed signals.
class MoveCueCommand : public QUndoCommand {
public:
    MoveCueCommand(CueList *list, int from, int to, QUndoCommand *parent = nullptr);
    void undo() override;
    void redo() override;

private:
    int destRowAfterTake() const;
    CueList *m_list;
    int m_from;
    int m_to;
};

class RenameCueListCommand : public QUndoCommand {
public:
    RenameCueListCommand(CueList *list, QString newName, QUndoCommand *parent = nullptr);
    void undo() override;
    void redo() override;

private:
    CueList *m_list;
    QString  m_old;
    QString  m_new;
};

// Drag-reorder of cue-list tabs. Undoable so the new order is dirty-tracked.
class MoveCueListCommand : public QUndoCommand {
public:
    MoveCueListCommand(Workspace *ws, int from, int to, QUndoCommand *parent = nullptr);
    void undo() override;
    void redo() override;

private:
    Workspace *m_ws;
    int m_from;
    int m_to;
};

} // namespace quewi::core
