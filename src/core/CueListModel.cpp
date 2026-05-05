#include "core/CueListModel.h"

#include "core/CueList.h"
#include "cues/Cue.h"

#include <QBrush>
#include <QBuffer>
#include <QDataStream>
#include <QFont>
#include <QHash>
#include <QIODevice>
#include <QSet>
#include <QFontDatabase>
#include <QMimeData>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

namespace quewi::core {

// 14×14 line icons for each cue type-key. Drawn into a cached pixmap on
// first use. White ink on transparent background — Qt's icon mode tints
// for selected/disabled rows.
QPixmap CueListModel::iconForType(const QString &typeKey)
{
    static QHash<QString, QPixmap> cache;
    if (auto it = cache.constFind(typeKey); it != cache.constEnd()) return it.value();

    const int sz = 14;
    QPixmap pm(sz, sz);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    // Warm cream ink — matches Theme::tokens().ink100, but core can't
    // include ui, so the value is duplicated here.
    QColor ink(0xE8, 0xE2, 0xD4);
    QPen pen(ink); pen.setWidthF(1.4); pen.setCapStyle(Qt::RoundCap); pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen); p.setBrush(Qt::NoBrush);

    auto family = [&](const QString &k) -> QChar { // category dispatch
        if (k == QLatin1String("audio") || k == QLatin1String("mic")) return 'A';
        if (k == QLatin1String("fade"))   return 'F';
        if (k == QLatin1String("light") || k == QLatin1String("light-fade")) return 'L';
        if (k == QLatin1String("video") || k == QLatin1String("image") || k == QLatin1String("text")) return 'V';
        if (k == QLatin1String("osc"))    return 'O';
        if (k == QLatin1String("midi") || k == QLatin1String("msc")) return 'M';
        if (k == QLatin1String("wait"))   return 'W';
        if (k == QLatin1String("group"))  return 'G';
        if (k == QLatin1String("memo"))   return 'N';
        if (k == QLatin1String("start") || k == QLatin1String("stop") || k == QLatin1String("goto")) return 'T';
        return '?';
    };

    switch (family(typeKey).unicode()) {
    case 'A': // speaker triangle + waves
        p.drawLine(3, 6, 6, 6); p.drawLine(3, 8, 6, 8);
        p.drawLine(6, 4, 8, 6); p.drawLine(6, 10, 8, 8); p.drawLine(8, 6, 8, 8);
        p.drawArc(8, 4, 4, 6, 90 * 16, -180 * 16);
        break;
    case 'F': // diagonal ramp
        p.drawLine(2, 11, 12, 3);
        p.drawLine(2, 11, 12, 11);
        break;
    case 'L': // sun rays
        p.drawEllipse(QPointF(7, 7), 2.0, 2.0);
        for (int i = 0; i < 8; ++i) {
            const double a = i * M_PI / 4.0;
            const double cx = 7 + std::cos(a) * 4.0;
            const double cy = 7 + std::sin(a) * 4.0;
            const double cx2 = 7 + std::cos(a) * 6.0;
            const double cy2 = 7 + std::sin(a) * 6.0;
            p.drawLine(QPointF(cx, cy), QPointF(cx2, cy2));
        }
        break;
    case 'V': // play triangle in box
        p.drawRect(1, 2, 12, 10);
        { QPainterPath pp; pp.moveTo(5, 5); pp.lineTo(10, 7); pp.lineTo(5, 9); pp.closeSubpath();
          p.fillPath(pp, ink); }
        break;
    case 'O': // OSC: open circle with arrow
        p.drawEllipse(2, 2, 10, 10);
        p.drawLine(6, 7, 11, 7);
        p.drawLine(9, 5, 11, 7); p.drawLine(9, 9, 11, 7);
        break;
    case 'M': // MIDI: 5-pin dots
        p.drawEllipse(2, 4, 10, 8);
        for (int i = 0; i < 5; ++i) {
            const double a = M_PI + i * (M_PI / 4);
            const double cx = 7 + std::cos(a) * 3.0;
            const double cy = 8 + std::sin(a) * 2.5;
            p.setBrush(ink); p.drawEllipse(QPointF(cx, cy), 0.9, 0.9); p.setBrush(Qt::NoBrush);
        }
        break;
    case 'W': // clock
        p.drawEllipse(2, 2, 10, 10);
        p.drawLine(7, 7, 7, 4); p.drawLine(7, 7, 10, 7);
        break;
    case 'G': // stack
        p.drawRect(2, 3, 10, 3);
        p.drawRect(2, 8, 10, 3);
        break;
    case 'N': // memo: lined sheet
        p.drawRect(3, 2, 8, 11);
        p.drawLine(5, 5, 9, 5); p.drawLine(5, 7, 9, 7); p.drawLine(5, 9, 9, 9);
        break;
    case 'T': // control: target
        p.drawEllipse(3, 3, 8, 8);
        p.drawEllipse(QPointF(7, 7), 1.5, 1.5);
        break;
    default:  // unknown
        p.drawEllipse(3, 3, 8, 8);
        p.drawText(QRect(0,0,sz,sz), Qt::AlignCenter, QStringLiteral("?"));
        break;
    }
    p.end();
    cache.insert(typeKey, pm);
    return cache.value(typeKey);
}

