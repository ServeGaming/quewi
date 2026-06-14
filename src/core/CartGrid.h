#pragma once

#include <QByteArray>
#include <QColor>
#include <QHash>
#include <QJsonObject>
#include <QList>
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

// A soundboard "layer" — one full page of pads. Layers let an operator stack
// several boards behind a single soundboard tab (Act 1 / Act 2 / spot FX, say)
// and flip between them; only the active layer is shown and fires. Every layer
// shares the board's row/col size and references cues from the same cue lists.
struct CartLayer {
    QString name;
    QHash<int, CartCell> cells;   // row-major key: row * 1000 + col
};

// The sound-effect board model: a stack of pad layers. Tap (or hotkey, or MIDI,
// or OSC) to fire the active layer's pads. Pads reference cues by id; the cart
// is a presentation layer, not a second copy of the cue data. Persists into the
// show file.
class CartGrid : public QObject {
    Q_OBJECT
public:
    explicit CartGrid(QObject *parent = nullptr);
    ~CartGrid() override;

    int  rows() const { return m_rows; }
    int  cols() const { return m_cols; }
    void setSize(int rows, int cols);

    // ── Layers ──────────────────────────────────────────────────────────
    // The cart always has at least one layer. Cell access below operates on
    // whichever layer is active.
    int     layerCount() const { return int(m_layers.size()); }
    int     activeLayer() const { return m_active; }
    void    setActiveLayer(int index);
    QString layerName(int index) const;
    void    setLayerName(int index, const QString &name);
    // Append a new empty layer and make it active. Returns its index.
    int     addLayer(const QString &name = QString());
    // Remove a layer (no-op if it's the only one). Keeps the active index in
    // range and pointing at a sensible neighbour.
    void    removeLayer(int index);

    // ── Cell access (operates on the ACTIVE layer) ──────────────────────
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

    // Lookups used by the trigger paths (active layer).
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
    void layoutChanged();   // size or any cell changed (rebuild the visible grid)
    void layersChanged();   // a layer was added/removed/renamed, or active changed

private:
    int m_rows = 4;
    int m_cols = 6;
    // Always ≥ 1 layer. m_active indexes into m_layers.
    QList<CartLayer> m_layers;
    int m_active = 0;
    QByteArray m_outputDeviceId;   // empty = engine default output

    static int packKey(int row, int col) { return row * 1000 + col; }
    // The active layer's cell map. Both overloads assume the layer invariant
    // (m_layers non-empty, m_active in range) which every mutator upholds.
    QHash<int, CartCell>       &cells();
    const QHash<int, CartCell> &cells() const;
    CartCell &mutableCell(int row, int col); // creates if absent
};

} // namespace quewi::core
