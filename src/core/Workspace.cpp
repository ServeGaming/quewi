#include "core/Workspace.h"

#include "core/CueList.h"
#include "core/PatchManager.h"
#include "core/ScriptModel.h"

#include <algorithm>

namespace quewi::core {

Workspace::Workspace(QObject *parent)
    : QObject(parent)
    , m_patches(std::make_unique<PatchManager>(this))
    , m_script(std::make_unique<ScriptModel>(this))
{
    connect(&m_undoStack, &QUndoStack::cleanChanged, this, &Workspace::dirtyChanged);
}

Workspace::~Workspace() = default;

void Workspace::setName(QString name)
{
    if (m_name == name) return;
    m_name = std::move(name);
    emit nameChanged();
}

void Workspace::setActiveCueList(CueList *list)
{
    if (m_activeCueList == list) return;
    m_activeCueList = list;
    emit activeCueListChanged();
}

CueList *Workspace::addCueList(std::unique_ptr<CueList> list)
{
    auto *raw = list.get();
    raw->setParent(this);
    m_cueLists.push_back(std::move(list));
    if (!m_activeCueList) {
        m_activeCueList = raw;
        emit activeCueListChanged();
    }
    emit cueListsChanged();
    return raw;
}

std::unique_ptr<CueList> Workspace::takeCueList(CueListId id)
{
    auto it = std::find_if(m_cueLists.begin(), m_cueLists.end(),
        [&](const auto &cl) { return cl->id() == id; });
    if (it == m_cueLists.end()) return nullptr;
    auto taken = std::move(*it);
    m_cueLists.erase(it);
    if (m_activeCueList == taken.get()) {
        m_activeCueList = m_cueLists.empty() ? nullptr : m_cueLists.front().get();
        emit activeCueListChanged();
    }
    taken->setParent(nullptr);
    emit cueListsChanged();
    return taken;
}

} // namespace quewi::core
