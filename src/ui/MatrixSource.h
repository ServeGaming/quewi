#pragma once

#include "core/MatrixModel.h"

#include <QJsonObject>
#include <QSet>
#include <QString>
#include <QUuid>
#include <QVector>

#include <vector>

namespace quewi::cues { class Cue; }
namespace quewi::core { class CueList; class Workspace; }
namespace quewi::osc  { struct EosCue; class EosCueLists; }

namespace quewi::ui {

// Fills the Matrix List's pure model (core/MatrixModel) from a real show and
// the desk's cue list. Kept apart from the view so it's testable on its own.

// Which desk cues a quewi cue fires, read from what it already does:
//   * lighting triggers on its song (audio cue, or a video cue's sound):
//     "Go to cue" desk actions, and custom OSC to /eos/cue/[<list>/]<n>/fire
//     or an /eos/newcmd|cmd "Go_To_Cue [<list>/]<n>" — a trigger at the top
//     of the song counts as firing at GO, a later one is a hit in the song;
//   * an OSC cue sending one of those addresses;
//   * an MSC cue sending GO with a Q number (Q list = the desk list).
std::vector<core::matrix::DeskFire> deskFiresOf(cues::Cue *cue);

// The source list's cues, in order, as the matrix reads them.
std::vector<core::matrix::QuewiCue> matrixQuewiCues(const core::CueList *list);

// The quewi list a Matrix List interleaves: its configured source if it
// still exists and is a normal list, else the show's first normal list.
core::CueList *matrixSourceList(const core::Workspace *ws, const core::CueList *matrix);

core::matrix::DeskCue toDeskCue(const osc::EosCue &c);
std::vector<core::matrix::DeskCue> toDeskCues(const QVector<osc::EosCue> &cues);

// "12", "12.5" — a quewi cue number as typed (no trailing zeros).
QString matrixCueNumber(double n);

// A Matrix List worked out: its inputs and its rows.
struct MatrixBuild {
    enum class Desk {
        None,       // never read, nothing cached
        Live,       // read from the desk this session
        Cached,     // the cues saved with the show last time it was read
    };
    core::CueList *source = nullptr;
    QString deskList;
    Desk desk = Desk::None;
    std::vector<core::matrix::QuewiCue> quewi;
    std::vector<core::matrix::DeskCue> deskCues;
    core::matrix::Result result;

    int rowOfQuewi(const QUuid &id) const;                         // its Quewi row; -1
    int rowOfDesk(const QString &list, const QString &number) const;   // first row holding it; -1
};
// `reader` may be null (no desk): the cached cues are used.
MatrixBuild buildMatrix(const core::Workspace *ws, const core::CueList *matrix,
                        const osc::EosCueLists *reader);

// What's live, for highlighting and remotes.
struct MatrixLive {
    QUuid standby;                  // what GO fires next
    QSet<QUuid> running;            // quewi cues playing now
    QString deskActiveList, deskActiveCue;     // the desk's running cue
    QString deskPendingList, deskPendingCue;   // ... and its next
    bool operator==(const MatrixLive &o) const {
        return standby == o.standby && running == o.running
            && deskActiveList == o.deskActiveList && deskActiveCue == o.deskActiveCue
            && deskPendingList == o.deskPendingList && deskPendingCue == o.deskPendingCue;
    }
};
// Is this desk cue the desk's active / pending one? (Matches numbers as the
// desk writes them; an empty list in the feedback matches any list.)
bool matrixDeskIs(const core::matrix::DeskCue &c, const QString &list, const QString &cue);

// The merged list as JSON for remotes (/quewi/query/matrix): rows [from,
// from + count) — count < 0 = all. Documented in docs/osc-control/reference.md.
QJsonObject matrixJson(const core::CueList *matrix, const MatrixBuild &b, const MatrixLive &live,
                       int from = 0, int count = -1);

// "LX 12", "LX 12.5 P2" — a desk cue's short name on screen.
QString matrixDeskName(const core::matrix::DeskCue &c);

} // namespace quewi::ui
