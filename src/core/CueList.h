#pragma once

#include "core/MatrixModel.h"
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

    // What a list's tab shows and whether its cues join the set list:
    //   Normal     — the cue table; cues are in the set list.
    //   Soundboard — the pad grid; cues stay out of the set list.
    //   Mix        — the DCA assignment grid; cues stay out of the set list.
    //   Matrix     — the Matrix List: a merged running order of another
    //                list's cues and the lighting desk's (core/MatrixModel).
    //                Holds no cues of its own, only matrixConfig().
    //
    // Mix cues live in their own list rather than interleaved with sound cues
    // deliberately: a musical has 500+ mix cues and maybe 40 sound cues, so one
    // combined list would bury the sound cues in a wall of DCA moves. The two
    // lists mirror how the job is actually run.
    //
    // Runtime flag; persisted via the show file's meta keys, not as a cue-list
    // column.
    enum class Kind { Normal, Soundboard, Mix, Matrix };
    Kind kind() const { return m_kind; }
    void setKind(Kind k) { m_kind = k; }

    // Kind::Matrix only: which lists it interleaves and the placements made
    // by hand. Saved in the show file's meta (matrix_lists_json).
    const matrix::Config &matrixConfig() const { return m_matrix; }
    void setMatrixConfig(const matrix::Config &c);

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
    void matrixConfigChanged();

private:
    CueListId m_id;
    QString m_name;
    Kind m_kind = Kind::Normal;
    matrix::Config m_matrix;
    std::vector<std::unique_ptr<cues::Cue>> m_cues;
};

} // namespace quewi::core
