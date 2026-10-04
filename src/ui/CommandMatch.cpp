#include "ui/CommandMatch.h"

#include <algorithm>

namespace quewi::ui {

QList<int> wordStarts(const QString &text)
{
    QList<int> starts;
    for (int i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);
        if (!c.isLetterOrNumber()) continue;
        if (i == 0) { starts << i; continue; }
        const QChar p = text.at(i - 1);
        if (!p.isLetterOrNumber() || (p.isLower() && c.isUpper())) starts << i;
    }
    return starts;
}

namespace {

// Shorter texts win ties within a tier: a query fits a short title better.
int lengthPenalty(const QString &text) { return std::min(int(text.size()), 40) / 4; }

FuzzyResult matchToken(const QString &q, const QString &text, const QString &lower)
{
    FuzzyResult r;
    if (q.isEmpty()) { r.matched = true; return r; }
    const auto starts = wordStarts(text);
    auto isStart = [&](int i) { return starts.contains(i); };

    // 1. Prefix.
    if (lower.startsWith(q)) {
        r.matched = true;
        r.score = 1000 - lengthPenalty(text);
        for (int i = 0; i < q.size(); ++i) r.positions << i;
        return r;
    }

    // 2. Acronym: every query letter starts a successive word.
    {
        QList<int> pos;
        int w = 0;
        for (const QChar qc : q) {
            bool found = false;
            for (; w < starts.size(); ++w)
                if (lower.at(starts[w]) == qc) { pos << starts[w]; ++w; found = true; break; }
            if (!found) break;
        }
        if (pos.size() == q.size() && q.size() >= 2) {
            r.matched = true;
            // Tighter acronyms (no skipped words) and earlier ones win.
            const int skipped = starts.indexOf(pos.last()) - starts.indexOf(pos.first()) - (q.size() - 1);
            r.score = 820 - 15 * skipped - 10 * starts.indexOf(pos.first()) - lengthPenalty(text);
            r.positions = pos;
            return r;
        }
    }

    // 3. The query starts a word; 4. the query appears anywhere.
    {
        int from = 0;
        int anywhere = -1;
        while (true) {
            const int at = lower.indexOf(q, from);
            if (at < 0) break;
            if (anywhere < 0) anywhere = at;
            if (isStart(at)) {
                r.matched = true;
                r.score = 760 - 2 * starts.indexOf(at) - lengthPenalty(text);
                for (int i = 0; i < q.size(); ++i) r.positions << at + i;
                return r;
            }
            from = at + 1;
        }
        if (anywhere >= 0) {
            r.matched = true;
            r.score = 600 - std::min(anywhere, 60) - lengthPenalty(text);
            for (int i = 0; i < q.size(); ++i) r.positions << anywhere + i;
            return r;
        }
    }

    // 5. Subsequence: letters in order with gaps. For each query letter take
    // the next occurrence, but prefer one that starts a word if there's one
    // close by — "nwdio" should light up N-ew, W, and the D-I-O of auDIO.
    {
        QList<int> pos;
        int from = 0;
        for (const QChar qc : q) {
            const int next = lower.indexOf(qc, from);
            if (next < 0) { pos.clear(); break; }
            int chosen = next;
            if (!isStart(next)) {
                for (int i = next + 1; i < lower.size() && i <= next + 12; ++i)
                    if (lower.at(i) == qc && isStart(i)) { chosen = i; break; }
            }
            pos << chosen;
            from = chosen + 1;
        }
        if (pos.size() == q.size()) {
            r.matched = true;
            int wordStartHits = 0, consecutive = 0, gaps = 0;
            for (int i = 0; i < pos.size(); ++i) {
                if (isStart(pos[i])) ++wordStartHits;
                if (i > 0) {
                    if (pos[i] == pos[i - 1] + 1) ++consecutive;
                    else gaps += pos[i] - pos[i - 1] - 1;
                }
            }
            r.score = std::max(1, 300 + 20 * wordStartHits + 10 * consecutive
                                  - 2 * std::min(gaps, 60) - pos.first() - lengthPenalty(text));
            r.positions = pos;
        }
    }
    return r;
}

} // namespace

FuzzyResult fuzzyMatch(const QString &query, const QString &text)
{
    const QString q = query.trimmed().toLower();
    const QString lower = text.toLower();
    if (q.isEmpty()) { FuzzyResult r; r.matched = true; return r; }

    const auto tokens = q.split(QChar(' '), Qt::SkipEmptyParts);
    if (tokens.size() <= 1) return matchToken(q, text, lower);

    // Several words: each must match somewhere (any order). The weakest token
    // sets the tier; a whole-phrase hit on top is best of all.
    FuzzyResult whole = matchToken(q, text, lower);
    FuzzyResult combined;
    combined.matched = true;
    int weakest = 1000, sum = 0;
    for (const auto &t : tokens) {
        const auto one = matchToken(t, text, lower);
        if (!one.matched) { combined.matched = false; break; }
        weakest = std::min(weakest, one.score);
        sum += one.score;
        for (int p : one.positions) if (!combined.positions.contains(p)) combined.positions << p;
    }
    if (!combined.matched) return whole;
    combined.score = (weakest * 2 + sum / tokens.size()) / 3 - 5 * (tokens.size() - 1);
    std::sort(combined.positions.begin(), combined.positions.end());
    if (whole.matched && whole.score > combined.score) return whole;
    return combined;
}

} // namespace quewi::ui
