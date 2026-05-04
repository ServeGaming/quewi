#pragma once

#include "core/Workspace.h"

#include <QObject>
#include <QString>
#include <memory>
#include <vector>

namespace quewi::cues { class Cue; }

namespace quewi::core {

// An ordered collection of cues. A workspace can contain several
// (e.g. Main / Backup / Pre-show / Curtain Call).
//
// Cues are owned by the list. Reordering preserves cue identity.
class CueList : public QObject {
    Q_OBJECT
public:
    explicit CueList(QString name = QStringLiteral("Main"), QObject *parent = nullptr);
    ~CueList() override;

    CueListId id() const { return m_id; }
    void setId(CueListId id) { m_id = id; } // for ShowFile load

    QString name() const { return m_name; }
    void setName(QString name);

    int cueCount() const { return static_cast<int>(m_cues.size()); }
    cues::Cue *cueAt(int row) const;
    int rowOf(const cues::Cue *cue) const;

    // Owning insert/remove. Used by ShowFile during load and by undo commands.
    void insertCue(int row, std::unique_ptr<cues::Cue> cue);
    std::unique_ptr<cues::Cue> takeCue(int row);

signals:
    void nameChanged();
    void aboutToInsertCue(int row);
    void cueInserted(int row);
    void aboutToRemoveCue(int row);
    void cueRemoved(int row);
    void cueChanged(int row);

private:
    CueListId m_id;
    QString m_name;
    std::vector<std::unique_ptr<cues::Cue>> m_cues;
};

} // namespace quewi::core
