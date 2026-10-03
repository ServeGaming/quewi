#pragma once

#include "audio/AudioCue.h"
#include "audio/BeatGrid.h"
#include "audio/LightTrigger.h"

#include <QElapsedTimer>
#include <QFrame>
#include <QPointer>
#include <QSet>
#include <QUuid>
#include <QWidget>

#include <functional>
#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QPushButton;
class QShowEvent;
class QSlider;
class QSpinBox;
class QStackedWidget;
class QTableWidget;
class QUndoStack;

namespace quewi::core { class Workspace; }

namespace quewi::ui {

// Edits one TriggerAction — what an edge of a lighting trigger sends.
//
// Simple mode (the default): "What should it do?" — Nothing, the things the
// lighting desk in Preferences can do (GO, Bump sub, Go to cue…; Kind::Desk)
// or "Fire a quewi cue" — with only the fields that action needs and a
// plain-English preview line.
//
// Custom mode ("Custom OSC / MIDI" toggle; automatic for Osc / Midi / Msc
// actions): the expert editor — a kind combo (OSC / MIDI / MSC), its page, and
// Presets ▾. Turning it on from a desk action pre-fills the OSC the desk
// would get, so the real command can be seen and tweaked.
//
// Every change is reported through edited() with the whole action; the owner
// decides how to store it.
class TriggerActionEditor : public QFrame {
    Q_OBJECT
public:
    explicit TriggerActionEditor(QWidget *parent = nullptr);

    void setTitle(const QString &title);
    // Shows `a` without emitting edited().
    void setAction(const audio::TriggerAction &a);
    const audio::TriggerAction &action() const { return m_action; }

    void setWorkspace(core::Workspace *ws);                 // Fire-cue picker
    void setMidiPortsProvider(std::function<QStringList()> f);
    // Re-reads the lighting desk in Preferences (what simple mode offers).
    void refreshDesk();

    bool isCustom() const;             // showing the expert (OSC / MIDI / MSC) editor

    // Presets ▾, in menu order. applyPreset fills the fields and emits edited().
    static QStringList presetNames();
    void applyPreset(int index);

signals:
    void edited(const quewi::audio::TriggerAction &action);
    void testRequested(const quewi::audio::TriggerAction &action);

private:
    void load();                       // m_action → widgets
    void change(const std::function<void(audio::TriggerAction &)> &f);
    void setCustom(bool on);           // the toggle
    void fillPorts(QComboBox *combo);
    void fillCues();
    void updateMidiRows();
    void syncDoCombo();                // "What should it do?" items for the desk
    void updateSimpleRows();           // which simple fields show, their labels, preview

    audio::TriggerAction m_action;
    bool m_loading = false;
    int  m_deskType = 0;               // core::LightingDesk::Type, cached by refreshDesk
    QPointer<core::Workspace> m_workspace;
    std::function<QStringList()> m_midiPorts;

    QLabel         *m_title = nullptr;
    QCheckBox      *m_custom = nullptr;
    QComboBox      *m_kind  = nullptr;
    QPushButton    *m_presets = nullptr;
    QPushButton    *m_test  = nullptr;
    QWidget        *m_simpleBox = nullptr;
    QWidget        *m_customBox = nullptr;
    QStackedWidget *m_pages = nullptr;

    // Simple mode
    QFormLayout    *m_simpleForm = nullptr;
    QComboBox      *m_do = nullptr;
    QLineEdit      *m_number = nullptr;
    QSpinBox       *m_list = nullptr;
    QWidget        *m_levelRow = nullptr;
    QSlider        *m_levelSlider = nullptr;
    QSpinBox       *m_level = nullptr;
    QDoubleSpinBox *m_hold = nullptr;
    QLineEdit      *m_text = nullptr;
    QLabel         *m_preview = nullptr;
    QLabel         *m_problem = nullptr;

