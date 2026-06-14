#include "core/CartGrid.h"

#include <QJsonArray>

namespace quewi::core {

CartGrid::CartGrid(QObject *parent) : QObject(parent)
{
    // Invariant: there is always at least one layer.
    m_layers.append(CartLayer{ tr("Layer 1"), {} });
}

CartGrid::~CartGrid() = default;

QHash<int, CartCell> &CartGrid::cells()
{
    if (m_active < 0 || m_active >= m_layers.size()) m_active = 0;
    return m_layers[m_active].cells;
}

const QHash<int, CartCell> &CartGrid::cells() const
{
    const int i = (m_active >= 0 && m_active < m_layers.size()) ? m_active : 0;
    return m_layers[i].cells;
}

void CartGrid::setSize(int rows, int cols)
{
    rows = std::max(1, std::min(rows, 32));
    cols = std::max(1, std::min(cols, 999));
    if (rows == m_rows && cols == m_cols) return;
    m_rows = rows;
    m_cols = cols;
    // Drop pads that fell outside the new bounds — across every layer, since
    // the board size is shared.
    for (auto &layer : m_layers) {
        for (auto it = layer.cells.begin(); it != layer.cells.end(); ) {
            const int r = it.key() / 1000;
            const int c = it.key() % 1000;
            if (r >= m_rows || c >= m_cols) it = layer.cells.erase(it);
            else                            ++it;
        }
    }
    emit layoutChanged();
}

// ── Layers ──────────────────────────────────────────────────────────────────

void CartGrid::setActiveLayer(int index)
{
    if (index < 0 || index >= m_layers.size() || index == m_active) return;
    m_active = index;
    emit layersChanged();   // update the layer switcher's highlight
    emit layoutChanged();   // a different page of pads is now visible
}

QString CartGrid::layerName(int index) const
{
    if (index < 0 || index >= m_layers.size()) return {};
    return m_layers[index].name;
}

void CartGrid::setLayerName(int index, const QString &name)
{
    if (index < 0 || index >= m_layers.size() || name.isEmpty()) return;
    if (m_layers[index].name == name) return;
    m_layers[index].name = name;
    emit layersChanged();
}

int CartGrid::addLayer(const QString &name)
{
    const QString n = name.isEmpty()
        ? tr("Layer %1").arg(m_layers.size() + 1)
        : name;
    m_layers.append(CartLayer{ n, {} });
    m_active = int(m_layers.size()) - 1;
    emit layersChanged();
    emit layoutChanged();
    return m_active;
}

void CartGrid::removeLayer(int index)
{
    if (m_layers.size() <= 1) return;               // keep at least one layer
    if (index < 0 || index >= m_layers.size()) return;
    m_layers.removeAt(index);
    // Keep the active index in range and pointing at a neighbour the operator
    // would expect (the one that slid into this slot, or the new last layer).
    if (m_active >= m_layers.size()) m_active = int(m_layers.size()) - 1;
    else if (m_active > index)       --m_active;
    emit layersChanged();
    emit layoutChanged();
}

// ── Cell access ─────────────────────────────────────────────────────────────

CartCell CartGrid::cell(int row, int col) const
{
    return cells().value(packKey(row, col));
}

QUuid CartGrid::cueAt(int row, int col) const
{
    return cells().value(packKey(row, col)).cueId;
}

CartCell &CartGrid::mutableCell(int row, int col)
{
    return cells()[packKey(row, col)];
}

void CartGrid::setCell(int row, int col, const QUuid &cueId)
{
    if (row < 0 || row >= m_rows || col < 0 || col >= m_cols) return;
    const int k = packKey(row, col);
    auto &cs = cells();
    if (cueId.isNull()) {
        // Clear the binding but keep any look/trigger the operator set, so
        // re-dropping a sound onto a styled pad keeps its colour/hotkey.
        auto it = cs.find(k);
        if (it == cs.end()) return;
        it->cueId = QUuid();
        if (it->color == QColor() && it->label.isEmpty()
            && it->hotkey.isEmpty() && it->midiNote < 0) {
            cs.erase(it); // fully blank pad — drop it
        }
    } else {
        cs[k].cueId = cueId;
    }
    emit layoutChanged();
}

void CartGrid::clearCell(int row, int col)
{
    if (cells().remove(packKey(row, col))) emit layoutChanged();
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
    auto &cs = cells();
    // A hotkey is unique within a layer — clear it from any other pad first.
    // (Different layers may reuse the same key, since only one fires at a time.)
    if (!hotkey.isEmpty()) {
        for (auto it = cs.begin(); it != cs.end(); ++it)
            if (it.key() != packKey(row, col) && it->hotkey == hotkey)
                it->hotkey.clear();
    }
    mutableCell(row, col).hotkey = hotkey;
    emit layoutChanged();
}

void CartGrid::setCellMidiNote(int row, int col, int note)
{
    if (row < 0 || row >= m_rows || col < 0 || col >= m_cols) return;
    auto &cs = cells();
    if (note >= 0) {
        for (auto it = cs.begin(); it != cs.end(); ++it)
            if (it.key() != packKey(row, col) && it->midiNote == note)
                it->midiNote = -1;
    }
    mutableCell(row, col).midiNote = note;
    emit layoutChanged();
}

QPair<int,int> CartGrid::cellOfCue(const QUuid &cueId) const
{
    if (cueId.isNull()) return {-1, -1};
    const auto &cs = cells();
    for (auto it = cs.constBegin(); it != cs.constEnd(); ++it)
        if (it.value().cueId == cueId)
            return { it.key() / 1000, it.key() % 1000 };
    return {-1, -1};
}

QPair<int,int> CartGrid::cellOfHotkey(const QString &hotkey) const
{
    if (hotkey.isEmpty()) return {-1, -1};
    const auto &cs = cells();
    for (auto it = cs.constBegin(); it != cs.constEnd(); ++it)
        if (!it.value().cueId.isNull() && it.value().hotkey == hotkey)
            return { it.key() / 1000, it.key() % 1000 };
    return {-1, -1};
}

QPair<int,int> CartGrid::cellOfMidiNote(int note) const
{
    if (note < 0) return {-1, -1};
    const auto &cs = cells();
    for (auto it = cs.constBegin(); it != cs.constEnd(); ++it)
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

// ── Serialisation ───────────────────────────────────────────────────────────

QJsonObject CartGrid::toJson() const
{
    QJsonObject o;
    o.insert(QStringLiteral("rows"), m_rows);
    o.insert(QStringLiteral("cols"), m_cols);
    if (!m_outputDeviceId.isEmpty())
        o.insert(QStringLiteral("outputDevice"),
                 QString::fromLatin1(m_outputDeviceId));

    auto cellsToJson = [](const QHash<int, CartCell> &cs) {
        QJsonArray cells;
        for (auto it = cs.constBegin(); it != cs.constEnd(); ++it) {
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
        return cells;
    };

    QJsonArray layers;
    for (const auto &layer : m_layers) {
        QJsonObject lo;
        lo.insert(QStringLiteral("name"), layer.name);
        lo.insert(QStringLiteral("cells"), cellsToJson(layer.cells));
        layers.append(lo);
    }
    o.insert(QStringLiteral("layers"), layers);
    o.insert(QStringLiteral("activeLayer"), m_active);
    // Legacy mirror: also write the active layer's cells under the old "cells"
    // key so an older quewi build can still open the show and show that page.
    o.insert(QStringLiteral("cells"), cellsToJson(cells()));
    return o;
}

void CartGrid::fromJson(const QJsonObject &o)
{
    m_rows = o.value(QStringLiteral("rows")).toInt(4);
    m_cols = o.value(QStringLiteral("cols")).toInt(6);
    m_outputDeviceId = o.value(QStringLiteral("outputDevice")).toString().toLatin1();
    m_layers.clear();

    auto cellsFromJson = [this](const QJsonArray &arr) {
        QHash<int, CartCell> cs;
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
            cs.insert(packKey(r, c), cell);
        }
        return cs;
    };

    if (o.contains(QStringLiteral("layers"))) {
        const auto larr = o.value(QStringLiteral("layers")).toArray();
        int n = 0;
        for (const auto &lv : larr) {
            const auto lo = lv.toObject();
            CartLayer layer;
            layer.name = lo.value(QStringLiteral("name"))
                           .toString(tr("Layer %1").arg(++n));
            layer.cells = cellsFromJson(lo.value(QStringLiteral("cells")).toArray());
            m_layers.append(layer);
        }
    } else {
        // Legacy show (pre-layers): a single flat "cells" array becomes layer 1.
        CartLayer layer;
        layer.name  = tr("Layer 1");
        layer.cells = cellsFromJson(o.value(QStringLiteral("cells")).toArray());
        m_layers.append(layer);
    }
    if (m_layers.isEmpty())          // never leave the cart without a layer
        m_layers.append(CartLayer{ tr("Layer 1"), {} });

    m_active = o.value(QStringLiteral("activeLayer")).toInt(0);
    if (m_active < 0 || m_active >= m_layers.size()) m_active = 0;

    emit layersChanged();
    emit layoutChanged();
}

} // namespace quewi::core
