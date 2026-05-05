#pragma once

#include <QByteArray>
#include <vector>

// SLIP framing per RFC 1055 — used to delimit OSC packets on a stream
// transport (TCP). Each frame is bracketed by END (0xC0); ESC (0xDB)
// escapes literal END/ESC bytes inside the payload.
//
// Encoding:
//   END   → ESC ESC_END   (DB DC)
//   ESC   → ESC ESC_ESC   (DB DD)
//   any   → as-is
//   then surrounded by END...END
//
// Decoder is streaming: feed any chunk of bytes, get back zero or more
// fully-framed packets. Holds state across calls so partial frames
// straddling chunk boundaries are reassembled.

namespace quewi::osc {

class SlipEncoder {
public:
    // Wrap one OSC packet's bytes in a SLIP frame.
    static QByteArray encode(const QByteArray &packet);
};

class SlipDecoder {
public:
    SlipDecoder() = default;

    // Feed received bytes. Returns any complete frames extracted.
    // Trailing partial-frame bytes are buffered for the next call.
    // Frames larger than `maxFrameBytes` are silently dropped — a
    // remote peer (or noisy line) can otherwise grow the buffer
    // unbounded. 1 MiB is well above any sane OSC packet (the 1.0
    // spec suggests they should fit in a single UDP datagram).
    std::vector<QByteArray> feed(const QByteArray &bytes);

    void reset();

    static constexpr int kMaxFrameBytes = 1 * 1024 * 1024;

private:
    QByteArray m_buf;
    bool m_inFrame = false;
    bool m_escNext = false;
    bool m_overflow = false;  // current frame exceeded the cap; drop until next END
};

} // namespace quewi::osc
