#include "osc/OscPattern.h"

#include <QList>

namespace quewi::osc {

namespace {

// Match a single path part (no '/' allowed inside) against a single
// pattern part. Implements ?, *, [chars], {alt,alt}.
bool matchPart(QStringView pat, QStringView text);

// Match a `[chars]` class starting at pat[i] (the '['). On success, fills
// `consumed` with how many pattern chars were consumed and `matchedOne`
// with whether the first text char (if any) matches.
bool matchClass(QStringView pat, qsizetype i, QChar ch, qsizetype &consumed)
{
    qsizetype j = i + 1;
    bool negate = false;
    if (j < pat.size() && pat[j] == QChar('!')) { negate = true; ++j; }

    bool matched = false;
    while (j < pat.size() && pat[j] != QChar(']')) {
        if (j + 2 < pat.size() && pat[j + 1] == QChar('-') && pat[j + 2] != QChar(']')) {
            // range
            if (ch >= pat[j] && ch <= pat[j + 2]) matched = true;
            j += 3;
        } else {
            if (ch == pat[j]) matched = true;
            ++j;
        }
    }
    if (j >= pat.size()) { consumed = 0; return false; } // no closing ]
    consumed = j - i + 1;
    return negate ? !matched : matched;
}

// Match `{alt1,alt2,...}` starting at pat[i] (the '{'). The alternatives
// are matched against the prefix of `text` and the longest successful
// alternative is taken. Fills consumed with pattern chars and textConsumed
// with text chars.
bool matchAlternatives(QStringView pat, qsizetype i, QStringView text,
                       qsizetype &consumed, qsizetype &textConsumed)
{
    qsizetype end = i + 1;
    int depth = 1;
    while (end < pat.size() && depth > 0) {
        if (pat[end] == QChar('{')) ++depth;
        else if (pat[end] == QChar('}')) --depth;
        if (depth > 0) ++end;
    }
    if (depth != 0) { consumed = 0; textConsumed = 0; return false; }

    QList<QStringView> alts;
    qsizetype start = i + 1;
    int d = 0;
    for (qsizetype k = start; k < end; ++k) {
        if (pat[k] == QChar('{')) ++d;
        else if (pat[k] == QChar('}')) --d;
        else if (d == 0 && pat[k] == QChar(',')) {
            alts.push_back(pat.mid(start, k - start));
            start = k + 1;
        }
    }
    alts.push_back(pat.mid(start, end - start));

    for (const auto &alt : alts) {
        if (alt.size() > text.size()) continue;
        if (text.left(alt.size()) == alt) {
            consumed = end - i + 1;
            textConsumed = alt.size();
            return true;
        }
    }
    consumed = 0;
    textConsumed = 0;
    return false;
}

bool matchPart(QStringView pat, QStringView text)
{
    // Recursive descent with backtracking on '*'. Sufficient for OSC's
    // pattern complexity; address parts are short.
    qsizetype pi = 0;
    qsizetype ti = 0;
    qsizetype starPi = -1;
    qsizetype starTi = -1;

    while (ti <= text.size()) {
        if (pi < pat.size()) {
            const QChar c = pat[pi];
            if (c == QChar('*')) {
                starPi = ++pi;
                starTi = ti;
                continue;
            }
            if (c == QChar('?')) {
                if (ti < text.size()) { ++pi; ++ti; continue; }
            } else if (c == QChar('[')) {
                if (ti < text.size()) {
                    qsizetype consumed = 0;
                    if (matchClass(pat, pi, text[ti], consumed)) {
                        pi += consumed;
                        ++ti;
                        continue;
                    }
                }
            } else if (c == QChar('{')) {
                qsizetype patConsumed = 0;
                qsizetype textConsumed = 0;
                if (matchAlternatives(pat, pi, text.mid(ti), patConsumed, textConsumed)) {
                    pi += patConsumed;
                    ti += textConsumed;
                    continue;
                }
            } else {
                if (ti < text.size() && c == text[ti]) {
                    ++pi; ++ti;
                    continue;
                }
            }
        } else if (ti == text.size()) {
            return true;
        }

        if (starPi >= 0 && starTi < text.size()) {
            pi = starPi;
            ti = ++starTi;
            continue;
        }
        return false;
    }
    return pi >= pat.size();
}

} // namespace

bool Pattern::matches(QStringView pattern, QStringView address)
{
    if (address.isEmpty() || address[0] != QChar('/')) return false;

    // Tokenize on '/' but track empty tokens because OSC 1.1's `//` is
    // signaled by an empty pattern part (e.g., "/foo//bar" → ["", "foo", "", "bar"]).
    auto split = [](QStringView s) {
        QList<QStringView> parts;
        qsizetype start = 0;
        for (qsizetype i = 0; i < s.size(); ++i) {
            if (s[i] == QChar('/')) {
                parts.push_back(s.mid(start, i - start));
                start = i + 1;
            }
        }
        parts.push_back(s.mid(start));
        return parts;
    };

    const auto patParts  = split(pattern);
    const auto addrParts = split(address);

    // matchFrom: try to match patParts[pi..] against addrParts[ai..]
    auto matchFrom = [&](auto &self, qsizetype pi, qsizetype ai) -> bool {
        // Descendant operator: "//" produces an empty part between two slashes.
        // Skip the empty part and allow ai to advance through any number of
        // address parts.
        if (pi < patParts.size() && patParts[pi].isEmpty() && pi != 0
            && pi != patParts.size() - 1) {
            for (qsizetype skip = 0; ai + skip <= addrParts.size(); ++skip) {
                if (self(self, pi + 1, ai + skip)) return true;
            }
            return false;
        }
        if (pi == patParts.size()) return ai == addrParts.size();
        if (ai == addrParts.size()) return false;
        if (!matchPart(patParts[pi], addrParts[ai])) return false;
        return self(self, pi + 1, ai + 1);
    };
    return matchFrom(matchFrom, 0, 0);
}

} // namespace quewi::osc
