#include "audio/effects/TremoloEffect.h"
#include <cmath>

namespace quewi::audio {

namespace {
enum { Rate, Depth, Stereo };
constexpr double kTwoPi = 6.283185307179586;
}

TremoloEffect::TremoloEffect(QObject *parent) : SimpleEffect(parent) {
    addParam(QStringLiteral("rate"),   QStringLiteral("Rate (Hz)"), 0.1f, 20.f, 5.f, 1);
    addParam(QStringLiteral("depth"),  QStringLiteral("Depth"),     0.f,  1.f,  0.5f);
    addParam(QStringLiteral("stereo"), QStringLiteral("Stereo"),    0.f,  1.f,  0.f);
}

void TremoloEffect::process(float *data, int numFrames) {
    if (!m_enabled) return;
    const double inc   = kTwoPi * double(p(Rate)) / double(m_sampleRate);
    const float  depth = p(Depth);
    const double offR  = double(p(Stereo)) * kTwoPi * 0.5;   // up to 180°
    for (int n = 0; n < numFrames; ++n) {
        // Gain swings between 1 and 1 - depth.
        const float gL = 1.f - depth * float(0.5 + 0.5 * std::sin(m_phase));
        const float gR = 1.f - depth * float(0.5 + 0.5 * std::sin(m_phase + offR));
        data[n * 2]     *= gL;
        data[n * 2 + 1] *= gR;
        m_phase += inc;
        if (m_phase >= kTwoPi) m_phase -= kTwoPi;
    }
}

} // namespace quewi::audio
