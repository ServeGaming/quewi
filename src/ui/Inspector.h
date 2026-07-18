#pragma once

#include <QHash>
#include <QPointer>
#include <QWidget>

class QLabel;
class QLineEdit;
class QSpinBox;
class QSlider;
class QDoubleSpinBox;
class QPlainTextEdit;
class QGroupBox;
class QComboBox;
class QPushButton;
class QCheckBox;
class QGridLayout;
class QFormLayout;
class QButtonGroup;
class QListWidget;
class QTimer;
class QToolButton;
class QBoxLayout;
class QEvent;

namespace quewi::core { class Workspace; class CueList; }
namespace quewi::cues { class Cue; class FadeCue; }
namespace quewi::osc  { class OscCue; }
namespace quewi::audio { class AudioCue; class AudioEngine; }
namespace quewi::lighting { class LightCue; class LightFadeCue; }
namespace quewi::video { class VideoCue; class ImageCue; class TextCue; class VisualCue; class VideoEngine; }
namespace quewi::midi  { class MidiCue; class MscCue; class MidiEngine; }
class QTableWidget;

namespace quewi::ui {

class WaveformWidget;
class StageView;
class VideoScrubber;

// Right-pane editor for the currently-selected cue. Common header
// (number, name, pre/post wait, notes) for every type, plus
// type-specific groups that show only for the matching cue type.
class Inspector : public QWidget {
    Q_OBJECT
public:
    explicit Inspector(QWidget *parent = nullptr);
    ~Inspector() override;

    void setWorkspace(core::Workspace *workspace);
    void setAudioEngine(audio::AudioEngine *engine);
    void setVideoEngine(video::VideoEngine *engine);
    void setMidiEngine(midi::MidiEngine *engine);

public slots:
    void setCue(quewi::cues::Cue *cue);

protected:
    // Keeps each poppable section's ⤢ button pinned to the top-right corner as
    // the section (or its floating window) resizes.
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void onCueChanged();
    void commitNumber();
    void commitName();
    void commitPreWait();
    void commitPostWait();
    void commitNotes();
    void commitContinueMode();
    void commitArmed();
    void commitLink();
    void pickCueColor();
    void clearCueColor();

    // Wait
    void commitWaitDuration();

    // Start/Stop/Goto target picker
    void commitTargetCue();

    // Group
    void commitGroupMode();
    void commitGroupStepInterval();
    void addGroupChild();
    void removeGroupChild();

    // MIDI / MSC
    void commitMidiPort();
    void commitMidiBytes();
    void commitMscPort();
    void commitMscField();

    // OSC
    void commitOscAddress();
    void commitOscHost();
    void commitOscPort();
    void commitOscArgs();

    // Audio
    void browseAudioFile();
    void onGainSliderChanged(int centiDb);
    void onPanSliderChanged(int hundredths);
    void commitAudioFadeIn();
    void commitAudioFadeOut();
    void commitAudioTrimIn();
    void commitAudioTrimOut();
    void commitAudioLoop();
    void normalizeAudio();
    void reverseAudio();
    void setAudioModeTrim();
    void setAudioModeFade();
    void commitAudioOutputDevice();

    // Object Audio
    void commitObjectAudioEnabled(bool on);
    void commitSpeakerPatch();
    void onStagePositionChanged(float azimuthDeg, float elevationDeg);
    void onElevationSliderChanged(int tenthDeg);
    void onSpreadSliderChanged(int hundredths);
    void onOutputMatrixToggled(bool on);
    void onOutputMatrixSliderChanged();
    void rebuildOutputMatrix();
    void openSpeakerPatchDialog();
    void onTrajectoryAdd();
    void onTrajectoryRemove();
    void onTrajectoryCellChanged(int row, int column);
    void onTrajectoryModeChanged();

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

