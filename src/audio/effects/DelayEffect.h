#pragma once
#include "audio/AudioEffect.h"
#include <vector>

namespace quewi::audio {

// Stereo ping-pong delay with independent L/R time, feedback, and wet level.
// Parameters: timeL (ms), timeR (ms), feedback (0-1), wet (0-1).
class DelayEffect : public AudioEffect {
    Q_OBJECT
public:
    explicit DelayEffect(QObject *parent = nullptr);
    ~DelayEffect() override = default;

    Type    type() const override { return Type::Delay; }
    QString name() const override { return QStringLiteral("Delay"); }

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
    float m_timeLMs   = 375.f;
    float m_timeRMs   = 500.f;
    float m_feedback  = 0.4f;
    float m_wet       = 0.3f;

    int m_sampleRate  = 48000;
    static constexpr int kMaxDelayMs = 2000;

    std::vector<float> m_bufL, m_bufR;
    int  m_writeL = 0, m_writeR = 0;
    int  m_delaySampL = 0, m_delaySampR = 0;

    void rebuildBuffers();
};

} // namespace quewi::audio
