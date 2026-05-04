#pragma once
#include "audio/AudioEffect.h"

namespace quewi::audio {

// Feed-forward RMS compressor with soft-knee, attack/release envelope.
// Parameters: threshold (dB), ratio, attack (ms), release (ms),
//             knee (dB width), makeupGain (dB).
class CompressorEffect : public AudioEffect {
    Q_OBJECT
public:
    explicit CompressorEffect(QObject *parent = nullptr);
    ~CompressorEffect() override = default;

    Type    type() const override { return Type::Compressor; }
    QString name() const override { return QStringLiteral("Compressor"); }

    void prepare(int sampleRate) override;
    void process(float *data, int numFrames) override;
    void reset() override;

    QStringList        parameterIds()                     const override;
    QString            parameterLabel(const QString &id)  const override;
    float              parameterValue(const QString &id)  const override;
    void               setParameterValue(const QString &id, float v) override;
    QPair<float,float> parameterRange(const QString &id)  const override;
    float              parameterDefault(const QString &id) const override;
    int                parameterDecimals(const QString &id) const override;

private:
    float m_threshDb   = -18.f;
    float m_ratio      = 4.f;
    float m_attackMs   = 10.f;
    float m_releaseMs  = 100.f;
    float m_kneeDb     = 6.f;
    float m_makeupDb   = 0.f;

    // Runtime
    int   m_sampleRate = 48000;
    float m_attackCoeff  = 0.f;
    float m_releaseCoeff = 0.f;
    float m_gainEnvDb    = 0.f; // current smoothed gain reduction (dB)

    void recomputeCoeffs();
    float computeGainDb(float levelDb) const; // returns gain reduction in dB
};

} // namespace quewi::audio
