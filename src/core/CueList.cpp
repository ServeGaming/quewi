#include "core/CueList.h"

#include "cues/Cue.h"

#include <algorithm>

namespace quewi::core {

CueList::CueList(QString name, QObject *parent)
    : QObject(parent)
    , m_id(QUuid::createUuid())
    , m_name(std::move(name))
{
}

CueList::~CueList() = default;

void CueList::setName(QString name)
{
    if (m_name == name) return;
    m_name = std::move(name);
    emit nameChanged();
}

cues::Cue *CueList::cueAt(int row) const
{
    if (row < 0 || row >= static_cast<int>(m_cues.size())) return nullptr;
    return m_cues[static_cast<size_t>(row)].get();
}

int CueList::rowOf(const cues::Cue *cue) const
{
    auto it = std::find_if(m_cues.begin(), m_cues.end(),
        [&](const auto &c) { return c.get() == cue; });
    if (it == m_cues.end()) return -1;
    return static_cast<int>(std::distance(m_cues.begin(), it));
}

void CueList::insertCue(int row, std::unique_ptr<cues::Cue> cue)
{
    if (!cue) return;
    if (row < 0 || row > static_cast<int>(m_cues.size()))
        row = static_cast<int>(m_cues.size());
    cue->setParent(this);
    emit aboutToInsertCue(row);
    m_cues.insert(m_cues.begin() + row, std::move(cue));
    emit cueInserted(row);
}

std::unique_ptr<cues::Cue> CueList::takeCue(int row)
{
    if (row < 0 || row >= static_cast<int>(m_cues.size())) return nullptr;
    emit aboutToRemoveCue(row);
    auto cue = std::move(m_cues[static_cast<size_t>(row)]);
    m_cues.erase(m_cues.begin() + row);
    cue->setParent(nullptr);
    emit cueRemoved(row);
    return cue;
}

} // namespace quewi::core