void CueListModel::setRunningCueIds(const QSet<QUuid> &running)
{
    if (m_runningIds == running) return;
    m_runningIds = running;
    if (m_list && m_list->cueCount() > 0)
        emit dataChanged(index(0, ColumnState),
                         index(m_list->cueCount() - 1, ColumnState),
                         { Qt::DecorationRole, Qt::BackgroundRole });
}

void CueListModel::setLoadedCueIds(const QSet<QUuid> &loaded)
{
    if (m_loadedIds == loaded) return;
    m_loadedIds = loaded;
    if (m_list && m_list->cueCount() > 0)
        emit dataChanged(index(0, ColumnState),
                         index(m_list->cueCount() - 1, ColumnState),
                         { Qt::DecorationRole });
}

CueListModel::CueListModel(QObject *parent)
    : QAbstractItemModel(parent)
{
}

CueListModel::~CueListModel() = default;

void CueListModel::setCueList(CueList *list)
{
    if (m_list.data() == list) return;
    beginResetModel();
    disconnectList();
    m_list = list;
    connectList();
    endResetModel();
}

void CueListModel::connectList()
{
    if (!m_list) return;
    connect(m_list, &CueList::aboutToInsertCue, this, &CueListModel::onAboutToInsert);
    connect(m_list, &CueList::cueInserted,      this, &CueListModel::onInserted);
    connect(m_list, &CueList::aboutToRemoveCue, this, &CueListModel::onAboutToRemove);
    connect(m_list, &CueList::cueRemoved,       this, &CueListModel::onRemoved);
    connect(m_list, &CueList::cueChanged,       this, &CueListModel::onCueChanged);
}

void CueListModel::disconnectList()
{
    if (!m_list) return;
    disconnect(m_list, nullptr, this, nullptr);
}

cues::Cue *CueListModel::cueAt(const QModelIndex &index) const
{
    if (!m_list || !index.isValid()) return nullptr;
    return m_list->cueAt(index.row());
}

QModelIndex CueListModel::indexForCue(const cues::Cue *cue, int column) const
{
    if (!m_list || !cue) return {};
    int row = m_list->rowOf(cue);
    if (row < 0) return {};
    return index(row, column);
}

QModelIndex CueListModel::index(int row, int column, const QModelIndex &parent) const
{
    if (parent.isValid() || !m_list) return {};
    if (row < 0 || row >= m_list->cueCount()) return {};
    if (column < 0 || column >= ColumnCount) return {};
    return createIndex(row, column);
}

QModelIndex CueListModel::parent(const QModelIndex &) const
{
    return {};
}

int CueListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid() || !m_list) return 0;
    return m_list->cueCount();
}

int CueListModel::columnCount(const QModelIndex &) const
{
    return ColumnCount;
}

