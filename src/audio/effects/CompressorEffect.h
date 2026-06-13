#pragma once
#include "audio/AudioEffect.h"

#include <atomic>

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

    // ── Visual-editor support ───────────────────────────────────────────
    // Steady-state output level (dBFS) the compressor produces for a given
    // input level (dBFS), with threshold/ratio/knee/makeup all applied. The
    // editor plots this directly so its transfer curve always matches the
    // running DSP exactly. Below ~ -90 dBFS the curve is treated as 1:1.
    float transferOutputDb(float inputDb) const;

    // Most-recent gain reduction the processor applied, in dB (≤ 0). Updated
    // once per audio block (peak reduction within the block) and read by the
    // editor's live GR meter. Atomic so the GUI polls it without touching the
    // audio thread's locks.
    float currentGainReductionDb() const {
        return m_currentGrDb.load(std::memory_order_relaxed);
    }

    // Typed accessors so the paint loop avoids per-point string lookups.
    float thresholdDb() const { return m_threshDb; }
    float ratio()       const { return m_ratio; }
    float kneeDb()      const { return m_kneeDb; }
    float makeupDb()    const { return m_makeupDb; }

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
    std::atomic<float> m_currentGrDb{0.f}; // published per block for the GR meter

    void recomputeCoeffs();
    float computeGainDb(float levelDb) const; // returns gain reduction in dB
};

} // namespace quewi::audio
