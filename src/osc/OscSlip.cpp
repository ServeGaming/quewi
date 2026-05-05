#include "osc/OscSlip.h"

namespace quewi::osc {

namespace {
constexpr quint8 kEnd     = 0xC0;
constexpr quint8 kEsc     = 0xDB;
constexpr quint8 kEscEnd  = 0xDC;
constexpr quint8 kEscEsc  = 0xDD;
} // namespace

QByteArray SlipEncoder::encode(const QByteArray &packet)
{
    QByteArray out;
    out.reserve(packet.size() + 4);
    out.append(static_cast<char>(kEnd));
    for (char raw : packet) {
        const auto b = static_cast<quint8>(raw);
        if (b == kEnd) {
            out.append(static_cast<char>(kEsc));
            out.append(static_cast<char>(kEscEnd));
        } else if (b == kEsc) {
            out.append(static_cast<char>(kEsc));
            out.append(static_cast<char>(kEscEsc));
        } else {
            out.append(raw);
        }
    }
    out.append(static_cast<char>(kEnd));
    return out;
}

std::vector<QByteArray> SlipDecoder::feed(const QByteArray &bytes)
{
    std::vector<QByteArray> frames;
    auto appendOrOverflow = [this](char c) {
        if (m_overflow) return;
        if (m_buf.size() >= kMaxFrameBytes) {
            // Frame too large; mark overflow so the rest of the frame
            // is discarded until the next END resyncs us. Prevents an
            // unbounded SLIP frame from exhausting memory.
            m_overflow = true;
            m_buf.clear();
            return;
        }
        m_buf.append(c);
    };

    for (char raw : bytes) {
        const auto b = static_cast<quint8>(raw);

        if (b == kEnd) {
            // END terminates the current frame (if any) and also doubles
            // as a framing-sync byte before the next one.
            if (m_inFrame) {
                if (!m_overflow && !m_buf.isEmpty()) frames.push_back(m_buf);
                m_buf.clear();
                m_inFrame = false;
                m_escNext = false;
                m_overflow = false;
            } else {
                m_inFrame = true;
                m_buf.clear();
                m_escNext = false;
                m_overflow = false;
            }
            continue;
        }

        if (!m_inFrame) continue; // junk between frames

        if (m_escNext) {
            m_escNext = false;
            if (b == kEscEnd)      appendOrOverflow(static_cast<char>(kEnd));
            else if (b == kEscEsc) appendOrOverflow(static_cast<char>(kEsc));
            else {
                // Protocol violation; drop the frame and resync on next END.
                m_buf.clear();
                m_inFrame = false;
                m_overflow = false;
            }
            continue;
        }

        if (b == kEsc) {
            m_escNext = true;
            continue;
        }

        appendOrOverflow(raw);
    }
    return frames;
}

void SlipDecoder::reset()
{
    m_buf.clear();
    m_inFrame = false;
    m_escNext = false;
}

} // namespace quewi::osc