    // OSC
    QLineEdit *m_oscHost = nullptr;
    QSpinBox  *m_oscPort = nullptr;
    QComboBox *m_oscTransport = nullptr;
    QLineEdit *m_oscAddress = nullptr;
    QLineEdit *m_oscArgs = nullptr;
    // MIDI
    QFormLayout *m_midiForm = nullptr;
    QComboBox *m_midiPort = nullptr;
    QComboBox *m_midiType = nullptr;
    QSpinBox  *m_midiChannel = nullptr;
    QSpinBox  *m_midiData1 = nullptr;
    QSpinBox  *m_midiData2 = nullptr;
    QLineEdit *m_midiRaw = nullptr;
    // MSC
    QComboBox *m_mscPort = nullptr;
    QSpinBox  *m_mscDevice = nullptr;
    QComboBox *m_mscCommand = nullptr;
    QComboBox *m_mscFormat = nullptr;
    QLineEdit *m_mscQNumber = nullptr;
    QLineEdit *m_mscQList = nullptr;
    // Fire cue
    QComboBox *m_cueCombo = nullptr;
};

// The audio editor's "Lighting" tab: the cue's lighting triggers as a list on
// the left and an editor for the selected one on the right.
//
// Every edit builds a new LightTriggers vector and goes through commit(): an
// undoable "lightTriggers" field edit when an undo stack is set, otherwise a
// direct setField. The panel re-reads the cue on its changed() signal, so undo,
// remotes and the timeline's marker lane all show up here; the selection is
// kept by trigger id across those refreshes.
class LightTriggersPanel : public QWidget {
    Q_OBJECT
public:
    explicit LightTriggersPanel(QWidget *parent = nullptr);

    void setCue(audio::AudioCue *cue);                 // QPointer inside
    void setUndoStack(QUndoStack *undo);               // may be null
    void setWorkspace(core::Workspace *ws);            // for Fire-cue picker
    void setMidiPortsProvider(std::function<QStringList()> f);
    void setCursorSeconds(double s);                   // editor edit cursor
    void selectTrigger(const QUuid &id);

    QUuid selectedTrigger() const { return m_selectedId; }

    // Mutations the timeline's marker lane asks for. All go through commit().
    // end <= start (e.g. -1) = a point. addTrigger selects and returns the new one.
    QUuid addTrigger(double start, double end = -1.0);
    void  moveTrigger(const QUuid &id, double start, double end);
    // "rename", "toggleRange", "toggleEnabled", "delete".
    void  applyTriggerAction(const QUuid &id, const QString &action);

    // Re-reads the lighting desk in Preferences: the header line and what the
    // editors' simple mode offers. Also done whenever the panel is shown.
    void refreshDesk();

    // ── Beat grid (the strip under the desk line) ─────────────────────────
    // Every grid edit goes to the cue's "beatGrid" field through the undo
    // stack. Spin edits merge; a tap run, a Detect and Set-to-cursor are each
    // their own step.
    void setBeatGrid(const audio::BeatGrid &g, bool mergeable = false);
    // Where the editor's preview is (seconds), or < 0 when it isn't playing.
    void setPlayheadProvider(std::function<double()> f);
    // Renders the song for Detect: interleaved stereo float + its rate. Runs on
    // the UI thread; the analysis itself runs on a worker.
    void setDetectSource(std::function<bool(std::vector<float> &stereo, int &sampleRate)> f);
    void setSongLength(double seconds);          // for Fill's "Whole song"

    // Tap (button / T key). `seconds` is any monotonic clock; from the second
    // tap it sets the BPM. With playheadSeconds >= 0 (preview playing) the first
    // beat moves onto the tap, reduced by whole beats to the earliest >= 0.
    void tap();
    void tapAt(double seconds, double playheadSeconds = -1.0);
    // Detect: renders through the detect source, then detectFromPcm.
    void detectTempo();
    // Analyses interleaved PCM off the UI thread, then sets bpm + first beat in
    // one undo step. Emits tempoDetected when done (bpm 0 = nothing found).
    void detectFromPcm(std::vector<float> interleaved, int sampleRate, int channels = 2);
    bool isDetecting() const { return m_detecting; }