    // ── Detachable sections (pop-out) ─────────────────────────────────
    // Any section can be torn off into its own floating window and moved to
    // another monitor, then docked back — the whole Inspector stays removable
    // as before, this just makes the pieces movable too. makePoppable() adds
    // the corner button; togglePopout() floats/re-docks; when floated we
    // remember exactly where the section came from so it returns to the same
    // spot. Uses the Qt::Window-on-a-child trick, so the section keeps its
    // parent for ownership and never leaks.
    void makePoppable(QGroupBox *box);
    void togglePopout(QGroupBox *box);
    void positionPopoutButton(QGroupBox *box);
    struct Placement { QBoxLayout *layout = nullptr; int index = -1; };
    QHash<QGroupBox *, QToolButton *> m_popoutButtons;
    QHash<QGroupBox *, Placement>     m_poppedOut;
    void pushFieldEdit(const QString &field, const QVariant &newValue);
    void pollVideoTransport();
    // ~30 Hz follow of the selected audio cue's live voice position, used to
    // move the waveform playhead. Runs only while an audio cue is selected.
    void pollAudioPlayhead();

    QPointer<core::Workspace>   m_workspace;
    QPointer<cues::Cue>         m_cue;
    QPointer<audio::AudioEngine> m_audioEngine;
    QPointer<video::VideoEngine> m_videoEngine;
    QTimer                      *m_videoPollTimer = nullptr;
    QTimer                      *m_audioPollTimer = nullptr;
    midi::MidiEngine            *m_midiEngine = nullptr;

    QLabel         *m_typeLabel = nullptr;
    // Empty-state placeholder card shown when m_cue == nullptr.
    // m_inspectorBody is the container for the actual form + groups;
    // toggling visibility between the two gives a clean "nothing
    // selected → big helpful card" without leaving a disabled form
    // standing where the data should be.
    QWidget        *m_emptyState    = nullptr;
    QWidget        *m_inspectorBody = nullptr;
    QPushButton    *m_colorChip = nullptr;
    QPushButton    *m_colorClear = nullptr;
    QDoubleSpinBox *m_number    = nullptr;
    QLineEdit      *m_name      = nullptr;
    QDoubleSpinBox *m_preWait   = nullptr;
    QDoubleSpinBox *m_postWait  = nullptr;
    QComboBox      *m_continueMode = nullptr;
    QCheckBox      *m_armedCheck = nullptr;
    QPlainTextEdit *m_notes     = nullptr;
    // Cross-list link picker — lists the show's Mix (DCA) cues so a playback
    // cue can be paired with a DCA cue (fires bidirectionally). Row hidden
    // when the show has no mix cues. m_headerForm is kept so the row can be
    // toggled without rebuilding the whole common header.
    QComboBox      *m_linkCombo  = nullptr;
    QFormLayout    *m_headerForm = nullptr;

    // Wait
    QGroupBox      *m_waitGroup    = nullptr;
    QDoubleSpinBox *m_waitDuration = nullptr;

    // Start / Stop / Goto target picker (one shared widget)
    QGroupBox      *m_targetGroup = nullptr;
    QComboBox      *m_targetCombo = nullptr;

    // Group
    QGroupBox      *m_groupGroup       = nullptr;
    QComboBox      *m_groupMode        = nullptr;
    QDoubleSpinBox *m_groupStepInterval = nullptr;
    QListWidget    *m_groupChildren    = nullptr;
    QComboBox      *m_groupChildPicker = nullptr;
    QPushButton    *m_groupChildAdd    = nullptr;
    QPushButton    *m_groupChildRemove = nullptr;

    // MIDI
    QGroupBox      *m_midiGroup    = nullptr;
    QComboBox      *m_midiPort     = nullptr;
    QLineEdit      *m_midiBytes    = nullptr;

