#pragma once

#include <QList>
#include <QString>

namespace quewi::ui {

// Fuzzy matching for the command menu. One text, one query, one number.
//
// The score orders results the way a person expects from a launcher:
//   1. the text starts with the query              ("new au" → New Audio)
//   2. the query is an acronym of the words          ("na"     → New Audio)
//   3. the query starts a word                       ("audio"  → New Audio)
//   4. the query appears anywhere                    ("udi"    → New Audio)
//   5. the letters appear in order with gaps         ("nwdio"  → New Audio)
// Within a tier, earlier and tighter matches win, and shorter texts win
// ties (a query that fits a short title fits it better). A query with
// spaces is matched token by token, in any order.
//
// positions are the matched character indexes in `text` (for highlighting).
struct FuzzyResult {
    bool       matched = false;
    int        score = 0;
    QList<int> positions;
};

FuzzyResult fuzzyMatch(const QString &query, const QString &text);

// Letters a word starts with: index 0, after a non-letter/digit, or a capital
// after a lower-case letter ("CueList" → 0, 3). Exposed for the mnemonic
// derivation, which prefers word-start letters.
QList<int> wordStarts(const QString &text);

} // namespace quewi::ui
