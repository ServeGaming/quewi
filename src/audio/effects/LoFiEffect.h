#pragma once
#include "audio/effects/SimpleEffect.h"

namespace quewi::audio {

// Bit crusher + sample-rate reducer: old recordings, cheap speakers, video
// game sounds. Parameters: bits (2-16), downsample (1-32×), mix (0-1).
class LoFiEffect : public SimpleEffect {
    Q_OBJECT
public:
    explicit LoFiEffect(QObject *parent = nullptr);

    Type    type() const override { return Type::LoFi; }
    QString name() const override { return QStringLiteral("Lo-Fi"); }

    void prepare(int) override { reset(); }
    void process(float *data, int numFrames) override;
    void reset() override { m_holdL = m_holdR = 0.f; m_counter = 0; }

private:
    float m_holdL = 0.f, m_holdR = 0.f;
    int   m_counter = 0;
};

} // namespace quewi::audio
