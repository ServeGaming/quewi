#pragma once
#include "audio/effects/SimpleEffect.h"

namespace quewi::audio {

// Volume wobble — tremolo, or with Stereo up an auto-pan that swings the
// sound left and right. Warbling radios, underwater, sirens, spooky.
// Parameters: rate (Hz), depth (0-1), stereo (0 = both sides together,
// 1 = opposite sides, i.e. auto-pan).
class TremoloEffect : public SimpleEffect {
    Q_OBJECT
public:
    explicit TremoloEffect(QObject *parent = nullptr);

    Type    type() const override { return Type::Tremolo; }
    QString name() const override { return QStringLiteral("Tremolo"); }

    void prepare(int sampleRate) override { m_sampleRate = sampleRate > 0 ? sampleRate : 48000; }
    void process(float *data, int numFrames) override;
    void reset() override { m_phase = 0.0; }

private:
    int    m_sampleRate = 48000;
    double m_phase = 0.0;          // radians
};

} // namespace quewi::audio
