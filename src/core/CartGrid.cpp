#include "core/CartGrid.h"

#include <QJsonArray>

namespace quewi::core {

CartGrid::CartGrid(QObject *parent) : QObject(parent) {}
CartGrid::~CartGrid() = default;

void CartGrid::setSize(int rows, int cols)
{
    rows = std::max(1, std::min(rows, 32));
    cols = std::max(1, std::min(cols, 999));
    if (rows == m_rows && cols == m_cols) return;
    m_rows = rows;
    m_cols = cols;
    // Drop cells that fell outside the new bounds.
    for (auto it = m_cells.begin(); it != m_cells.end(); ) {
        const int r = it.key() / 1000;
        const int c = it.key() % 1000;
        if (r >= m_rows || c >= m_cols) it = m_cells.erase(it);
        else                            ++it;
    }
    emit layoutChanged();
}

QUuid CartGrid::cueAt(int row, int col) const
{
    return m_cells.value(packKey(row, col));
}

void CartGrid::setCell(int row, int col, const QUuid &cueId)
{
    if (row < 0 || row >= m_rows || col < 0 || col >= m_cols) return;
    const int k = packKey(row, col);
    if (cueId.isNull()) m_cells.remove(k);
    else                m_cells.insert(k, cueId);
    emit layoutChanged();
}

void CartGrid::clearCell(int row, int col)
{
    setCell(row, col, QUuid());
}

QPair<int,int> CartGrid::cellOfCue(const QUuid &cueId) const
{
    if (cueId.isNull()) return {-1, -1};
    for (auto it = m_cells.constBegin(); it != m_cells.constEnd(); ++it) {
        if (it.value() == cueId)
            return { it.key() / 1000, it.key() % 1000 };
    }
    return {-1, -1};
}

QPair<int,int> CartGrid::firstEmpty() const
{
    for (int r = 0; r < m_rows; ++r) {
        for (int c = 0; c < m_cols; ++c) {
            if (!m_cells.contains(packKey(r, c))) return {r, c};
        }
    }
    return {-1, -1};
}

QJsonObject CartGrid::toJson() const
{
    QJsonObject o;
    o.insert(QStringLiteral("rows"), m_rows);
    o.insert(QStringLiteral("cols"), m_cols);
    QJsonArray cells;
    for (auto it = m_cells.constBegin(); it != m_cells.constEnd(); ++it) {
        QJsonObject cell;
        cell.insert(QStringLiteral("r"), it.key() / 1000);
        cell.insert(QStringLiteral("c"), it.key() % 1000);
        cell.insert(QStringLiteral("cue"),
                    it.value().toString(QUuid::WithoutBraces));
        cells.append(cell);
    }
    o.insert(QStringLiteral("cells"), cells);
    return o;
}

void CartGrid::fromJson(const QJsonObject &o)
{
    m_rows = o.value(QStringLiteral("rows")).toInt(4);
    m_cols = o.value(QStringLiteral("cols")).toInt(6);
    m_cells.clear();
    const auto arr = o.value(QStringLiteral("cells")).toArray();
    for (const auto &v : arr) {
        const auto cell = v.toObject();
        const int r = cell.value(QStringLiteral("r")).toInt();
        const int c = cell.value(QStringLiteral("c")).toInt();
        const QUuid id(cell.value(QStringLiteral("cue")).toString());
        if (id.isNull() || r < 0 || c < 0 || r >= m_rows || c >= m_cols) continue;
        m_cells.insert(packKey(r, c), id);
    }
    emit layoutChanged();
}

} // namespace quewi::core