QVariant CueListModel::data(const QModelIndex &index, int role) const
{
    auto *cue = cueAt(index);
    if (!cue) return {};

    if (role == CueIdRole)      return QVariant::fromValue(cue->id());
    if (role == CuePointerRole) return QVariant::fromValue(static_cast<void *>(cue));

    if (role == Qt::DisplayRole || role == Qt::EditRole) {
        auto fmtFieldDouble = [&](const QString &k, int decimals = 2) -> QVariant {
            QVariant v = cue->field(k);
            if (!v.isValid()) return QStringLiteral("—");
            return QString::number(v.toDouble(), 'f', decimals);
        };
        auto fmtFieldString = [&](const QString &k) -> QVariant {
            QVariant v = cue->field(k);
            const auto s = v.toString();
            return s.isEmpty() ? QStringLiteral("—") : s;
        };

        switch (index.column()) {
        case ColumnState:     return QString();   // dot drawn by delegate via DecorationRole
        case ColumnNumber:    return QString::number(cue->number(), 'f', 2);
        case ColumnType:      return cue->typeName();
        case ColumnName:      return cue->name();
        case ColumnPreWait:   return cue->preWait() > 0
                                  ? QString::number(cue->preWait(), 'f', 2)
                                  : QStringLiteral("—");
        case ColumnPostWait:  return cue->postWait() > 0
                                  ? QString::number(cue->postWait(), 'f', 2)
                                  : QStringLiteral("—");
        case ColumnNotes:     return cue->notes();
        case ColumnGain:      return fmtFieldDouble(QStringLiteral("gainDb"), 1);
        case ColumnPan:       return fmtFieldDouble(QStringLiteral("pan"),    2);
        case ColumnFadeIn:    return fmtFieldDouble(QStringLiteral("fadeInSeconds"));
        case ColumnFadeOut:   return fmtFieldDouble(QStringLiteral("fadeOutSeconds"));
        case ColumnOutput: {
            const auto v = cue->field(QStringLiteral("outputDeviceId")).toString();
            return v.isEmpty() ? QStringLiteral("default") : v;
        }
        case ColumnTarget: {
            const auto v = cue->field(QStringLiteral("targetId"));
            return v.isValid() && !v.toString().isEmpty() ? v.toString() : QStringLiteral("—");
        }
        case ColumnHost:      return fmtFieldString(QStringLiteral("host"));
        case ColumnPort: {
            const auto v = cue->field(QStringLiteral("port"));
            return v.isValid() ? QString::number(v.toInt()) : QStringLiteral("—");
        }
        case ColumnFile: {
            const auto v = cue->field(QStringLiteral("filePath")).toString();
            if (v.isEmpty()) return QStringLiteral("—");
            int slash = v.lastIndexOf(QLatin1Char('/'));
            int back  = v.lastIndexOf(QLatin1Char('\\'));
            return v.mid(std::max(slash, back) + 1);
        }
        default:              return {};
        }
    }

    if (role == Qt::TextAlignmentRole) {
        switch (index.column()) {
        case ColumnState:     return int(Qt::AlignCenter);
        case ColumnNumber:
        case ColumnPreWait:
        case ColumnPostWait:  return int(Qt::AlignRight | Qt::AlignVCenter);
        case ColumnType:      return int(Qt::AlignCenter);
        default:              return int(Qt::AlignLeft | Qt::AlignVCenter);
        }
    }

    if (role == Qt::FontRole) {
        if (index.column() == ColumnNumber
            || index.column() == ColumnPreWait
            || index.column() == ColumnPostWait) {
            QFont f = QFontDatabase::systemFont(QFontDatabase::FixedFont);
            f.setStyleHint(QFont::Monospace);
            if (index.column() == ColumnNumber) {
                f.setBold(true);
                f.setPointSizeF(f.pointSizeF() + 1.0);
            }
            return f;
        }
    }

    if (role == Qt::BackgroundRole) {
        // Tint every column of the row by the cue's chosen color (if any).
        // Lighten the colour first so it reads vividly against the warm
        // bg even at moderate alpha — a 12 % wash of a saturated colour
        // looks muddy, but a lightened version pops without overpowering.
        const auto col = cue->color();
        if (col.isValid()) {
            QColor tint = col.lighter(150);
            tint.setAlphaF(0.22f);
            return QBrush(tint);
        }
    }

    if (role == Qt::DecorationRole) {
        if (index.column() == ColumnState) {
            const bool running = m_runningIds.contains(cue->id());
            const bool loaded  = m_loadedIds.contains(cue->id());
            // Token values mirror Theme::tokens() so the dot stays in sync
            // with the QSS palette without core depending on ui.
            QColor c;
            if (running)              c = QColor(0x6F, 0xAE, 0x63); // running (mossy green)
            else if (loaded)          c = QColor(0x4F, 0x8E, 0xAF); // loaded (dusty blue)
            else if (cue->color().isValid()) c = cue->color();
            else c = cue->isArmed() ? QColor(0xB5, 0xAC, 0x9C)      // ink60
                                     : QColor(0x7A, 0x73, 0x68);    // ink40

            QPixmap pm(14, 14);
            pm.fill(Qt::transparent);
            QPainter p(&pm);
            p.setRenderHint(QPainter::Antialiasing);
            p.setPen(Qt::NoPen);
            p.setBrush(c);
            const int sz = (running || loaded) ? 12 : 8;
            const int off = (14 - sz) / 2;
            p.drawEllipse(off, off, sz, sz);
            if (running) {
                // Soft halo for running cues — easier to spot at a glance.
                QColor halo = c; halo.setAlphaF(0.35f);
                p.setBrush(halo);
                p.drawEllipse(0, 0, 14, 14);
            }
            return pm;
        }
        if (index.column() == ColumnType) {
            return iconForType(cue->typeKey());
        }
    }

    return {};
}

