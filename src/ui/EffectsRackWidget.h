#pragma once

#include "audio/AudioEditorModel.h"
#include <QWidget>
#include <QVBoxLayout>

namespace quewi::ui {

// Shows the effects chain for one AudioEditorTrack and lets the user:
//   • add effects (EQ / Compressor / Reverb / Delay)
//   • toggle each effect on/off
//   • adjust parameters via sliders
//   • remove effects
// Call setTrack() whenever the selected track changes.
class EffectsRackWidget : public QWidget {
    Q_OBJECT
public:
    explicit EffectsRackWidget(QWidget *parent = nullptr);
    ~EffectsRackWidget() override = default;

    void setTrack(audio::AudioEditorTrack *track);

private slots:
    void addEffect();
    void rebuild();

private:
    audio::AudioEditorTrack *m_track = nullptr;
    QVBoxLayout *m_effectsLayout = nullptr;

    QWidget *buildEffectRow(audio::AudioEffect *fx, int index);
};

} // namespace quewi::ui
