#pragma once

#include "audio/BeatGrid.h"
#include "audio/LightTrigger.h"

#include <QDialog>

#include <functional>
#include <optional>
#include <utility>

class QComboBox;
class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QRadioButton;
class QSpinBox;

namespace quewi::core { class Workspace; }

namespace quewi::ui {

class TriggerActionEditor;

// "Fill with beats…": points on the song's beat grid over a range, each
// sending the same action — "bump sub 3 on every beat of the chorus".
//
//   Range   ( ) Selected range   ( ) From cursor for [N] bars   ( ) Whole song
//   Every   [Beat ▾]
//   Sends   (a TriggerActionEditor in simple mode: Bump sub 1, hold 40% of a beat)
//   Name    [Beat]                       "Adds 32 bumps"
//
// The dialog only chooses; the panel adds triggers() in one undo step.
class FillBeatsDialog : public QDialog {
    Q_OBJECT
public:
    enum class Range { Selected, FromCursor, WholeSong };

    // selected: the selected range trigger's span, if one is selected.
    FillBeatsDialog(const audio::BeatGrid &grid, double cursorSeconds, double songSeconds,
                    std::optional<std::pair<double, double>> selected,
                    QWidget *parent = nullptr);

    void setWorkspace(core::Workspace *ws);
    void setMidiPortsProvider(std::function<QStringList()> f);

    void setRange(Range r);
    Range range() const;
    void setBars(int bars);
    void setEvery(int beats);              // 1, 2, or beatsPerBar
    int  every() const;
    void setAction(const audio::TriggerAction &a);
    audio::TriggerAction action() const;
    void setNamePrefix(const QString &p);
    QString namePrefix() const;           // what the new points are named (and grouped) after

    double from() const;
    double to() const;
    // What OK adds (empty when the grid isn't set).
    audio::LightTriggers triggers() const;
    int count() const;
    TriggerActionEditor *actionEditor() const { return m_action; }

signals:
    void testRequested(const quewi::audio::TriggerAction &action);

private:
    void updateCount();

    audio::BeatGrid m_grid;
    double m_cursor = 0.0;
    double m_song = 0.0;
    std::optional<std::pair<double, double>> m_selected;

    QRadioButton *m_selRange = nullptr;
    QRadioButton *m_fromCursor = nullptr;
    QRadioButton *m_whole = nullptr;
    QSpinBox     *m_bars = nullptr;
    QComboBox    *m_every = nullptr;
    TriggerActionEditor *m_action = nullptr;
    QLineEdit    *m_prefix = nullptr;
    QLabel       *m_count = nullptr;
    QLabel       *m_hint = nullptr;
    QDialogButtonBox *m_buttons = nullptr;
};

} // namespace quewi::ui
