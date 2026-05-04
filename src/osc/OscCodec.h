#pragma once

#include "osc/OscMessage.h"

#include <QByteArray>
#include <optional>

// Pure-function OSC codec. All encode functions append to a QByteArray;
// all decode functions take a byte buffer and a cursor and either fill
// out the target or return false.
//
// All numeric types are encoded big-endian per OSC spec. Strings and
// blobs are padded to 4-byte boundaries with NUL bytes.
//
// Coverage: every type tag listed in OscMessage.h, plus arbitrarily
// nested bundles.

namespace quewi::osc {

class Codec {
public:
    // Encode a complete OSC packet (Message or Bundle).
    static QByteArray encode(const Message &m);
    static QByteArray encode(const Bundle &b);
    static QByteArray encode(const Element &e);

    // Decode a complete OSC packet. Returns nullopt on parse failure
    // (truncated input, bad type tag, malformed bundle).
    static std::optional<Element> decode(const QByteArray &bytes);

    // Lower-level helpers, exposed for tests.
    static void appendString(QByteArray &out, QStringView s);
    static void appendBlob(QByteArray &out, const QByteArray &b);
    static int  paddedLength(int rawLength); // rounds up to multiple of 4
};

} // namespace quewi::osc
