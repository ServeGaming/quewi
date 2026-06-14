#include "core/UndoCommands.h"

#include "core/CueList.h"
#include "cues/Cue.h"

namespace quewi::core {

InsertCueCommand::InsertCueCommand(CueList *list, int row,
                                   std::unique_ptr<cues::Cue> cue,
                                   QUndoCommand *parent)
    : QUndoCommand(parent)
    , m_list(list)
    , m_row(row)
    , m_storage(std::move(cue))
{
    setText(QObject::tr("Insert cue"));
}

void InsertCueCommand::redo()
{
    if (!m_storage) return;
    m_list->insertCue(m_row, std::move(m_storage));
}

void InsertCueCommand::undo()
{
    m_storage = m_list->takeCue(m_row);
}

RemoveCueCommand::RemoveCueCommand(CueList *list, int row, QUndoCommand *parent)
    : QUndoCommand(parent)
    , m_list(list)
    , m_row(row)
{
    setText(QObject::tr("Remove cue"));
}

void RemoveCueCommand::redo()
{
    m_storage = m_list->takeCue(m_row);
}

void RemoveCueCommand::undo()
{
    if (!m_storage) return;
    m_list->insertCue(m_row, std::move(m_storage));
}

EditCueFieldCommand::EditCueFieldCommand(cues::Cue *cue, QString field,
                                         QVariant oldValue, QVariant newValue,
                                         QUndoCommand *parent)
    : QUndoCommand(parent)
    , m_cue(cue)
    , m_field(std::move(field))
    , m_old(std::move(oldValue))
    , m_new(std::move(newValue))
{
    setText(QObject::tr("Edit %1").arg(m_field));
}

void EditCueFieldCommand::redo() { m_cue->setField(m_field, m_new); }
void EditCueFieldCommand::undo() { m_cue->setField(m_field, m_old); }

bool EditCueFieldCommand::mergeWith(const QUndoCommand *other)
{
    const auto *o = dynamic_cast<const EditCueFieldCommand *>(other);
    if (!o || o->m_cue != m_cue || o->m_field != m_field) return false;
    m_new = o->m_new;
    return true;
}

MoveCueCommand::MoveCueCommand(CueList *list, int from, int to, QUndoCommand *parent)
    : QUndoCommand(parent), m_list(list), m_from(from), m_to(to)
{
    setText(QObject::tr("Move cue"));
}

int MoveCueCommand::destRowAfterTake() const
{
    // After takeCue(from), every row above shifts down by one, so a
    // requested target index past the source becomes target-1.
    return (m_to > m_from) ? m_to - 1 : m_to;
}

void MoveCueCommand::redo()
{
    auto cue = m_list->takeCue(m_from);
    if (!cue) return;
    m_list->insertCue(destRowAfterTake(), std::move(cue));
}

void MoveCueCommand::undo()
{
    auto cue = m_list->takeCue(destRowAfterTake());
    if (!cue) return;
    m_list->insertCue(m_from, std::move(cue));
}

RenameCueListCommand::RenameCueListCommand(CueList *list, QString newName, QUndoCommand *parent)
    : QUndoCommand(parent)
    , m_list(list)
    , m_old(list->name())
    , m_new(std::move(newName))
{
    setText(QObject::tr("Rename cue list"));
}

void RenameCueListCommand::redo() { m_list->setName(m_new); }
void RenameCueListCommand::undo() { m_list->setName(m_old); }

MoveCueListCommand::MoveCueListCommand(Workspace *ws, int from, int to,
                                       QUndoCommand *parent)
    : QUndoCommand(parent), m_ws(ws), m_from(from), m_to(to)
{
    setText(QObject::tr("Reorder cue lists"));
}
// The tab bar already moved itself; redo only syncs the model order. undo
// moves the element now at `to` back to `from`.
void MoveCueListCommand::redo() { if (m_ws) m_ws->moveCueList(m_from, m_to); }
void MoveCueListCommand::undo() { if (m_ws) m_ws->moveCueList(m_to, m_from); }

} // namespace quewi::core
