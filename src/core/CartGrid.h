#pragma once

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QUuid>

namespace quewi::core {

// Alternate UI for SFX-style shows: a grid of named buttons, one per
// cue. Tap to fire (the operator doesn't have to scroll a list to find
// the cue), drag a wav onto an empty cell to create one. Lives
// alongside the cue list — every cart cell points at a cue id that's
// still owned by a CueList; the cart is a presentation layer, not a
// second copy of the data.
//
// One cart per workspace in v0.8.1. Multiple carts ("Show A", "Show
// B", "Pre-show", …) can come later — the JSON shape already keeps
// the door open with `rows` and `cols` instead of a fixed grid.
class CartGrid : public QObject {
    Q_OBJECT
public:
    explicit CartGrid(QObject *parent = nullptr);
    ~CartGrid() override;

    int  rows() const { return m_rows; }
    int  cols() const { return m_cols; }
    void setSize(int rows, int cols);

    // Empty QUuid = unbound cell. setCell with a null id clears.
    QUuid cueAt(int row, int col) const;
    void  setCell(int row, int col, const QUuid &cueId);
    void  clearCell(int row, int col);

    // Find which cell currently holds cueId, or {-1,-1} if none.
    QPair<int,int> cellOfCue(const QUuid &cueId) const;

    // First empty cell in row-major order, or {-1,-1} if the cart is
    // full. Used by drag-drop import to pick a default landing spot.
    QPair<int,int> firstEmpty() const;

    QJsonObject toJson() const;
    void        fromJson(const QJsonObject &o);

signals:
    void layoutChanged();   // size or any cell changed

private:
    int m_rows = 4;
    int m_cols = 6;
    // Row-major key: row * 1000 + col. Cap at 999 cols, plenty for a
    // sane grid; switching to QPair<int,int> as the key would let us
    // grow further but bloats the hash for no real-world benefit.
    QHash<int, QUuid> m_cells;

    static int  packKey(int row, int col)  { return row * 1000 + col; }
};

} // namespace quewi::core
