#include "audio/effects/DelayEffect.h"
#include <cmath>
#include <algorithm>

namespace quewi::audio {

DelayEffect::DelayEffect(QObject *parent) : AudioEffect(parent) {
    // Allocate the delay buffers up-front so process() — which indexes them and
    // does `% size` — can't read out of bounds or divide by zero if it runs
    // before prepare() (e.g. the effect is added to an already-playing chain).
    // prepare() re-runs at the real sample rate when playback (re)starts.
    prepare(m_sampleRate);
}

void DelayEffect::prepare(int sr) {
    m_sampleRate = sr;
    rebuildBuffers();
    reset();
}

void DelayEffect::rebuildBuffers() {
    int maxSamples = int(kMaxDelayMs * 0.001f * float(m_sampleRate)) + 1;
    m_bufL.assign(maxSamples, 0.f);
    m_bufR.assign(maxSamples, 0.f);
    m_delaySampL = std::clamp(int(m_timeLMs * 0.001f * float(m_sampleRate)), 1, maxSamples-1);
    m_delaySampR = std::clamp(int(m_timeRMs * 0.001f * float(m_sampleRate)), 1, maxSamples-1);
}

void DelayEffect::reset() {
    m_bufL.assign(m_bufL.size(), 0.f);
    m_bufR.assign(m_bufR.size(), 0.f);
    m_writeL = m_writeR = 0;
}

void DelayEffect::process(float *data, int numFrames) {
    if (!m_enabled) return;
    int szL = int(m_bufL.size()), szR = int(m_bufR.size());

    for (int n = 0; n < numFrames; ++n) {
        float inL = data[n*2], inR = data[n*2+1];

        // Read delayed signal
        int rL = (m_writeL - m_delaySampL + szL) % szL;
        int rR = (m_writeR - m_delaySampR + szR) % szR;
        float delL = m_bufL[rL];
        float delR = m_bufR[rR];

        // Write input + feedback (ping-pong: L feeds into R buffer)
        m_bufL[m_writeL] = inL + delR * m_feedback;
        m_bufR[m_writeR] = inR + delL * m_feedback;
        if (++m_writeL >= szL) m_writeL = 0;
        if (++m_writeR >= szR) m_writeR = 0;

        float dry = 1.f - m_wet;
        data[n*2]   = inL * dry + delL * m_wet;
        data[n*2+1] = inR * dry + delR * m_wet;
    }
}

QStringList DelayEffect::parameterIds() const {
    return {QStringLiteral("timeL"), QStringLiteral("timeR"),
            QStringLiteral("feedback"), QStringLiteral("wet")};
}

QString DelayEffect::parameterLabel(const QString &id) const {
    if (id == QLatin1String("timeL"))    return QStringLiteral("Time L (ms)");
    if (id == QLatin1String("timeR"))    return QStringLiteral("Time R (ms)");
    if (id == QLatin1String("feedback")) return QStringLiteral("Feedback");
    if (id == QLatin1String("wet"))      return QStringLiteral("Wet");
    return id;
}

float DelayEffect::parameterValue(const QString &id) const {
    if (id == QLatin1String("timeL"))    return m_timeLMs;
    if (id == QLatin1String("timeR"))    return m_timeRMs;
    if (id == QLatin1String("feedback")) return m_feedback;
    if (id == QLatin1String("wet"))      return m_wet;
    return 0.f;
}

void DelayEffect::setParameterValue(const QString &id, float v) {
    if      (id == QLatin1String("timeL"))    m_timeLMs  = v;
    else if (id == QLatin1String("timeR"))    m_timeRMs  = v;
    else if (id == QLatin1String("feedback")) m_feedback = v;
    else if (id == QLatin1String("wet"))      m_wet      = v;
    else return;
    if (id == QLatin1String("timeL") || id == QLatin1String("timeR")) {
        m_delaySampL = std::clamp(int(m_timeLMs * 0.001f * float(m_sampleRate)), 1, int(m_bufL.size())-1);
        m_delaySampR = std::clamp(int(m_timeRMs * 0.001f * float(m_sampleRate)), 1, int(m_bufR.size())-1);
    }
    emit parameterChanged(id, v);
}

QPair<float,float> DelayEffect::parameterRange(const QString &id) const {
    if (id == QLatin1String("timeL") || id == QLatin1String("timeR")) return {1.f, float(kMaxDelayMs)};
    if (id == QLatin1String("feedback")) return {0.f, 0.95f};
    if (id == QLatin1String("wet"))      return {0.f, 1.f};
    return {0.f, 1.f};
}

float DelayEffect::parameterDefault(const QString &id) const {
    if (id == QLatin1String("timeL"))    return 375.f;
    if (id == QLatin1String("timeR"))    return 500.f;
    if (id == QLatin1String("feedback")) return 0.4f;
    if (id == QLatin1String("wet"))      return 0.3f;
    return 0.f;
}

} // namespace quewi::audio
