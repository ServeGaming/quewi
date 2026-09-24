#pragma once
#include "audio/effects/SimpleEffect.h"

#include <vector>

namespace quewi::audio {

// Real-time pitch shifter without changing speed: monster voices (down),
// chipmunks (up). Classic two-tap delay-line design: two read heads sweep
// through a short window at the pitch ratio, crossfaded so each is silent as
// it wraps. Cheap and latency-light; at large shifts it has a slight
// "chorused" texture, which suits character voices.
// Parameters: semitones (-12..+12), mix (0-1).
class PitchShiftEffect : public SimpleEffect {
    Q_OBJECT
public:
    explicit PitchShiftEffect(QObject *parent = nullptr);

    Type    type() const override { return Type::PitchShift; }
    QString name() const override { return QStringLiteral("Pitch Shift"); }

    void prepare(int sampleRate) override;
    void process(float *data, int numFrames) override;
    void reset() override;

private:
    float readTap(const std::vector<float> &buf, float delay) const;

    int   m_sampleRate = 48000;
    int   m_window = 2400;          // samples (50 ms)
    std::vector<float> m_bufL, m_bufR;
    int   m_write = 0;
    float m_phase = 0.f;            // 0..1 position of tap A in the window
};

} // namespace quewi::audio
