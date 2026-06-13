#pragma once

#include "audio/AudioEditorModel.h"
#include <QWidget>

class QHBoxLayout;
class QLabel;

namespace quewi::ui {

// The per-track effects rack shown along the bottom of the audio editor.
//
// Effects are laid out as a horizontal row of channel-strip "cards" — one
// per effect — so each gets room to breathe instead of being crammed into a
// dense vertical list. Cards carry an accent stripe per effect type, a
// bypass switch, a one-line description, and either a big "Open Editor"
// button (EQ / Compressor, which have full visual editors) or a tidy set of
// labelled sliders (Reverb / Delay). Call setTrack() when the selection
// changes.
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
    QHBoxLayout *m_cardsLayout = nullptr; // horizontal strip of effect cards
    QLabel      *m_trackLabel  = nullptr;

    QWidget *buildEffectCard(audio::AudioEffect *fx, int index);
    QWidget *buildParamRow(audio::AudioEffect *fx, const QString &id, QWidget *parent);
    QWidget *buildPlaceholder(const QString &title, const QString &subtitle);
    void     openEditor(audio::AudioEffect *fx);
};

} // namespace quewi::ui
