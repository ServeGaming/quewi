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
    for (char raw : bytes) {
        const auto b = static_cast<quint8>(raw);

        if (b == kEnd) {
            // END terminates the current frame (if any) and also doubles
            // as a framing-sync byte before the next one.
            if (m_inFrame) {
                if (!m_buf.isEmpty()) frames.push_back(m_buf);
                m_buf.clear();
                m_inFrame = false;
                m_escNext = false;
            } else {
                // Start of a new frame.
                m_inFrame = true;
                m_buf.clear();
                m_escNext = false;
            }
            continue;
        }

        if (!m_inFrame) continue; // junk between frames

        if (m_escNext) {
            m_escNext = false;
            if (b == kEscEnd)      m_buf.append(static_cast<char>(kEnd));
            else if (b == kEscEsc) m_buf.append(static_cast<char>(kEsc));
            else {
                // Protocol violation; drop the frame and resync on next END.
                m_buf.clear();
                m_inFrame = false;
            }
            continue;
        }

        if (b == kEsc) {
            m_escNext = true;
            continue;
        }

        m_buf.append(raw);
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
