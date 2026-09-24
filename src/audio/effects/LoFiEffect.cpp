#include "audio/effects/LoFiEffect.h"
#include <cmath>

namespace quewi::audio {

namespace { enum { Bits, Downsample, Mix }; }

LoFiEffect::LoFiEffect(QObject *parent) : SimpleEffect(parent) {
    addParam(QStringLiteral("bits"),       QStringLiteral("Bits"),       2.f, 16.f, 8.f, 0);
    addParam(QStringLiteral("downsample"), QStringLiteral("Downsample"), 1.f, 32.f, 4.f, 0);
    addParam(QStringLiteral("mix"),        QStringLiteral("Mix"),        0.f, 1.f,  1.f);
}

void LoFiEffect::process(float *data, int numFrames) {
    if (!m_enabled) return;
    // Quantise to 2^bits levels across -1..1.
    const float levels = std::pow(2.f, std::round(p(Bits))) * 0.5f;
    const int   hold   = std::max(1, int(std::lround(p(Downsample))));
    const float mix = p(Mix), dry = 1.f - mix;
    for (int n = 0; n < numFrames; ++n) {
        float &l = data[n * 2], &r = data[n * 2 + 1];
        if (m_counter == 0) {                   // sample-and-hold
            m_holdL = std::round(l * levels) / levels;
            m_holdR = std::round(r * levels) / levels;
        }
        if (++m_counter >= hold) m_counter = 0;
        l = l * dry + m_holdL * mix;
        r = r * dry + m_holdR * mix;
    }
}

} // namespace quewi::audio
