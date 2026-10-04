#pragma once

#include <QColor>
#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QUuid>

#include <vector>

// The Matrix List: one running order for the whole show, quewi's cues
// interleaved with the lighting desk's. This header is the pure model — plain
// data in, rows out — so the placement rules are unit-tested without a desk,
// a show or a widget. ui/MatrixSource fills the inputs from a real show and
// osc::EosCueLists; ui/MatrixView draws the rows.
//
// THE INTERMESHING MODEL (also in docs/using-quewi/matrix-list.md)
//
// A row is a moment in the show. quewi's cues keep their own order (the
// source cue list's) and each gets a row. Every desk cue is then placed by
// the first rule that applies:
//
//   1. Placed by hand  — you dragged it onto a quewi cue ("with": same row)
//      or between rows ("after": its own row after that quewi cue; "start":
//      above the first quewi cue). Saved in the show file.
//   2. Fired by quewi  — a quewi cue fires it at GO (a "Go to cue" desk
//      action or an /eos/cue/... OSC address on a lighting trigger at the
//      top of the song, an OSC cue, an MSC GO): same row as that cue.
//   3. Hit in a song   — a lighting trigger part-way through a song fires
//      it: a row nested under the song, at its time.
//   4. By order        — nothing ties it to quewi: it follows the desk cue
//      before it (desk order), in a row of its own after that cue's quewi
//      row. Desk cues before any tied one go at the top.
//
// Identity: a desk cue is list + number + part, plus the desk's UID when it
// was known. A hand placement whose cue the desk renumbered (same UID, new
// number) follows the cue; one whose cue is gone stays where it was, marked
// missing, until you clear it — a placement is never dropped silently. A
// quewi cue firing a desk cue that isn't on the desk shows it as missing too.
namespace quewi::core::matrix {

// A desk cue as the matrix needs it (filled from osc::EosCue). Parts of a
// multipart cue ride along with their cue.
struct DeskCue {
    QString list;               // "1"
    QString number;             // "12.5", as the desk writes it
    int     part = 0;
    QString uid;
    QString label;
    QString notes;
    QString scene;
    bool    sceneEnd = false;   // the desk marks this cue as the end of its scene
    double  upSeconds = -1.0;   // < 0 = not set
    double  downSeconds = -1.0;
    double  followSeconds = -1.0;
    double  hangSeconds = -1.0;
    int     partCount = 0;

    QJsonObject toJson() const;
    static DeskCue fromJson(const QJsonObject &o);
    bool operator==(const DeskCue &o) const { return toJson() == o.toJson(); }
};

// "this quewi cue fires desk cue <list>/<number>".
struct DeskFire {
    QString list;               // empty = the desk's default list (any)
    QString number;
    double  atSeconds = -1.0;   // < 0 = at GO; else seconds after GO (a trigger)
    QString triggerName;        // the lighting trigger's name, when it's one
};

// One quewi cue in the source list, in order.
struct QuewiCue {
    QUuid   id;
    QString number;             // "12" — as typed
    QString name;
    QString type;               // "Audio", "Video"…
    QString notes;
    QColor  colour;
    std::vector<DeskFire> fires;
};

// A placement you made by hand. Saved in the show file.
struct ManualPlacement {
    enum class Mode { With, After, Start };
    QString list, number;       // desk cue identity
    int     part = 0;
    QString uid;                // when known — follows a renumber
    QString label;              // last label seen, for a missing cue's row
    Mode    mode = Mode::With;
    QUuid   anchor;             // the quewi cue (unused for Start)

    QJsonObject toJson() const;
    static ManualPlacement fromJson(const QJsonObject &o);
    bool sameCue(const DeskCue &c) const;     // uid first, else list+number+part
    bool operator==(const ManualPlacement &o) const { return toJson() == o.toJson(); }
};

// Everything a Matrix List keeps in the show file (one per list).
struct Config {
    static constexpr int kVersion = 1;
    QUuid   sourceList;         // quewi cue list to interleave; null = the first normal list
    QString deskList = QStringLiteral("1");   // the desk's cue list
    std::vector<ManualPlacement> placements;
    // The desk's cues as last read, so the matrix still reads right with no
    // desk connected (planning at home, a desk that's off).
    std::vector<DeskCue> deskCache;
    QDateTime deskCachedAt;

    QJsonObject toJson() const;
    // Unknown keys are ignored; a newer "version" still loads what it can.
    static Config fromJson(const QJsonObject &o);
    bool operator==(const Config &o) const { return toJson() == o.toJson(); }

    // Hand placement edits. place() replaces any earlier one for that cue.
    void place(const DeskCue &cue, ManualPlacement::Mode mode, const QUuid &anchor);
    bool unplace(const QString &list, const QString &number, int part);   // false = none
};

// How a desk cue got where it is.
enum class How { Manual, Fired, Trigger, Order };

struct DeskCell {
    DeskCue cue;                // number/label (a missing cue: what was last known)
    std::vector<DeskCue> parts; // its parts (part > 0), in order
    How     how = How::Order;
    bool    missing = false;    // not on the desk (any more)
    double  atSeconds = -1.0;   // a hit in a song: seconds after GO
    QString triggerName;
    int     deskIndex = -1;     // position in the desk's list; -1 = missing
};

struct Row {
    enum class Kind {
        Quewi,      // a quewi cue (desk cues fired with it alongside)
        Hit,        // a desk cue hit part-way through the quewi cue above
        Desk,       // a desk cue on its own
    };
    Kind  kind = Kind::Quewi;
    int   quewiIndex = -1;      // Quewi: its cue; Hit: the song it's in; Desk: the cue it follows (-1 = top)
    std::vector<DeskCell> desk; // the Lights lane (more lanes = more vectors, later)
};

struct Result {
    std::vector<Row> rows;
    // Placements whose cue the desk renumbered (matched by UID): index into
    // Config::placements and the cue's new number — the caller updates them.
    struct Renumbered { int placement; QString number; int part; };
    std::vector<Renumbered> renumbered;
    std::vector<int> orphaned;  // placements whose quewi cue is gone (placed automatically)
};

// Build the rows. `desk` is the desk list's cues in the desk's order (parts
// included); `deskKnown` = the desk list has been read (live or cached) — only
// then can a cue be called missing.
Result intermesh(const std::vector<QuewiCue> &quewi, const std::vector<DeskCue> &desk,
                 const Config &config, bool deskKnown = true);

// "12.50" → "12.5" (same rule as osc::normalEosNumber; core can't link osc).
QString normalNumber(const QString &n);

} // namespace quewi::core::matrix
