#pragma once

// Reading a TheatreMix show file (.tmix) into quewi Mix.
//
// A .tmix is an SQLite database. What we take from it:
//   cues      number + point (2 / 5 -> 2.5), name, and per DCA the channels on
//             it ("dca01Channels" = "8,2") and its label ("dca01Label").
//             However many dcaNN columns the file has (8 in old files, 12+ in
//             newer ones) are read.
//   profiles  a channel's default row names it ("Elphaba").
//   actors    who's on a channel, when the file has them.
//   ensembles named groups of channels.
//   config    targetConsole, the DCAs and channels the show uses.
// Everything else (positions, FX, snippets, scenes, levels) has no home in
// quewi Mix yet and is left behind; the summary says so.
//
// readTmix() only reads; applyTmix() puts the result into a workspace.

#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

namespace quewi::core { class Workspace; class CueList; }

namespace quewi::mix {

struct TmixCue {
    double              number = 0;
    QString             name;
    QHash<int, QSet<int>> dcas;      // 1-based DCA -> strips
    QHash<int, QString>   labels;    // 1-based DCA -> TheatreMix's label
};

struct TmixShow {
    QString                 console;        // config targetConsole, e.g. "X32"
    QVector<TmixCue>        cues;           // in cue-number order
    QHash<int, QString>     channelNames;   // strip -> name
    QHash<int, QString>     actors;         // strip -> actor
    QHash<QString, QSet<int>> ensembles;
    QSet<int>               strips;         // every strip the show uses
    int                     highestDca = 0; // highest DCA with channels on it
};

// Reads `path`. On failure returns false and says why in plain words.
bool readTmix(const QString &path, TmixShow *out, QString *error);

struct TmixApplied {
    int cues = 0;
    int channelsAdded = 0;      // strips quewi didn't have yet
    int channelsNamed = 0;      // existing unnamed strips given a name
    int ensembles = 0;
    int dcasDropped = 0;        // assignments past quewi's 24 DCAs
    int dcaCount = 0;           // the show's DCA count afterwards
};

// Appends the cues to `list` (a Mix list) as one undo step, and fills in the
// workspace's mix show: strips it doesn't have, names for unnamed ones,
// ensembles it doesn't have, and a DCA count big enough for the cues.
// Existing names and ensembles are never overwritten.
TmixApplied applyTmix(const TmixShow &show, core::Workspace *ws, core::CueList *list);

} // namespace quewi::mix
