#include "core/Workspace.h"

#include "core/CartGrid.h"
#include "core/CueList.h"
#include "core/PatchManager.h"
#include "core/ScriptModel.h"
#include "mix/MixShow.h"

#include <algorithm>

namespace quewi::core {

Workspace::Workspace(QObject *parent)
    : QObject(parent)
    , m_patches(std::make_unique<PatchManager>(this))
    , m_script(std::make_unique<ScriptModel>(this))
    , m_cart(std::make_unique<CartGrid>(this))
    , m_mixShow(std::make_unique<mix::MixShow>(this))
{
    connect(&m_undoStack, &QUndoStack::cleanChanged, this, &Workspace::dirtyChanged);

    // Everything below is saved in the show but edited outside the undo
    // stack, so it has to raise the unsaved flag itself. Loading also fires
    // some of these; ShowFile::load calls markClean() once it's done.
    connect(m_cart.get(), &CartGrid::modified, this, &Workspace::markModified);
    connect(m_mixShow.get(), &mix::MixShow::channelsChanged,  this, &Workspace::markModified);
    connect(m_mixShow.get(), &mix::MixShow::ensemblesChanged, this, &Workspace::markModified);
    connect(m_mixShow.get(), &mix::MixShow::dcaCountChanged,  this, &Workspace::markModified);
    connect(m_patches.get(), &PatchManager::patchesChanged,  this, [this] { markModified(); });
    connect(m_script.get(), &ScriptModel::scriptChanged,      this, &Workspace::markModified);
    connect(m_script.get(), &ScriptModel::annotationsChanged, this, &Workspace::markModified);
    // Cue-list add / remove / reorder (removal isn't undoable and even clears
    // the undo stack — which used to make the show look *saved*).
    connect(this, &Workspace::cueListsChanged, this, &Workspace::markModified);
}

Workspace::~Workspace() = default;

void Workspace::markModified()
{
    emit contentModified();
    if (m_modified) return;
    const bool wasDirty = isDirty();
    m_modified = true;
    if (!wasDirty) emit dirtyChanged();
}

void Workspace::markClean()
{
    m_modified = false;
    m_undoStack.setClean();
    emit dirtyChanged();   // setClean() only signals if the stack itself changed
}

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
    // Renaming a cue-list tab isn't undoable either.
    connect(raw, &CueList::nameChanged, this, &Workspace::markModified,
            Qt::UniqueConnection);
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
        // Prefer a Normal list as the fallback — the soundboard is not a
        // valid set-list home (its pad cues would surface in the cue table
        // and the GO context). Fall back to whatever remains only if there
        // is no normal list left.
        m_activeCueList = nullptr;
        for (const auto &cl : m_cueLists) {
            if (cl->kind() == CueList::Kind::Normal) { m_activeCueList = cl.get(); break; }
        }
        if (!m_activeCueList && !m_cueLists.empty())
            m_activeCueList = m_cueLists.front().get();
        emit activeCueListChanged();
    }
    taken->setParent(nullptr);
    emit cueListsChanged();
    return taken;
}

void Workspace::moveCueList(int from, int to)
{
    const int n = static_cast<int>(m_cueLists.size());
    if (from < 0 || to < 0 || from >= n || to >= n || from == to) return;
    auto item = std::move(m_cueLists[from]);
    m_cueLists.erase(m_cueLists.begin() + from);
    m_cueLists.insert(m_cueLists.begin() + to, std::move(item));
    emit cueListsChanged();
}

} // namespace quewi::core
