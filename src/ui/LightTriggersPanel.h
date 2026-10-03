#pragma once

#include "audio/AudioCue.h"
#include "audio/LightTrigger.h"

#include <QFrame>
#include <QPointer>
#include <QSet>
#include <QUuid>
#include <QWidget>

#include <functional>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QStackedWidget;
class QTableWidget;
class QUndoStack;

namespace quewi::core { class Workspace; }

namespace quewi::ui {

// Edits one TriggerAction — what an edge of a lighting trigger sends: nothing,
// an OSC message, a MIDI message, an MSC command or "fire this cue". A kind
// combo picks the page; Presets ▾ fills in the usual desk addresses (Eos,
// grandMA) for the user to adjust. Every change is reported through edited()
// with the whole action; the owner decides how to store it.
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

    // Presets ▾, in menu order. applyPreset fills the fields and emits edited().
    static QStringList presetNames();
    void applyPreset(int index);

signals:
    void edited(const quewi::audio::TriggerAction &action);
    void testRequested(const quewi::audio::TriggerAction &action);

private:
    void load();                       // m_action → widgets
    void change(const std::function<void(audio::TriggerAction &)> &f);
    void fillPorts(QComboBox *combo);
    void fillCues();
    void updateMidiRows();

    audio::TriggerAction m_action;
    bool m_loading = false;
    QPointer<core::Workspace> m_workspace;
    std::function<QStringList()> m_midiPorts;

    QLabel         *m_title = nullptr;
    QComboBox      *m_kind  = nullptr;
    QPushButton    *m_presets = nullptr;
    QPushButton    *m_test  = nullptr;
    QStackedWidget *m_pages = nullptr;

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

public slots:
    void flashTrigger(const QUuid &id);                // briefly highlight a row (amber) when it fires live

signals:
    void testRequested(const quewi::audio::TriggerAction &action);
    void triggersEdited();                             // after a commit
    void selectionChanged(const QUuid &id);

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

    QPointer<audio::AudioCue>  m_cue;
    QPointer<QUndoStack>       m_undo;
    QPointer<core::Workspace>  m_workspace;
    QUuid  m_selectedId;
    QUuid  m_editorId;                       // whose fields the editor shows
    double m_cursor = 0.0;
    bool   m_loading = false;
    QSet<QUuid> m_flashing;
    QList<QUuid> m_rowIds;                   // table row → trigger id

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
