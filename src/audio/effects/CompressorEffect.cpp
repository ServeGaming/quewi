#include "audio/effects/CompressorEffect.h"
#include <cmath>
#include <algorithm>

namespace quewi::audio {

CompressorEffect::CompressorEffect(QObject *parent) : AudioEffect(parent) {}

void CompressorEffect::prepare(int sr) {
    m_sampleRate = sr;
    recomputeCoeffs();
    reset();
}

void CompressorEffect::reset() { m_gainEnvDb = 0.f; }

void CompressorEffect::recomputeCoeffs() {
    // One-pole IIR coefficients from time constants
    m_attackCoeff  = std::exp(-1.f / (m_attackMs  * 0.001f * float(m_sampleRate)));
    m_releaseCoeff = std::exp(-1.f / (m_releaseMs * 0.001f * float(m_sampleRate)));
}

float CompressorEffect::computeGainDb(float levelDb) const {
    // Gain computer with soft knee
    float halfKnee = m_kneeDb * 0.5f;
    float over = levelDb - m_threshDb;
    float gr;
    if (over < -halfKnee) {
        gr = 0.f; // below knee: no compression
    } else if (over > halfKnee) {
        gr = over * (1.f / m_ratio - 1.f); // above knee
    } else {
        // inside knee: smooth blend
        float x = (over + halfKnee) / m_kneeDb;
        float slope = (1.f / m_ratio - 1.f);
        gr = slope * x * x * m_kneeDb * 0.5f;
    }
    return gr; // ≤ 0 dB
}

void CompressorEffect::process(float *data, int numFrames) {
    if (!m_enabled) {
        m_currentGrDb.store(0.f, std::memory_order_relaxed);
        return;
    }
    const float makeup = std::pow(10.f, m_makeupDb / 20.f);

    float blockMinGrDb = 0.f; // most reduction seen this block (most negative)
    for (int n = 0; n < numFrames; ++n) {
        float l = data[n*2], r = data[n*2+1];
        // Peak level detection (max of both channels)
        float peak = std::max(std::abs(l), std::abs(r));
        float levelDb = (peak < 1e-9f) ? -180.f : 20.f * std::log10(peak);
        float targetGrDb = computeGainDb(levelDb);

        // Ballistics: attack when gain reduction increases, release when it decreases
        float coeff = (targetGrDb < m_gainEnvDb) ? m_attackCoeff : m_releaseCoeff;
        m_gainEnvDb = m_gainEnvDb * coeff + targetGrDb * (1.f - coeff);

        if (m_gainEnvDb < blockMinGrDb) blockMinGrDb = m_gainEnvDb;

        float gain = std::pow(10.f, m_gainEnvDb / 20.f) * makeup;
        data[n*2]   = l * gain;
        data[n*2+1] = r * gain;
    }
    // Publish the block's peak gain reduction for the visual meter.
    m_currentGrDb.store(blockMinGrDb, std::memory_order_relaxed);
}

float CompressorEffect::transferOutputDb(float inputDb) const {
    // Treat the noise floor as untouched 1:1 so the curve's far-left tail
    // doesn't dive toward the makeup-shifted origin.
    if (inputDb < -90.f) return inputDb + m_makeupDb;
    return inputDb + computeGainDb(inputDb) + m_makeupDb;
}

QStringList CompressorEffect::parameterIds() const {
    return {QStringLiteral("threshold"), QStringLiteral("ratio"),
            QStringLiteral("attack"),    QStringLiteral("release"),
            QStringLiteral("knee"),      QStringLiteral("makeup")};
}

QString CompressorEffect::parameterLabel(const QString &id) const {
    if (id == QLatin1String("threshold")) return QStringLiteral("Threshold (dB)");
    if (id == QLatin1String("ratio"))     return QStringLiteral("Ratio");
    if (id == QLatin1String("attack"))    return QStringLiteral("Attack (ms)");
    if (id == QLatin1String("release"))   return QStringLiteral("Release (ms)");
    if (id == QLatin1String("knee"))      return QStringLiteral("Knee (dB)");
    if (id == QLatin1String("makeup"))    return QStringLiteral("Makeup (dB)");
    return id;
}

float CompressorEffect::parameterValue(const QString &id) const {
    if (id == QLatin1String("threshold")) return m_threshDb;
    if (id == QLatin1String("ratio"))     return m_ratio;
    if (id == QLatin1String("attack"))    return m_attackMs;
    if (id == QLatin1String("release"))   return m_releaseMs;
    if (id == QLatin1String("knee"))      return m_kneeDb;
    if (id == QLatin1String("makeup"))    return m_makeupDb;
    return 0.f;
}

void CompressorEffect::setParameterValue(const QString &id, float v) {
    if (id == QLatin1String("threshold")) m_threshDb  = v;
    else if (id == QLatin1String("ratio"))     m_ratio     = v;
    else if (id == QLatin1String("attack"))    m_attackMs  = v;
    else if (id == QLatin1String("release"))   m_releaseMs = v;
    else if (id == QLatin1String("knee"))      m_kneeDb    = v;
    else if (id == QLatin1String("makeup"))    m_makeupDb  = v;
    else return;
    recomputeCoeffs();
    emit parameterChanged(id, v);
}

QPair<float,float> CompressorEffect::parameterRange(const QString &id) const {
    if (id == QLatin1String("threshold")) return {-60.f, 0.f};
    if (id == QLatin1String("ratio"))     return {1.f,   20.f};
    if (id == QLatin1String("attack"))    return {0.1f,  200.f};
    if (id == QLatin1String("release"))   return {1.f,   2000.f};
    if (id == QLatin1String("knee"))      return {0.f,   12.f};
    if (id == QLatin1String("makeup"))    return {-12.f, 24.f};
    return {0.f, 1.f};
}

float CompressorEffect::parameterDefault(const QString &id) const {
    if (id == QLatin1String("threshold")) return -18.f;
    if (id == QLatin1String("ratio"))     return 4.f;
    if (id == QLatin1String("attack"))    return 10.f;
    if (id == QLatin1String("release"))   return 100.f;
    if (id == QLatin1String("knee"))      return 6.f;
    if (id == QLatin1String("makeup"))    return 0.f;
    return 0.f;
}

int CompressorEffect::parameterDecimals(const QString &id) const {
    if (id == QLatin1String("ratio")) return 1;
    return 1;
}

} // namespace quewi::audio
