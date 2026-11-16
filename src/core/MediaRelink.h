#pragma once

#include <QString>
#include <QStringList>

namespace quewi::core {

class Workspace;

// A show moved to another computer: its cues still point at where the
// tracks were on the old one. These find the cues whose file isn't there
// (Matthew, 2026-11-15: "when I switch computers with the same tracks they
// won't be linked — do we have a bulk change feature for track locations").

// Cues (any type with a "filePath" field: audio, video, image) whose file
// is set but isn't on this computer. One entry per cue.
struct MissingMedia {
    int         count = 0;        // cues
    QStringList paths;            // distinct missing paths, in cue order
};
MissingMedia missingMedia(const Workspace *ws);

struct RelinkResult {
    int         relinked = 0;     // cues pointed at a file that was found
    QStringList stillMissing;     // distinct paths with no match in the folder
    QStringList ambiguous;        // file names found more than once (the first, in path order, was used)
};

// Look in `folder` (and the folders inside it) for each missing file, by
// file name (case-insensitive), and point the cue at it. Where the folder
// holds the same name more than once, the one whose parent folders best
// match the old path wins. One undo step ("Relink N files"). Cues whose
// file exists are left alone.
RelinkResult relinkMissingMedia(Workspace *ws, const QString &folder);

} // namespace quewi::core
