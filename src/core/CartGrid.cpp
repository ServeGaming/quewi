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
    // Drop pads that fell outside the new bounds.
    for (auto it = m_cells.begin(); it != m_cells.end(); ) {
        const int r = it.key() / 1000;
        const int c = it.key() % 1000;
        if (r >= m_rows || c >= m_cols) it = m_cells.erase(it);
        else                            ++it;
    }
    emit layoutChanged();
}

CartCell CartGrid::cell(int row, int col) const
{
    return m_cells.value(packKey(row, col));
}

QUuid CartGrid::cueAt(int row, int col) const
{
    return m_cells.value(packKey(row, col)).cueId;
}

CartCell &CartGrid::mutableCell(int row, int col)
{
    return m_cells[packKey(row, col)];
}

void CartGrid::setCell(int row, int col, const QUuid &cueId)
{
    if (row < 0 || row >= m_rows || col < 0 || col >= m_cols) return;
    const int k = packKey(row, col);
    if (cueId.isNull()) {
        // Clear the binding but keep any look/trigger the operator set, so
        // re-dropping a sound onto a styled pad keeps its colour/hotkey.
        auto it = m_cells.find(k);
        if (it == m_cells.end()) return;
        it->cueId = QUuid();
        if (it->color == QColor() && it->label.isEmpty()
            && it->hotkey.isEmpty() && it->midiNote < 0) {
            m_cells.erase(it); // fully blank pad — drop it
        }
    } else {
        m_cells[k].cueId = cueId;
    }
    emit layoutChanged();
}

void CartGrid::clearCell(int row, int col)
{
    if (m_cells.remove(packKey(row, col))) emit layoutChanged();
}

void CartGrid::setOutputDeviceId(const QByteArray &id)
{
    if (m_outputDeviceId == id) return;
    m_outputDeviceId = id;
    emit layoutChanged();
}

void CartGrid::setCellColor(int row, int col, const QColor &color)
{
    if (row < 0 || row >= m_rows || col < 0 || col >= m_cols) return;
    mutableCell(row, col).color = color;
    emit layoutChanged();
}

void CartGrid::setCellLabel(int row, int col, const QString &label)
{
    if (row < 0 || row >= m_rows || col < 0 || col >= m_cols) return;
    mutableCell(row, col).label = label;
    emit layoutChanged();
}

void CartGrid::setCellHotkey(int row, int col, const QString &hotkey)
{
    if (row < 0 || row >= m_rows || col < 0 || col >= m_cols) return;
    // A hotkey is unique across the board — clear it from any other pad first.
    if (!hotkey.isEmpty()) {
        for (auto it = m_cells.begin(); it != m_cells.end(); ++it)
            if (it.key() != packKey(row, col) && it->hotkey == hotkey)
                it->hotkey.clear();
    }
    mutableCell(row, col).hotkey = hotkey;
    emit layoutChanged();
}

void CartGrid::setCellMidiNote(int row, int col, int note)
{
    if (row < 0 || row >= m_rows || col < 0 || col >= m_cols) return;
    if (note >= 0) {
        for (auto it = m_cells.begin(); it != m_cells.end(); ++it)
            if (it.key() != packKey(row, col) && it->midiNote == note)
                it->midiNote = -1;
    }
    mutableCell(row, col).midiNote = note;
    emit layoutChanged();
}

QPair<int,int> CartGrid::cellOfCue(const QUuid &cueId) const
{
    if (cueId.isNull()) return {-1, -1};
    for (auto it = m_cells.constBegin(); it != m_cells.constEnd(); ++it)
        if (it.value().cueId == cueId)
            return { it.key() / 1000, it.key() % 1000 };
    return {-1, -1};
}

QPair<int,int> CartGrid::cellOfHotkey(const QString &hotkey) const
{
    if (hotkey.isEmpty()) return {-1, -1};
    for (auto it = m_cells.constBegin(); it != m_cells.constEnd(); ++it)
        if (!it.value().cueId.isNull() && it.value().hotkey == hotkey)
            return { it.key() / 1000, it.key() % 1000 };
    return {-1, -1};
}

QPair<int,int> CartGrid::cellOfMidiNote(int note) const
{
    if (note < 0) return {-1, -1};
    for (auto it = m_cells.constBegin(); it != m_cells.constEnd(); ++it)
        if (!it.value().cueId.isNull() && it.value().midiNote == note)
            return { it.key() / 1000, it.key() % 1000 };
    return {-1, -1};
}

QPair<int,int> CartGrid::firstEmpty() const
{
    for (int r = 0; r < m_rows; ++r)
        for (int c = 0; c < m_cols; ++c)
            if (cueAt(r, c).isNull()) return {r, c};
    return {-1, -1};
}

QJsonObject CartGrid::toJson() const
{
    QJsonObject o;
    o.insert(QStringLiteral("rows"), m_rows);
    o.insert(QStringLiteral("cols"), m_cols);
    if (!m_outputDeviceId.isEmpty())
        o.insert(QStringLiteral("outputDevice"),
                 QString::fromLatin1(m_outputDeviceId));
    QJsonArray cells;
    for (auto it = m_cells.constBegin(); it != m_cells.constEnd(); ++it) {
        const CartCell &cell = it.value();
        QJsonObject c;
        c.insert(QStringLiteral("r"), it.key() / 1000);
        c.insert(QStringLiteral("c"), it.key() % 1000);
        c.insert(QStringLiteral("cue"), cell.cueId.toString(QUuid::WithoutBraces));
        if (cell.color.isValid()) c.insert(QStringLiteral("color"), cell.color.name());
        if (!cell.label.isEmpty()) c.insert(QStringLiteral("label"), cell.label);
        if (!cell.hotkey.isEmpty()) c.insert(QStringLiteral("key"), cell.hotkey);
        if (cell.midiNote >= 0) c.insert(QStringLiteral("midi"), cell.midiNote);
        cells.append(c);
    }
    o.insert(QStringLiteral("cells"), cells);
    return o;
}

void CartGrid::fromJson(const QJsonObject &o)
{
    m_rows = o.value(QStringLiteral("rows")).toInt(4);
    m_cols = o.value(QStringLiteral("cols")).toInt(6);
    m_outputDeviceId = o.value(QStringLiteral("outputDevice")).toString().toLatin1();
    m_cells.clear();
    const auto arr = o.value(QStringLiteral("cells")).toArray();
    for (const auto &v : arr) {
        const auto co = v.toObject();
        const int r = co.value(QStringLiteral("r")).toInt();
        const int c = co.value(QStringLiteral("c")).toInt();
        if (r < 0 || c < 0 || r >= m_rows || c >= m_cols) continue;
        CartCell cell;
        cell.cueId    = QUuid(co.value(QStringLiteral("cue")).toString());
        if (co.contains(QStringLiteral("color")))
            cell.color = QColor(co.value(QStringLiteral("color")).toString());
        cell.label    = co.value(QStringLiteral("label")).toString();
        cell.hotkey   = co.value(QStringLiteral("key")).toString();
        cell.midiNote = co.value(QStringLiteral("midi")).toInt(-1);
        // Skip a pad that has neither a cue nor any customisation.
        if (cell.cueId.isNull() && !cell.color.isValid() && cell.label.isEmpty()
            && cell.hotkey.isEmpty() && cell.midiNote < 0)
            continue;
        m_cells.insert(packKey(r, c), cell);
    }
    emit layoutChanged();
}

} // namespace quewi::core
