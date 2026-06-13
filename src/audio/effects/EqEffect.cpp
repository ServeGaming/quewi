#include "audio/effects/EqEffect.h"
#include <cmath>

#ifndef M_PIf
#define M_PIf 3.14159265358979323846f
#endif

namespace quewi::audio {

static const std::array<float,6> kDefaultFreqs = {
    80.f, 200.f, 700.f, 2500.f, 8000.f, 12000.f
};
static const std::array<EqEffect::FilterType,6> kDefaultTypes = {
    EqEffect::LowShelf, EqEffect::Peaking, EqEffect::Peaking,
    EqEffect::Peaking,  EqEffect::Peaking, EqEffect::HighShelf
};

EqEffect::EqEffect(QObject *parent) : AudioEffect(parent) {
    for (int i = 0; i < kBands; ++i) {
        m_bands[i].freq    = kDefaultFreqs[i];
        m_bands[i].gainDb  = 0.f;
        m_bands[i].Q       = 0.707f;
        m_bands[i].type    = kDefaultTypes[i];
        m_bands[i].enabled = true;
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
    const float A     = std::pow(10.f, b.gainDb / 40.f);
    const float w0    = 2.f * M_PIf * b.freq / float(m_sampleRate);
    const float sinW  = std::sin(w0);
    const float cosW  = std::cos(w0);
    const float alpha = sinW / (2.f * std::max(0.1f, b.Q));

    float b0=1, b1=0, b2=0, a0=1, a1=0, a2=0;

    // RBJ Audio EQ Cookbook formulas.
    switch (b.type) {
    case Peaking: {
        b0 = 1.f + alpha * A;
        b1 = -2.f * cosW;
        b2 = 1.f - alpha * A;
        a0 = 1.f + alpha / A;
        a1 = -2.f * cosW;
        a2 = 1.f - alpha / A;
        break;
    }
    case LowShelf: {
        const float sqrtA = std::sqrt(A);
        const float twoSqrtAalpha = 2.f * sqrtA * alpha;
        b0 =      A * ((A + 1.f) - (A - 1.f) * cosW + twoSqrtAalpha);
        b1 = 2.f * A * ((A - 1.f) - (A + 1.f) * cosW);
        b2 =      A * ((A + 1.f) - (A - 1.f) * cosW - twoSqrtAalpha);
        a0 =          (A + 1.f) + (A - 1.f) * cosW + twoSqrtAalpha;
        a1 =  -2.f * ((A - 1.f) + (A + 1.f) * cosW);
        a2 =          (A + 1.f) + (A - 1.f) * cosW - twoSqrtAalpha;
        break;
    }
    case HighShelf: {
        const float sqrtA = std::sqrt(A);
        const float twoSqrtAalpha = 2.f * sqrtA * alpha;
        b0 =       A * ((A + 1.f) + (A - 1.f) * cosW + twoSqrtAalpha);
        b1 = -2.f * A * ((A - 1.f) + (A + 1.f) * cosW);
        b2 =       A * ((A + 1.f) + (A - 1.f) * cosW - twoSqrtAalpha);
        a0 =          (A + 1.f) - (A - 1.f) * cosW + twoSqrtAalpha;
        a1 =   2.f * ((A - 1.f) - (A + 1.f) * cosW);
        a2 =          (A + 1.f) - (A - 1.f) * cosW - twoSqrtAalpha;
        break;
    }
    case LowPass: {
        b0 = (1.f - cosW) * 0.5f;
        b1 = 1.f - cosW;
        b2 = (1.f - cosW) * 0.5f;
        a0 = 1.f + alpha;
        a1 = -2.f * cosW;
        a2 = 1.f - alpha;
        break;
    }
    case HighPass: {
        b0 = (1.f + cosW) * 0.5f;
        b1 = -(1.f + cosW);
        b2 = (1.f + cosW) * 0.5f;
        a0 = 1.f + alpha;
        a1 = -2.f * cosW;
        a2 = 1.f - alpha;
        break;
    }
    }

    // Update coefficients only — do NOT clear the biquad delay-line
    // state here. rebuildCoeffs runs on every parameter change (knob
    // drag), and zeroing x1/x2/y1/y2 mid-stream produces an audible
    // click on each update. State is cleared where it should be: in
    // prepare() and reset(). Copying L→R would also copy L's history
    // into R, so assign coefficients field-by-field and leave each
    // channel's running state intact.
    m_filtersL[i].b0 = b0 / a0;
    m_filtersL[i].b1 = b1 / a0;
    m_filtersL[i].b2 = b2 / a0;
    m_filtersL[i].a1 = a1 / a0;
    m_filtersL[i].a2 = a2 / a0;
    m_filtersR[i].b0 = m_filtersL[i].b0;
    m_filtersR[i].b1 = m_filtersL[i].b1;
    m_filtersR[i].b2 = m_filtersL[i].b2;
    m_filtersR[i].a1 = m_filtersL[i].a1;
    m_filtersR[i].a2 = m_filtersL[i].a2;
}

void EqEffect::process(float *data, int numFrames) {
    if (!m_enabled) return;
    for (int n = 0; n < numFrames; ++n) {
        float l = data[n*2], r = data[n*2+1];
        for (int i = 0; i < kBands; ++i) {
            if (!m_bands[i].enabled) continue;
            // Peaking/shelves at exactly 0 dB are inert; skip the multiply.
            if ((m_bands[i].type == Peaking || m_bands[i].type == LowShelf
                 || m_bands[i].type == HighShelf)
                && std::abs(m_bands[i].gainDb) < 0.01f) continue;
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
            << QStringLiteral("eq%1_q").arg(i)
            << QStringLiteral("eq%1_type").arg(i)
            << QStringLiteral("eq%1_enabled").arg(i);
    }
    return ids;
}

QString EqEffect::parameterLabel(const QString &id) const {
    if (id.endsWith(QLatin1String("_freq")))    return QStringLiteral("Freq (Hz)");
    if (id.endsWith(QLatin1String("_gain")))    return QStringLiteral("Gain (dB)");
    if (id.endsWith(QLatin1String("_q")))       return QStringLiteral("Q");
    if (id.endsWith(QLatin1String("_type")))    return QStringLiteral("Type");
    if (id.endsWith(QLatin1String("_enabled"))) return QStringLiteral("On");
    return id;
}

float EqEffect::parameterValue(const QString &id) const {
    for (int i = 0; i < kBands; ++i) {
        if (id == QStringLiteral("eq%1_freq").arg(i+1))    return m_bands[i].freq;
        if (id == QStringLiteral("eq%1_gain").arg(i+1))    return m_bands[i].gainDb;
        if (id == QStringLiteral("eq%1_q").arg(i+1))       return m_bands[i].Q;
        if (id == QStringLiteral("eq%1_type").arg(i+1))    return float(int(m_bands[i].type));
        if (id == QStringLiteral("eq%1_enabled").arg(i+1)) return m_bands[i].enabled ? 1.f : 0.f;
    }
    return 0.f;
}

void EqEffect::setParameterValue(const QString &id, float v) {
    for (int i = 0; i < kBands; ++i) {
        bool changed = false;
        if (id == QStringLiteral("eq%1_freq").arg(i+1)) { m_bands[i].freq   = v; changed=true; }
        else if (id == QStringLiteral("eq%1_gain").arg(i+1)) { m_bands[i].gainDb = v; changed=true; }
        else if (id == QStringLiteral("eq%1_q").arg(i+1))   { m_bands[i].Q      = v; changed=true; }
        else if (id == QStringLiteral("eq%1_type").arg(i+1)) {
            const int t = std::clamp(int(v), 0, 4);
            m_bands[i].type = FilterType(t); changed=true;
        }
        else if (id == QStringLiteral("eq%1_enabled").arg(i+1)) {
            m_bands[i].enabled = (v >= 0.5f); changed=true;
        }
        if (changed) { rebuildCoeffs(i); emit parameterChanged(id, v); return; }
    }
}

QPair<float,float> EqEffect::parameterRange(const QString &id) const {
    if (id.endsWith(QLatin1String("_freq")))    return {20.f,    20000.f};
    if (id.endsWith(QLatin1String("_gain")))    return {-24.f,   24.f};
    if (id.endsWith(QLatin1String("_q")))       return {0.1f,    10.f};
    if (id.endsWith(QLatin1String("_type")))    return {0.f,     4.f};
    if (id.endsWith(QLatin1String("_enabled"))) return {0.f,     1.f};
    return {0.f, 1.f};
}

float EqEffect::parameterDefault(const QString &id) const {
    if (id.endsWith(QLatin1String("_gain")))    return 0.f;
    if (id.endsWith(QLatin1String("_q")))       return 0.707f;
    if (id.endsWith(QLatin1String("_enabled"))) return 1.f;
    if (id.endsWith(QLatin1String("_type"))) {
        for (int i = 0; i < kBands; ++i)
            if (id == QStringLiteral("eq%1_type").arg(i+1)) return float(int(kDefaultTypes[i]));
    }
    for (int i = 0; i < kBands; ++i)
        if (id == QStringLiteral("eq%1_freq").arg(i+1)) return kDefaultFreqs[i];
    return 0.f;
}

int EqEffect::parameterDecimals(const QString &id) const {
    if (id.endsWith(QLatin1String("_freq")))    return 0;
    if (id.endsWith(QLatin1String("_gain")))    return 1;
    if (id.endsWith(QLatin1String("_q")))       return 2;
    if (id.endsWith(QLatin1String("_type")))    return 0;
    if (id.endsWith(QLatin1String("_enabled"))) return 0;
    return 1;
}

EqEffect::BandSnapshot EqEffect::bandSnapshot(int i) const {
    if (i < 0 || i >= kBands) return {1000.f, 0.f, 0.707f, Peaking, false};
    return {m_bands[i].freq, m_bands[i].gainDb, m_bands[i].Q,
            m_bands[i].type, m_bands[i].enabled};
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

void EqEffect::setBandType(int i, FilterType t) {
    if (i < 0 || i >= kBands) return;
    if (m_bands[i].type == t) return;
    m_bands[i].type = t;
    rebuildCoeffs(i);
    emit parameterChanged(QStringLiteral("eq%1_type").arg(i+1), float(int(t)));
}

void EqEffect::setBandEnabled(int i, bool on) {
    if (i < 0 || i >= kBands) return;
    if (m_bands[i].enabled == on) return;
    m_bands[i].enabled = on;
    emit parameterChanged(QStringLiteral("eq%1_enabled").arg(i+1), on ? 1.f : 0.f);
}

// Magnitude response of one biquad at angular frequency w.
static float biquadMagDb(float b0, float b1, float b2,
                         float a1, float a2, float w)
{
    const float cosW = std::cos(w),  cos2W = std::cos(2.f*w);
    const float sinW = std::sin(w),  sin2W = std::sin(2.f*w);
    const float numRe = b0 + b1*cosW + b2*cos2W;
    const float numIm =      -b1*sinW - b2*sin2W;
    const float denRe = 1.f + a1*cosW + a2*cos2W;
    const float denIm =      -a1*sinW - a2*sin2W;
    const float numMag2 = numRe*numRe + numIm*numIm;
    const float denMag2 = denRe*denRe + denIm*denIm;
    if (denMag2 < 1e-20f) return 0.f;
    return 10.f * std::log10(numMag2 / denMag2);
}

float EqEffect::bandResponseDb(int idx, float freq) const {
    if (idx < 0 || idx >= kBands) return 0.f;
    const auto &f = m_filtersL[idx];
    const float w = 2.f * M_PIf * freq / float(m_sampleRate);
    return biquadMagDb(f.b0, f.b1, f.b2, f.a1, f.a2, w);
}

float EqEffect::responseDb(float freq) const {
    float total = 0.f;
    for (int i = 0; i < kBands; ++i) {
        if (!m_bands[i].enabled) continue;
        total += bandResponseDb(i, freq);
    }
    return total;
}

} // namespace quewi::audio
