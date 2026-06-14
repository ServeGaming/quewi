#pragma once

#include <QByteArray>
#include <QColor>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QUuid>

namespace quewi::core {

// One pad on the sound-effect board. A pad points at a cue (still owned by
// a CueList) and carries presentation/trigger overrides so each operator can
// lay the board out to fit them: a custom colour and label, a keyboard
// hotkey, and a MIDI note so a pad controller / Launchpad can fire it.
struct CartCell {
    QUuid   cueId;             // null = empty pad
    QColor  color;             // invalid = fall back to the cue's colour
    QString label;             // empty   = fall back to the cue's name
    QString hotkey;            // QKeySequence portable string ("Q", "Shift+1"); empty = none
    int     midiNote = -1;     // -1 = none

    bool isEmpty() const { return cueId.isNull(); }
};

// The sound-effect board model: a grid of pads. Tap (or hotkey, or MIDI, or
// OSC) to fire. Pads reference cues by id; the cart is a presentation layer,
// not a second copy of the cue data. Persists into the show file.
class CartGrid : public QObject {
    Q_OBJECT
public:
    explicit CartGrid(QObject *parent = nullptr);
    ~CartGrid() override;

    int  rows() const { return m_rows; }
    int  cols() const { return m_cols; }
    void setSize(int rows, int cols);

    // ── Cell access ────────────────────────────────────────────────────
    CartCell cell(int row, int col) const;
    QUuid    cueAt(int row, int col) const;          // convenience: cell().cueId

    // Bind / clear the cue a pad fires. setCell preserves any colour/label/
    // hotkey/MIDI overrides already on the pad; a null id clears the binding
    // but keeps the pad's look (clearCell wipes the whole pad).
    void setCell(int row, int col, const QUuid &cueId);
    void clearCell(int row, int col);

    // Per-pad customisation.
    void setCellColor (int row, int col, const QColor  &color);
    void setCellLabel (int row, int col, const QString &label);
    void setCellHotkey(int row, int col, const QString &hotkey);
    void setCellMidiNote(int row, int col, int note);

    // Lookups used by the trigger paths.
    QPair<int,int> cellOfCue(const QUuid &cueId) const;
    QPair<int,int> cellOfHotkey(const QString &hotkey) const;  // first match
    QPair<int,int> cellOfMidiNote(int note) const;             // first match
    QPair<int,int> firstEmpty() const;

    // The whole board routes to this output device (empty = engine default).
    // Voicemod-style: send every pad to a chosen device (e.g. a virtual
    // cable) independent of the main playback device. Applied as a fire-time
    // override so the shared cues themselves are never mutated.
    QByteArray outputDeviceId() const { return m_outputDeviceId; }
    void       setOutputDeviceId(const QByteArray &id);

    QJsonObject toJson() const;
    void        fromJson(const QJsonObject &o);

signals:
    void layoutChanged();   // size or any cell changed

private:
    int m_rows = 4;
    int m_cols = 6;
    // Row-major key: row * 1000 + col (≤ 999 cols).
    QHash<int, CartCell> m_cells;
    QByteArray m_outputDeviceId;   // empty = engine default output

    static int packKey(int row, int col) { return row * 1000 + col; }
    CartCell &mutableCell(int row, int col); // creates if absent
};

} // namespace quewi::core
