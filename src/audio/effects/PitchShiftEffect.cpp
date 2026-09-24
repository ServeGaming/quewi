#include "audio/effects/PitchShiftEffect.h"
#include <cmath>

namespace quewi::audio {

namespace {
enum { Semitones, Mix };
constexpr float kPi = 3.14159265f;
}

PitchShiftEffect::PitchShiftEffect(QObject *parent) : SimpleEffect(parent) {
    addParam(QStringLiteral("semitones"), QStringLiteral("Pitch (semitones)"),
             -12.f, 12.f, -5.f, 1);
    addParam(QStringLiteral("mix"), QStringLiteral("Mix"), 0.f, 1.f, 1.f);
    // Buffers exist before prepare() so process() can never index an empty
    // vector (same guard as DelayEffect).
    prepare(m_sampleRate);
}

void PitchShiftEffect::prepare(int sampleRate) {
    m_sampleRate = sampleRate > 0 ? sampleRate : 48000;
    m_window = std::max(64, m_sampleRate / 20);      // 50 ms
    m_bufL.assign(size_t(m_window) * 2 + 4, 0.f);
    m_bufR.assign(m_bufL.size(), 0.f);
    reset();
}

void PitchShiftEffect::reset() {
    std::fill(m_bufL.begin(), m_bufL.end(), 0.f);
    std::fill(m_bufR.begin(), m_bufR.end(), 0.f);
    m_write = 0;
    m_phase = 0.f;
}

float PitchShiftEffect::readTap(const std::vector<float> &buf, float delay) const {
    const int size = int(buf.size());
    float pos = float(m_write) - 1.f - delay;
    while (pos < 0.f) pos += float(size);
    const int   i0 = int(pos) % size;
    const int   i1 = (i0 + 1) % size;
    const float f  = pos - std::floor(pos);
    return buf[size_t(i0)] + (buf[size_t(i1)] - buf[size_t(i0)]) * f;
}

void PitchShiftEffect::process(float *data, int numFrames) {
    if (!m_enabled) return;
    const float semis = p(Semitones);
    if (std::abs(semis) < 0.01f) return;              // unity: stay transparent
    const float ratio = std::pow(2.f, semis / 12.f);
    // The delay under each tap changes by (1 - ratio) samples per sample; as
    // a fraction of the window that's the phase step.
    const float step = (1.f - ratio) / float(m_window);
    const float mix = p(Mix), dry = 1.f - mix;
    const int size = int(m_bufL.size());
    const float win = float(m_window);

    for (int n = 0; n < numFrames; ++n) {
        float &l = data[n * 2], &r = data[n * 2 + 1];
        m_bufL[size_t(m_write)] = l;
        m_bufR[size_t(m_write)] = r;
        if (++m_write >= size) m_write = 0;

        const float phA = m_phase;
        float phB = m_phase + 0.5f;
        if (phB >= 1.f) phB -= 1.f;
        // sin² windows on taps half a window apart sum to exactly 1.
        const float gA = std::sin(kPi * phA), gB = std::sin(kPi * phB);
        const float wA = gA * gA, wB = gB * gB;
        const float dA = phA * win, dB = phB * win;
        const float outL = readTap(m_bufL, dA) * wA + readTap(m_bufL, dB) * wB;
        const float outR = readTap(m_bufR, dA) * wA + readTap(m_bufR, dB) * wB;

        m_phase += step;
        if (m_phase >= 1.f) m_phase -= 1.f;
        if (m_phase < 0.f)  m_phase += 1.f;

        l = l * dry + outL * mix;
        r = r * dry + outR * mix;
    }
}

} // namespace quewi::audio
