#pragma once
#include "audio/effects/SimpleEffect.h"

namespace quewi::audio {

// Soft-clipping overdrive (tanh waveshaper) with a tone control. The grit in
// telephone, radio and megaphone voices, or a blown-out speaker.
// Parameters: drive (0-1), tone (0-1, dark → bright), mix (0-1), output (dB).
class DistortionEffect : public SimpleEffect {
    Q_OBJECT
public:
    explicit DistortionEffect(QObject *parent = nullptr);

    Type    type() const override { return Type::Distortion; }
    QString name() const override { return QStringLiteral("Distortion"); }

    void prepare(int sampleRate) override;
    void process(float *data, int numFrames) override;
    void reset() override;

private:
    void paramChanged(int) override { updateCoeffs(); }
    void updateCoeffs();

    int   m_sampleRate = 48000;
    float m_gain = 1.f, m_norm = 1.f, m_toneA = 0.f, m_outGain = 1.f;
    float m_lpL = 0.f, m_lpR = 0.f;
};

} // namespace quewi::audio
