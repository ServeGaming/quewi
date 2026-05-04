#pragma once
#include "audio/AudioEffect.h"
#include <array>
#include <cmath>

namespace quewi::audio {

// 3-band fully-parametric EQ. Each band is a biquad peaking filter.
// Parameters per band N (N = 1,2,3):
//   eqN_freq  — centre frequency in Hz
//   eqN_gain  — boost/cut in dB (-24 … +24)
//   eqN_q     — quality factor (0.1 … 10)
class EqEffect : public AudioEffect {
    Q_OBJECT
public:
    static constexpr int kNumBands = 3;

    explicit EqEffect(QObject *parent = nullptr);
    ~EqEffect() override = default;

    // Direct access for the visual editor — read-only.
    struct BandSnapshot { float freq; float gainDb; float Q; bool enabled; };
    BandSnapshot bandSnapshot(int i) const;
    void         setBand(int i, float freq, float gainDb, float Q);

    // Combined magnitude response of all bands at a given frequency, in dB.
    float responseDb(float frequencyHz) const;
    // Per-band magnitude response, dB.
    float bandResponseDb(int bandIdx, float frequencyHz) const;
    int   sampleRate() const { return m_sampleRate; }

    Type    type() const override { return Type::Eq; }
    QString name() const override { return QStringLiteral("Parametric EQ"); }

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
    struct Band {
        float freq  = 1000.f;
        float gainDb = 0.f;
        float Q     = 0.707f;
    };

    struct Biquad {
        float b0=1,b1=0,b2=0,a1=0,a2=0;
        float x1=0,x2=0,y1=0,y2=0;
        float tick(float x) {
            float y = b0*x + b1*x1 + b2*x2 - a1*y1 - a2*y2;
            x2=x1; x1=x; y2=y1; y1=y;
            return y;
        }
        void reset() { x1=x2=y1=y2=0.f; }
    };

    void rebuildCoeffs(int bandIdx);

    static constexpr int kBands = kNumBands;
    std::array<Band,   kBands>    m_bands;
    std::array<Biquad, kBands>    m_filtersL;
    std::array<Biquad, kBands>    m_filtersR;
    int m_sampleRate = 48000;
};

} // namespace quewi::audio
