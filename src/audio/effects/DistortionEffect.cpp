#include "audio/effects/DistortionEffect.h"
#include <cmath>

namespace quewi::audio {

namespace { enum { Drive, Tone, Mix, Output }; }

DistortionEffect::DistortionEffect(QObject *parent) : SimpleEffect(parent) {
    addParam(QStringLiteral("drive"),  QStringLiteral("Drive"),       0.f,   1.f, 0.35f);
    addParam(QStringLiteral("tone"),   QStringLiteral("Tone"),        0.f,   1.f, 0.6f);
    addParam(QStringLiteral("mix"),    QStringLiteral("Mix"),         0.f,   1.f, 1.f);
    addParam(QStringLiteral("output"), QStringLiteral("Output (dB)"), -24.f, 6.f, 0.f, 1);
    updateCoeffs();
}

void DistortionEffect::prepare(int sampleRate) {
    m_sampleRate = sampleRate > 0 ? sampleRate : 48000;
    updateCoeffs();
    reset();
}

void DistortionEffect::reset() { m_lpL = m_lpR = 0.f; }

void DistortionEffect::updateCoeffs() {
    // Drive 0..1 → pre-gain 1..40 (exponential feels even on a slider).
    m_gain = std::pow(40.f, p(Drive));
    // Normalise so a full-scale input still peaks at full scale.
    m_norm = 1.f / std::tanh(m_gain);
    // Tone 0..1 → one-pole low-pass 1 kHz..14 kHz on the clipped signal.
    const float fc = 1000.f * std::pow(14.f, p(Tone));
    m_toneA = std::exp(-2.f * 3.14159265f * fc / float(m_sampleRate));
    m_outGain = std::pow(10.f, p(Output) / 20.f);
}

void DistortionEffect::process(float *data, int numFrames) {
    if (!m_enabled) return;
    const float mix = p(Mix), dry = 1.f - mix;
    for (int n = 0; n < numFrames; ++n) {
        float &l = data[n * 2], &r = data[n * 2 + 1];
        const float wl = std::tanh(l * m_gain) * m_norm;
        const float wr = std::tanh(r * m_gain) * m_norm;
        m_lpL = wl + m_toneA * (m_lpL - wl);
        m_lpR = wr + m_toneA * (m_lpR - wr);
        l = (l * dry + m_lpL * mix) * m_outGain;
        r = (r * dry + m_lpR * mix) * m_outGain;
    }
}

} // namespace quewi::audio
