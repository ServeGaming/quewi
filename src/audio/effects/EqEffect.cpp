#include "audio/effects/EqEffect.h"
#include <cmath>

#ifndef M_PIf
#define M_PIf 3.14159265358979323846f
#endif

namespace quewi::audio {

static const std::array<float,3> kDefaultFreqs  = {200.f, 1000.f, 8000.f};
static const std::array<float,3> kDefaultQ      = {0.707f, 0.707f, 0.707f};

EqEffect::EqEffect(QObject *parent) : AudioEffect(parent) {
    for (int i = 0; i < kBands; ++i) {
        m_bands[i].freq   = kDefaultFreqs[i];
        m_bands[i].gainDb = 0.f;
        m_bands[i].Q      = kDefaultQ[i];
    }
}

void EqEffect::prepare(int sampleRate) {
    m_sampleRate = sampleRate;
    for (int i = 0; i < kBands; ++i) rebuildCoeffs(i);
    reset();
}

void EqEffect::reset() {
    for (auto &f : m_filtersL) f.reset();
    for (auto &f : m_filtersR) f.reset();
}

void EqEffect::rebuildCoeffs(int i) {
    auto &b = m_bands[i];
    float A     = std::pow(10.f, b.gainDb / 40.f);
    float w0    = 2.f * M_PIf * b.freq / float(m_sampleRate);
    float sinW  = std::sin(w0);
    float cosW  = std::cos(w0);
    float alpha = sinW / (2.f * b.Q);
    float a0    = 1.f + alpha / A;
    m_filtersL[i].b0 = (1.f + alpha * A) / a0;
    m_filtersL[i].b1 = (-2.f * cosW)     / a0;
    m_filtersL[i].b2 = (1.f - alpha * A) / a0;
    m_filtersL[i].a1 = (-2.f * cosW)     / a0;
    m_filtersL[i].a2 = (1.f - alpha / A) / a0;
    m_filtersR[i] = m_filtersL[i];
    m_filtersR[i].reset();
}

void EqEffect::process(float *data, int numFrames) {
    if (!m_enabled) return;
    for (int n = 0; n < numFrames; ++n) {
        float l = data[n*2],   r = data[n*2+1];
        for (int i = 0; i < kBands; ++i) {
            if (std::abs(m_bands[i].gainDb) < 0.01f) continue;
            l = m_filtersL[i].tick(l);
            r = m_filtersR[i].tick(r);
        }
        data[n*2] = l;  data[n*2+1] = r;
    }
}

QStringList EqEffect::parameterIds() const {
    QStringList ids;
    for (int i = 1; i <= kBands; ++i) {
        ids << QStringLiteral("eq%1_freq").arg(i)
            << QStringLiteral("eq%1_gain").arg(i)
            << QStringLiteral("eq%1_q").arg(i);
    }
    return ids;
}

QString EqEffect::parameterLabel(const QString &id) const {
    if (id.endsWith(QLatin1String("_freq"))) return QStringLiteral("Freq (Hz)");
    if (id.endsWith(QLatin1String("_gain"))) return QStringLiteral("Gain (dB)");
    if (id.endsWith(QLatin1String("_q")))    return QStringLiteral("Q");
    return id;
}

float EqEffect::parameterValue(const QString &id) const {
    for (int i = 0; i < kBands; ++i) {
        if (id == QStringLiteral("eq%1_freq").arg(i+1)) return m_bands[i].freq;
        if (id == QStringLiteral("eq%1_gain").arg(i+1)) return m_bands[i].gainDb;
        if (id == QStringLiteral("eq%1_q").arg(i+1))   return m_bands[i].Q;
    }
    return 0.f;
}

void EqEffect::setParameterValue(const QString &id, float v) {
    for (int i = 0; i < kBands; ++i) {
        bool changed = false;
        if (id == QStringLiteral("eq%1_freq").arg(i+1)) { m_bands[i].freq   = v; changed=true; }
        if (id == QStringLiteral("eq%1_gain").arg(i+1)) { m_bands[i].gainDb = v; changed=true; }
        if (id == QStringLiteral("eq%1_q").arg(i+1))   { m_bands[i].Q      = v; changed=true; }
        if (changed) { rebuildCoeffs(i); emit parameterChanged(id, v); return; }
    }
}

QPair<float,float> EqEffect::parameterRange(const QString &id) const {
    if (id.endsWith(QLatin1String("_freq"))) return {20.f,   20000.f};
    if (id.endsWith(QLatin1String("_gain"))) return {-24.f,  24.f};
    if (id.endsWith(QLatin1String("_q")))    return {0.1f,   10.f};
    return {0.f, 1.f};
}

float EqEffect::parameterDefault(const QString &id) const {
    if (id.endsWith(QLatin1String("_gain"))) return 0.f;
    if (id.endsWith(QLatin1String("_q")))    return 0.707f;
    for (int i = 0; i < kBands; ++i)
        if (id == QStringLiteral("eq%1_freq").arg(i+1)) return kDefaultFreqs[i];
    return 0.f;
}

int EqEffect::parameterDecimals(const QString &id) const {
    if (id.endsWith(QLatin1String("_freq"))) return 0;
    if (id.endsWith(QLatin1String("_gain"))) return 1;
    if (id.endsWith(QLatin1String("_q")))    return 2;
    return 1;
}

EqEffect::BandSnapshot EqEffect::bandSnapshot(int i) const {
    if (i < 0 || i >= kBands) return {1000.f, 0.f, 0.707f, false};
    return {m_bands[i].freq, m_bands[i].gainDb, m_bands[i].Q, m_enabled};
}

void EqEffect::setBand(int i, float freq, float gainDb, float Q) {
    if (i < 0 || i >= kBands) return;
    m_bands[i].freq   = freq;
    m_bands[i].gainDb = gainDb;
    m_bands[i].Q      = Q;
    rebuildCoeffs(i);
    emit parameterChanged(QStringLiteral("eq%1_freq").arg(i+1), freq);
    emit parameterChanged(QStringLiteral("eq%1_gain").arg(i+1), gainDb);
    emit parameterChanged(QStringLiteral("eq%1_q").arg(i+1),    Q);
}

// Magnitude response of one biquad at angular frequency w.
// |H(e^{jw})|^2 = |B(e^{jw})|^2 / |A(e^{jw})|^2 with B,A from the biquad.
static float biquadMagDb(const float b0, const float b1, const float b2,
                         const float a1, const float a2, float w)
{
    float cosW = std::cos(w), cos2W = std::cos(2.f*w);
    float sinW = std::sin(w), sin2W = std::sin(2.f*w);
    float numRe = b0 + b1*cosW + b2*cos2W;
    float numIm =      -b1*sinW - b2*sin2W;
    float denRe = 1.f + a1*cosW + a2*cos2W;
    float denIm =      -a1*sinW - a2*sin2W;
    float numMag2 = numRe*numRe + numIm*numIm;
    float denMag2 = denRe*denRe + denIm*denIm;
    if (denMag2 < 1e-20f) return 0.f;
    return 10.f * std::log10(numMag2 / denMag2);
}

float EqEffect::bandResponseDb(int idx, float freq) const {
    if (idx < 0 || idx >= kBands) return 0.f;
    const auto &f = m_filtersL[idx];
    float w = 2.f * M_PIf * freq / float(m_sampleRate);
    return biquadMagDb(f.b0, f.b1, f.b2, f.a1, f.a2, w);
}

float EqEffect::responseDb(float freq) const {
    float total = 0.f;
    for (int i = 0; i < kBands; ++i) total += bandResponseDb(i, freq);
    return total;
}

} // namespace quewi::audio
