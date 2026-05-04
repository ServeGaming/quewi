#pragma once
#include "audio/AudioEffect.h"
#include <vector>
#include <array>

namespace quewi::audio {

// Algorithmic stereo reverb based on the Freeverb architecture:
// 8 parallel comb filters + 4 series allpass filters per channel.
// Parameters: roomSize (0-1), damping (0-1), width (0-1), wet (0-1).
class ReverbEffect : public AudioEffect {
    Q_OBJECT
public:
    explicit ReverbEffect(QObject *parent = nullptr);
    ~ReverbEffect() override = default;

    Type    type() const override { return Type::Reverb; }
    QString name() const override { return QStringLiteral("Reverb"); }

    void prepare(int sampleRate) override;
    void process(float *data, int numFrames) override;
    void reset() override;

    QStringList        parameterIds()                     const override;
    QString            parameterLabel(const QString &id)  const override;
    float              parameterValue(const QString &id)  const override;
    void               setParameterValue(const QString &id, float v) override;
    QPair<float,float> parameterRange(const QString &id)  const override;
    float              parameterDefault(const QString &id) const override;

private:
    struct CombFilter {
        std::vector<float> buf;
        int    writePos = 0;
        float  feedback = 0.5f;
        float  damp1    = 0.5f;
        float  damp2    = 0.5f;
        float  filterStore = 0.f;

        void resize(int n) { buf.assign(n, 0.f); }
        float tick(float in) {
            float out = buf[writePos];
            filterStore = out * damp2 + filterStore * damp1;
            buf[writePos] = in + filterStore * feedback;
            if (++writePos >= int(buf.size())) writePos = 0;
            return out;
        }
        void reset() { buf.assign(buf.size(), 0.f); filterStore=0; writePos=0; }
    };

    struct AllpassFilter {
        std::vector<float> buf;
        int   writePos = 0;
        float feedback = 0.5f;

        void resize(int n) { buf.assign(n, 0.f); }
        float tick(float in) {
            float bufOut = buf[writePos];
            float out = -in + bufOut;
            buf[writePos] = in + bufOut * feedback;
            if (++writePos >= int(buf.size())) writePos = 0;
            return out;
        }
        void reset() { buf.assign(buf.size(), 0.f); writePos=0; }
    };

    static constexpr int kNumCombs   = 8;
    static constexpr int kNumAllpass = 4;

    std::array<CombFilter,   kNumCombs>   m_combL, m_combR;
    std::array<AllpassFilter, kNumAllpass> m_apL,   m_apR;

    float m_roomSize = 0.5f;
    float m_damping  = 0.5f;
    float m_width    = 1.0f;
    float m_wet      = 0.3f;

    int m_sampleRate = 48000;

    void rebuildDelayLines();
    void updateCombParams();
};

} // namespace quewi::audio