    bool snapToBeats() const;
    void setSnapToBeats(bool on);                // also saved in QSettings

    // Adds point triggers on every `every`-th beat in [from, to) sending
    // `action`, in one undo step. Returns how many.
    int  fillWithBeats(double from, double to, int every,
                       const audio::TriggerAction &action, const QString &namePrefix);
    // "Fill with beats…": the dialog, then fillWithBeats.
    void openFillDialog();

public slots:
    void flashTrigger(const QUuid &id);                // briefly highlight a row (amber) when it fires live

signals:
    void testRequested(const quewi::audio::TriggerAction &action);
    void triggersEdited();                             // after a commit
    void selectionChanged(const QUuid &id);
    void deskSettingsRequested();                      // "Change…" next to the desk line
    void snapToBeatsChanged(bool on);
    void tempoDetected(double bpm, double firstBeat, double confidence);

protected:
    void showEvent(QShowEvent *e) override;

private:
    // The single write path. `mergeable` edits (field tweaks) fold into the
    // previous undo step; structural ones (add, delete, move…) stay separate.
    void commit(const audio::LightTriggers &next, bool mergeable);
    // Applies f to the selected trigger and commits.
    void editSelected(const std::function<void(audio::LightTrigger &)> &f, bool mergeable = true);
    void refresh();
    void loadEditor();
    void setSelected(const QUuid &id);
    void colourRow(int row);
    QString actionText(const audio::TriggerAction &a) const;
    int indexOf(const QUuid &id) const;      // in the cue's vector, -1 if none
    QString nextName() const;
    void commitGrid(const audio::BeatGrid &g, int mergeId);   // -1 = own step
    void loadGrid();                         // cue → strip widgets
    void finishDetect(const audio::TempoEstimate &est, audio::AudioCue *forCue);

    QPointer<audio::AudioCue>  m_cue;
    QPointer<QUndoStack>       m_undo;
    QPointer<core::Workspace>  m_workspace;
    QUuid  m_selectedId;
    QUuid  m_editorId;                       // whose fields the editor shows
    double m_cursor = 0.0;
    bool   m_loading = false;
    QSet<QUuid> m_flashing;
    QList<QUuid> m_rowIds;                   // table row → trigger id

    QLabel       *m_deskLabel = nullptr;

    // Beat grid strip
    QDoubleSpinBox *m_bpm = nullptr;
    QPushButton    *m_tapBtn = nullptr;
    QPushButton    *m_detectBtn = nullptr;
    QDoubleSpinBox *m_firstBeat = nullptr;
    QSpinBox       *m_beatsPerBar = nullptr;
    QCheckBox      *m_snap = nullptr;
    QPushButton    *m_fillBtn = nullptr;
    QLabel         *m_gridStatus = nullptr;
    audio::TapTempo m_tapTempo;
    QElapsedTimer   m_tapClock;
    bool            m_hadTap = false;
    int             m_tapRun = 0;
    bool            m_detecting = false;
    double          m_songLength = 0.0;
    std::function<double()> m_playhead;
    std::function<bool(std::vector<float> &, int &)> m_detectSource;
    std::function<QStringList()> m_midiPorts;
    QTableWidget *m_table = nullptr;
    QLabel       *m_cursorLabel = nullptr;
    QPushButton  *m_duplicateBtn = nullptr;
    QPushButton  *m_deleteBtn = nullptr;

    QStackedWidget *m_editorStack = nullptr;   // placeholder | form
    QLineEdit      *m_name = nullptr;
    QDoubleSpinBox *m_start = nullptr;
    QCheckBox      *m_isRange = nullptr;
    QDoubleSpinBox *m_end = nullptr;
    QCheckBox      *m_enabled = nullptr;
    TriggerActionEditor *m_enter = nullptr;
    TriggerActionEditor *m_exit  = nullptr;
};

} // namespace quewi::ui
