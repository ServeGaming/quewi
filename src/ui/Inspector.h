#pragma once

#include <QPointer>
#include <QWidget>

class QLabel;
class QLineEdit;
class QSpinBox;
class QDoubleSpinBox;
class QPlainTextEdit;
class QGroupBox;
class QComboBox;
class QPushButton;
class QCheckBox;
class QFormLayout;

namespace quewi::core { class Workspace; class CueList; }
namespace quewi::cues { class Cue; class FadeCue; }
namespace quewi::osc  { class OscCue; }
namespace quewi::audio { class AudioCue; }
namespace quewi::lighting { class LightCue; class LightFadeCue; }
namespace quewi::video { class VideoCue; class ImageCue; class TextCue; class VisualCue; }
class QTableWidget;

namespace quewi::ui {

class WaveformWidget;

// Right-pane editor for the currently-selected cue. Common header
// (number, name, pre/post wait, notes) for every type, plus
// type-specific groups that show only for the matching cue type.
class Inspector : public QWidget {
    Q_OBJECT
public:
    explicit Inspector(QWidget *parent = nullptr);
    ~Inspector() override;

    void setWorkspace(core::Workspace *workspace);

public slots:
    void setCue(quewi::cues::Cue *cue);

private slots:
    void onCueChanged();
    void commitNumber();
    void commitName();
    void commitPreWait();
    void commitPostWait();
    void commitNotes();

    // OSC
    void commitOscAddress();
    void commitOscHost();
    void commitOscPort();
    void commitOscArgs();

    // Audio
    void browseAudioFile();
    void commitAudioGain();
    void commitAudioFadeIn();
    void commitAudioFadeOut();
    void commitAudioLoop();

    // Fade
    void commitFadeTarget();
    void commitFadeParameter();
    void commitFadeTargetValue();
    void commitFadeDuration();

    // Light
    void commitLightUniverse();
    void addLightChannel();
    void removeLightChannel();
    void commitLightChannels();

    // Light Fade
    void commitLightFadeTarget();
    void commitLightFadeDuration();

    // Visual (video/image/text shared geometry/screen/opacity)
    void browseVisualFile();
    void commitVisualScreen();
    void commitVisualGeometry();
    void commitVisualOpacity();
    void commitVideoLoop();
    void commitTextString();
    void commitTextSize();
    void pickTextColor();

private:
    void rebuild();
    void rebuildFadeTargets();
    void pushFieldEdit(const QString &field, const QVariant &newValue);

    QPointer<core::Workspace> m_workspace;
    QPointer<cues::Cue>       m_cue;

    QLabel         *m_typeLabel = nullptr;
    QDoubleSpinBox *m_number    = nullptr;
    QLineEdit      *m_name      = nullptr;
    QDoubleSpinBox *m_preWait   = nullptr;
    QDoubleSpinBox *m_postWait  = nullptr;
    QPlainTextEdit *m_notes     = nullptr;

    // OSC group
    QGroupBox      *m_oscGroup    = nullptr;
    QLineEdit      *m_oscAddress  = nullptr;
    QLineEdit      *m_oscHost     = nullptr;
    QSpinBox       *m_oscPort     = nullptr;
    QLineEdit      *m_oscArgs     = nullptr;

    // Audio group
    QGroupBox      *m_audioGroup    = nullptr;
    QLineEdit      *m_audioPath     = nullptr;
    QPushButton    *m_audioBrowse   = nullptr;
    QDoubleSpinBox *m_audioGain     = nullptr;
    QDoubleSpinBox *m_audioFadeIn   = nullptr;
    QDoubleSpinBox *m_audioFadeOut  = nullptr;
    QCheckBox      *m_audioLoop     = nullptr;
    WaveformWidget *m_audioWaveform = nullptr;
    QLabel         *m_audioMeta     = nullptr;

    // Fade group
    QGroupBox      *m_fadeGroup    = nullptr;
    QComboBox      *m_fadeTarget   = nullptr;
    QComboBox      *m_fadeParam    = nullptr;
    QDoubleSpinBox *m_fadeValue    = nullptr;
    QDoubleSpinBox *m_fadeDuration = nullptr;

    // Light group
    QGroupBox      *m_lightGroup    = nullptr;
    QSpinBox       *m_lightUniverse = nullptr;
    QTableWidget   *m_lightTable    = nullptr;
    QPushButton    *m_lightAdd      = nullptr;
    QPushButton    *m_lightRemove   = nullptr;

    // Light Fade group
    QGroupBox      *m_lightFadeGroup    = nullptr;
    QComboBox      *m_lightFadeTarget   = nullptr;
    QDoubleSpinBox *m_lightFadeDuration = nullptr;

    // Visual (video/image/text)
    QGroupBox      *m_visualGroup       = nullptr;
    QLineEdit      *m_visualPath        = nullptr;
    QPushButton    *m_visualBrowse      = nullptr;
    QSpinBox       *m_visualScreen      = nullptr;
    QDoubleSpinBox *m_visualX           = nullptr;
    QDoubleSpinBox *m_visualY           = nullptr;
    QDoubleSpinBox *m_visualW           = nullptr;
    QDoubleSpinBox *m_visualH           = nullptr;
    QDoubleSpinBox *m_visualOpacity     = nullptr;
    QCheckBox      *m_videoLoop         = nullptr;
    QLineEdit      *m_textString        = nullptr;
    QSpinBox       *m_textSize          = nullptr;
    QPushButton    *m_textColorBtn      = nullptr;

    bool m_loading = false;
};

} // namespace quewi::ui