QVariant CueListModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return {};
    return columnLabel(section);
}

QString CueListModel::columnLabel(int section)
{
    switch (section) {
    case ColumnState:    return QString();
    case ColumnNumber:   return tr("#");
    case ColumnType:     return tr("Type");
    case ColumnName:     return tr("Name");
    case ColumnPreWait:  return tr("Pre");
    case ColumnPostWait: return tr("Post");
    case ColumnNotes:    return tr("Notes");
    case ColumnGain:     return tr("Gain");
    case ColumnPan:      return tr("Pan");
    case ColumnFadeIn:   return tr("Fade In");
    case ColumnFadeOut:  return tr("Fade Out");
    case ColumnOutput:   return tr("Output");
    case ColumnTarget:   return tr("Target");
    case ColumnHost:     return tr("Host");
    case ColumnPort:     return tr("Port");
    case ColumnFile:     return tr("File");
    default:             return {};
    }
}

QString CueListModel::columnSettingsKey(int section)
{
    switch (section) {
    case ColumnGain:    return QStringLiteral("gain");
    case ColumnPan:     return QStringLiteral("pan");
    case ColumnFadeIn:  return QStringLiteral("fadeIn");
    case ColumnFadeOut: return QStringLiteral("fadeOut");
    case ColumnOutput:  return QStringLiteral("output");
    case ColumnTarget:  return QStringLiteral("target");
    case ColumnHost:    return QStringLiteral("host");
    case ColumnPort:    return QStringLiteral("port");
    case ColumnFile:    return QStringLiteral("file");
    default:            return {};
    }
}

bool CueListModel::columnIsOptional(int section)
{
    return section >= ColumnGain && section < ColumnCount;
}

Qt::ItemFlags CueListModel::flags(const QModelIndex &index) const
{
    Qt::ItemFlags base = Qt::ItemIsDropEnabled; // dropping above/below rows
    if (!index.isValid()) return base;
    return base | Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled;
}

QStringList CueListModel::mimeTypes() const
{
    return { QStringLiteral("application/x-quewi-cue-row") };
}

QMimeData *CueListModel::mimeData(const QModelIndexList &indexes) const
{
    auto *m = new QMimeData;
    QByteArray payload;
    QDataStream ds(&payload, QIODevice::WriteOnly);
    QSet<int> rows;
    for (const auto &idx : indexes) if (idx.isValid()) rows.insert(idx.row());
    QList<int> sorted(rows.begin(), rows.end());
    std::sort(sorted.begin(), sorted.end());
    ds << static_cast<qint32>(sorted.size());
    for (int r : sorted) ds << static_cast<qint32>(r);
    m->setData(QStringLiteral("application/x-quewi-cue-row"), payload);
    return m;
}

void CueListModel::onAboutToInsert(int row) { beginInsertRows({}, row, row); }
void CueListModel::onInserted(int)          { endInsertRows(); }
void CueListModel::onAboutToRemove(int row) { beginRemoveRows({}, row, row); }
void CueListModel::onRemoved(int)           { endRemoveRows(); }
void CueListModel::onCueChanged(int row)
{
    emit dataChanged(index(row, 0), index(row, ColumnCount - 1));
}

} // namespace quewi::core
