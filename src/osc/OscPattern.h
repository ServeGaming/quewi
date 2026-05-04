#pragma once

#include <QString>
#include <QStringView>

// OSC address pattern matching per the OSC 1.0 / 1.1 specs.
//
// Patterns support:
//   ?           any single char (within a path part)
//   *           zero or more chars (within a path part)
//   [chars]     character class; supports ranges (a-z) and negation (!abc)
//   {alt1,alt2} alternative match (any of the alternatives)
//   //          OSC 1.1 descendant operator (matches any sequence of path parts)
//
// Path parts are delimited by '/'. The pattern's path-part wildcards never
// cross '/' boundaries (except `//` which is explicitly multi-part).

namespace quewi::osc {

class Pattern {
public:
    // Returns true if `pattern` matches the OSC address `address`.
    // `address` must be a literal address (starts with '/'); `pattern`
    // is the pattern from an incoming message or a subscription.
    static bool matches(QStringView pattern, QStringView address);
};

} // namespace quewi::osc