    // MSC
    QGroupBox      *m_mscGroup     = nullptr;
    QComboBox      *m_mscPort      = nullptr;
    QSpinBox       *m_mscDeviceId  = nullptr;
    QSpinBox       *m_mscFormat    = nullptr;
    QSpinBox       *m_mscCommand   = nullptr;
    QLineEdit      *m_mscQNumber   = nullptr;
    QLineEdit      *m_mscQList     = nullptr;
    QLineEdit      *m_mscQPath     = nullptr;

    // OSC group
    QGroupBox      *m_oscGroup    = nullptr;
    QLineEdit      *m_oscAddress  = nullptr;
    QLineEdit      *m_oscHost     = nullptr;
    QSpinBox       *m_oscPort     = nullptr;
    QLineEdit      *m_oscArgs     = nullptr;

    // Audio group
    QGroupBox      *m_audioGroup     = nullptr;
    QLineEdit      *m_audioPath      = nullptr;
    QPushButton    *m_audioBrowse    = nullptr;
    QSlider        *m_audioGainSlider = nullptr;
    QLabel         *m_audioGainLabel = nullptr;
    QSlider        *m_audioPanSlider = nullptr;
    QLabel         *m_audioPanLabel  = nullptr;
    QDoubleSpinBox *m_audioFadeIn    = nullptr;
    QDoubleSpinBox *m_audioFadeOut   = nullptr;
    QDoubleSpinBox *m_audioTrimIn    = nullptr;
    QDoubleSpinBox *m_audioTrimOut   = nullptr;
    QCheckBox      *m_audioLoop      = nullptr;
    QPushButton    *m_audioNormalize = nullptr;
    QPushButton    *m_audioReverse   = nullptr;
    QPushButton    *m_audioModeTrim  = nullptr;
    QPushButton    *m_audioModeFade  = nullptr;
    WaveformWidget *m_audioWaveform  = nullptr;
    QLabel         *m_audioMeta      = nullptr;
    QComboBox      *m_audioOutputDevice = nullptr;

    // Object Audio sub-group (lives inside m_audioGroup; visible only
    // when the audio cue has objectAudio = true).
    QGroupBox      *m_objAudioGroup     = nullptr;
    QCheckBox      *m_objAudioEnable    = nullptr;
    QComboBox      *m_objSpeakerPatch   = nullptr;
    QPushButton    *m_objSpeakerEdit    = nullptr;
    StageView      *m_objStageView      = nullptr;
    QSlider        *m_objElevation      = nullptr;
    QSlider        *m_objSpread         = nullptr;
    QLabel         *m_objElevationLabel = nullptr;
    QLabel         *m_objSpreadLabel    = nullptr;

    // Trajectory editor (inside Object Audio group). Animates the source
    // position over the cue's playback time; when the table has 2+ rows,
    // GoEngine ticks it at ~30 Hz on the running voice.
    QGroupBox      *m_outputMatrixGroup = nullptr;
    QGridLayout    *m_outputMatrixLayout = nullptr;
    QList<QSlider*> m_outputMatrixSliders;
    QList<QLabel*>  m_outputMatrixLabels;

    QGroupBox      *m_trajGroup     = nullptr;
    QTableWidget   *m_trajTable     = nullptr;
    QComboBox      *m_trajMode      = nullptr;
    QPushButton    *m_trajAdd       = nullptr;
    QPushButton    *m_trajRemove    = nullptr;

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
    QComboBox      *m_visualScreen      = nullptr;
    QDoubleSpinBox *m_visualX           = nullptr;
    QDoubleSpinBox *m_visualY           = nullptr;
    QDoubleSpinBox *m_visualW           = nullptr;
    QDoubleSpinBox *m_visualH           = nullptr;
    QDoubleSpinBox *m_visualOpacity     = nullptr;
    QCheckBox      *m_videoLoop         = nullptr;
    VideoScrubber  *m_videoScrubber     = nullptr;
    QLineEdit      *m_textString        = nullptr;
    QSpinBox       *m_textSize          = nullptr;
    QPushButton    *m_textColorBtn      = nullptr;

    bool m_loading = false;
};

} // namespace quewi::ui
