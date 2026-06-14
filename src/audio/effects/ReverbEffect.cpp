#include "audio/effects/ReverbEffect.h"
#include <cmath>

namespace quewi::audio {

// Delay-line lengths at 44100 Hz (from Freeverb by Jezar at Dreampoint)
static const int kCombTuningL[8]   = {1116,1188,1277,1356,1422,1491,1557,1617};
static const int kCombTuningR[8]   = {1139,1211,1300,1379,1445,1514,1580,1640};
static const int kApTuningL[4]     = {556, 441, 341, 225};
static const int kApTuningR[4]     = {579, 464, 364, 248};
static constexpr float kScaleDamp  = 0.4f;
static constexpr float kScaleRoom  = 0.28f;
static constexpr float kOffsetRoom = 0.7f;
static constexpr float kFixedGain  = 0.015f;

ReverbEffect::ReverbEffect(QObject *parent) : AudioEffect(parent) {
    // Size the comb/allpass delay lines up-front. Without this the buffers are
    // empty until prepare() runs, and process() -> CombFilter::tick() reads
    // buf[writePos] on an empty vector — an out-of-bounds crash. That happens
    // whenever an effect is added to a chain that is ALREADY playing (the live
    // editor preview prepares only the effects that existed when playback
    // started), which is exactly the "selecting Reverb just crashes" report.
    // prepare() re-runs at the real device sample rate when playback (re)starts.
    prepare(m_sampleRate);
}

void ReverbEffect::prepare(int sr) {
    m_sampleRate = sr;
    rebuildDelayLines();
    updateCombParams();
    reset();
}

void ReverbEffect::rebuildDelayLines() {
    // Scale delay lengths proportionally to sample rate (designed for 44100)
    float scale = float(m_sampleRate) / 44100.f;
    for (int i = 0; i < kNumCombs; ++i) {
        m_combL[i].resize(int(kCombTuningL[i] * scale) + 1);
        m_combR[i].resize(int(kCombTuningR[i] * scale) + 1);
    }
    for (int i = 0; i < kNumAllpass; ++i) {
        m_apL[i].resize(int(kApTuningL[i] * scale) + 1);
        m_apR[i].resize(int(kApTuningR[i] * scale) + 1);
        m_apL[i].feedback = m_apR[i].feedback = 0.5f;
    }
}

void ReverbEffect::updateCombParams() {
    float fb   = m_roomSize * kScaleRoom + kOffsetRoom;
    float damp = m_damping * kScaleDamp;
    for (int i = 0; i < kNumCombs; ++i) {
        m_combL[i].feedback = m_combR[i].feedback = fb;
        m_combL[i].damp1 = m_combR[i].damp1 = damp;
        m_combL[i].damp2 = m_combR[i].damp2 = 1.f - damp;
    }
}

void ReverbEffect::reset() {
    for (auto &c : m_combL) c.reset();
    for (auto &c : m_combR) c.reset();
    for (auto &a : m_apL)   a.reset();
    for (auto &a : m_apR)   a.reset();
}

void ReverbEffect::process(float *data, int numFrames) {
    if (!m_enabled) return;
    float wet1 = m_wet * (m_width / 2.f + 0.5f);
    float wet2 = m_wet * ((1.f - m_width) / 2.f);
    float dry  = 1.f - m_wet;

    for (int n = 0; n < numFrames; ++n) {
        float dryL = data[n*2], dryR = data[n*2+1];
        float inL = (dryL + dryR) * kFixedGain;
        float inR = inL;

        float outL = 0.f, outR = 0.f;
        for (int i = 0; i < kNumCombs; ++i) {
            outL += m_combL[i].tick(inL);
            outR += m_combR[i].tick(inR);
        }
        for (int i = 0; i < kNumAllpass; ++i) {
            outL = m_apL[i].tick(outL);
            outR = m_apR[i].tick(outR);
        }
        data[n*2]   = dryL * dry + outL * wet1 + outR * wet2;
        data[n*2+1] = dryR * dry + outR * wet1 + outL * wet2;
    }
}

QStringList ReverbEffect::parameterIds() const {
    return {QStringLiteral("roomSize"), QStringLiteral("damping"),
            QStringLiteral("width"),    QStringLiteral("wet")};
}

QString ReverbEffect::parameterLabel(const QString &id) const {
    if (id == QLatin1String("roomSize")) return QStringLiteral("Room Size");
    if (id == QLatin1String("damping"))  return QStringLiteral("Damping");
    if (id == QLatin1String("width"))    return QStringLiteral("Width");
    if (id == QLatin1String("wet"))      return QStringLiteral("Wet");
    return id;
}

float ReverbEffect::parameterValue(const QString &id) const {
    if (id == QLatin1String("roomSize")) return m_roomSize;
    if (id == QLatin1String("damping"))  return m_damping;
    if (id == QLatin1String("width"))    return m_width;
    if (id == QLatin1String("wet"))      return m_wet;
    return 0.f;
}

void ReverbEffect::setParameterValue(const QString &id, float v) {
    if      (id == QLatin1String("roomSize")) m_roomSize = v;
    else if (id == QLatin1String("damping"))  m_damping  = v;
    else if (id == QLatin1String("width"))    m_width    = v;
    else if (id == QLatin1String("wet"))      m_wet      = v;
    else return;
    updateCombParams();
    emit parameterChanged(id, v);
}

QPair<float,float> ReverbEffect::parameterRange(const QString &) const {
    return {0.f, 1.f};
}

float ReverbEffect::parameterDefault(const QString &id) const {
    if (id == QLatin1String("roomSize")) return 0.5f;
    if (id == QLatin1String("damping"))  return 0.5f;
    if (id == QLatin1String("width"))    return 1.0f;
    if (id == QLatin1String("wet"))      return 0.3f;
    return 0.f;
}

} // namespace quewi::audio
