#include "ui/MatrixView.h"

#include "core/CueList.h"
#include "core/Workspace.h"
#include "osc/EosCueLists.h"
#include "osc/EosFeedback.h"
#include "ui/ShowModeView.h"
#include "ui/Theme.h"

#include <QCheckBox>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QMimeData>
#include <QPushButton>
#include <QTableView>
#include <QTimer>
#include <QVBoxLayout>

#include <cmath>

namespace quewi::ui {

namespace m = core::matrix;

namespace {

// "5", "2.5", "1:30" — a desk time the way the desk shows it.
QString secondsText(double s)
{
    if (s < 0.0) return {};
    if (s >= 60.0) {
        const int whole = int(std::floor(s));
        QString t = QStringLiteral("%1:%2").arg(whole / 60).arg(whole % 60, 2, 10, QLatin1Char('0'));
        const double frac = s - whole;
        if (frac > 0.05) t += QString::number(frac, 'f', 1).mid(1);
        return t;
    }
    QString t = QString::number(s, 'f', 2);
    while (t.endsWith(QLatin1Char('0'))) t.chop(1);
    if (t.endsWith(QLatin1Char('.'))) t.chop(1);
    return t;
}

QString clockText(double s)
{
    const int whole = int(std::floor(std::max(0.0, s) + 1e-6));
    return QStringLiteral("%1:%2").arg(whole / 60).arg(whole % 60, 2, 10, QLatin1Char('0'));
}

QString cellLine(const m::DeskCell &c)
{
    QString t = matrixDeskName(c.cue);
    if (!c.cue.label.isEmpty()) t += QStringLiteral("  ") + c.cue.label;
    if (!c.parts.empty())
        t += MatrixTableModel::tr("  (+%n part(s))", nullptr, int(c.parts.size()));
    if (c.missing) t += MatrixTableModel::tr("  — not on the desk");
    return t;
}

QString cellTime(const m::DeskCell &c)
{
    QString t = secondsText(c.cue.upSeconds);
    const QString down = secondsText(c.cue.downSeconds);
    if (!down.isEmpty() && down != t) t += QStringLiteral(" / ") + down;
    if (c.cue.followSeconds >= 0.0) t += MatrixTableModel::tr("  F %1").arg(secondsText(c.cue.followSeconds));
    if (c.cue.hangSeconds >= 0.0) t += MatrixTableModel::tr("  H %1").arg(secondsText(c.cue.hangSeconds));
    return t;
}

QString howText(const m::DeskCell &c, const MatrixBuild &b, const m::Row &row)
{
    const QString q = row.quewiIndex >= 0 ? b.quewi[size_t(row.quewiIndex)].number : QString();
    switch (c.how) {
    case m::How::Manual:
        return c.missing ? MatrixTableModel::tr("Placed here by hand. The desk doesn't have this cue (any more) — "
                                                "right-click to forget it.")
                         : MatrixTableModel::tr("Placed here by hand. Right-click to put it back automatically.");
    case m::How::Fired:
        return MatrixTableModel::tr("Fired by quewi cue %1 at its GO.").arg(q);
    case m::How::Trigger:
        return MatrixTableModel::tr("Hit %1 after quewi cue %2's GO, by lighting trigger \"%3\".")
            .arg(clockText(c.atSeconds), q, c.triggerName);
    case m::How::Order:
        return MatrixTableModel::tr("Follows the desk's order (nothing in quewi fires it). "
                                    "Drag it onto a quewi cue to tie it there.");
    }
    return {};
}

} // namespace

// ── MatrixTableModel ────────────────────────────────────────────────────

MatrixTableModel::MatrixTableModel(QObject *parent) : QAbstractTableModel(parent) {}

void MatrixTableModel::setBuild(const MatrixBuild &b)
{
    beginResetModel();
    m_build = b;
    endResetModel();
}

void MatrixTableModel::setLive(const MatrixLive &live)
{
    if (live == m_live) return;
    m_live = live;
    if (rowCount() > 0)
        emit dataChanged(index(0, 0), index(rowCount() - 1, ColCount - 1));
}

int MatrixTableModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_build.result.rows.size());
}

int MatrixTableModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColCount;
}

QVariant MatrixTableModel::headerData(int section, Qt::Orientation o, int role) const
{
    if (o != Qt::Horizontal || role != Qt::DisplayRole) return {};
    switch (section) {
    case ColState:  return QString();
    case ColNumber: return tr("Q");
    case ColCue:    return tr("Sound / Video");
    case ColLights: return tr("Lights");
    case ColTime:   return tr("Time");
    case ColNotes:  return tr("Notes / Scene");
    }
    return {};
}

QString MatrixTableModel::stateText(int r) const
{
    const auto &row = m_build.result.rows[size_t(r)];
    QStringList parts;
    if (row.kind == m::Row::Kind::Quewi && row.quewiIndex >= 0) {
        const auto &id = m_build.quewi[size_t(row.quewiIndex)].id;
        if (id == m_live.standby) parts << tr("STANDBY");
        if (m_live.running.contains(id)) parts << tr("PLAYING");
    }
    for (const auto &c : row.desk) {
        if (c.missing) { parts << QStringLiteral("⚠"); continue; }
        if (matrixDeskIs(c.cue, m_live.deskActiveList, m_live.deskActiveCue)) parts << tr("LX LIVE");
        else if (matrixDeskIs(c.cue, m_live.deskPendingList, m_live.deskPendingCue)) parts << tr("LX NEXT");
    }
    parts.removeDuplicates();
    return parts.join(QStringLiteral(" · "));
}

QColor MatrixTableModel::rowTint(int r) const
{
    const auto &row = m_build.result.rows[size_t(r)];
    const auto &tk = Theme::tokens();
    const auto rc = showRegionColours();
    if (row.kind == m::Row::Kind::Quewi && row.quewiIndex >= 0) {
        const auto &id = m_build.quewi[size_t(row.quewiIndex)].id;
        if (id == m_live.standby) return showMix(tk.bgRow, rc.standby, 0.24);
        if (m_live.running.contains(id)) return showMix(tk.bgRow, rc.running, 0.22);
    }
    for (const auto &c : row.desk) {
        if (c.missing) continue;
        if (matrixDeskIs(c.cue, m_live.deskActiveList, m_live.deskActiveCue))
            return showMix(tk.bgRow, rc.desk, 0.32);
    }
    for (const auto &c : row.desk) {
        if (!c.missing && matrixDeskIs(c.cue, m_live.deskPendingList, m_live.deskPendingCue))
            return showMix(tk.bgRow, rc.desk, 0.14);
    }
    return {};
}

QVariant MatrixTableModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= rowCount()) return {};
    const auto &row = m_build.result.rows[size_t(index.row())];
    const m::QuewiCue *q = row.quewiIndex >= 0 ? &m_build.quewi[size_t(row.quewiIndex)] : nullptr;
    const auto rc = showRegionColours();
    const auto &tk = Theme::tokens();

    if (role == Qt::UserRole) return int(row.kind);
    if (role == Qt::BackgroundRole) {
        const QColor c = rowTint(index.row());
        return c.isValid() ? QVariant(c) : QVariant();
    }
    if (role == Qt::ToolTipRole) {
        if (index.column() == ColLights && !row.desk.empty()) {
            QStringList t;
            for (const auto &c : row.desk) t << matrixDeskName(c.cue) + QStringLiteral(": ") + howText(c, m_build, row);
            return t.join(QLatin1Char('\n'));
        }
        if (index.column() == ColCue && q && row.kind == m::Row::Kind::Quewi) return q->type;
        return {};
    }
    if (role == Qt::ForegroundRole) {
        switch (index.column()) {
        case ColState: {
            const QString s = stateText(index.row());
            if (s.contains(tr("STANDBY"))) return rc.standby;
            if (s.contains(tr("PLAYING"))) return rc.running;
            if (s.contains(QStringLiteral("⚠"))) return tk.err;
            return rc.desk;
        }
        case ColNumber:
        case ColCue:
            if (row.kind == m::Row::Kind::Hit) return rc.hits;
            return {};
        case ColLights:
            for (const auto &c : row.desk) if (c.missing) return tk.err;
            return row.kind == m::Row::Kind::Hit ? rc.hits : rc.desk;
        case ColTime:
        case ColNotes:
            return row.kind == m::Row::Kind::Quewi ? QVariant() : QVariant(tk.ink60);
        }
        return {};
    }
    if (role == Qt::FontRole && row.kind == m::Row::Kind::Quewi && q && q->id == m_live.standby) {
        QFont f;
        f.setBold(true);
        return f;
    }
    if (role != Qt::DisplayRole) return {};

    switch (index.column()) {
    case ColState:
        return stateText(index.row());
    case ColNumber:
        return row.kind == m::Row::Kind::Quewi && q ? q->number : QString();
    case ColCue:
        if (row.kind == m::Row::Kind::Quewi && q) return q->name;
        if (row.kind == m::Row::Kind::Hit && !row.desk.empty()) {
            const auto &c = row.desk.front();
            QString t = tr("    ↳ hit at %1").arg(clockText(c.atSeconds));
            if (!c.triggerName.isEmpty()) t += QStringLiteral("  ·  ") + c.triggerName;
            return t;
        }
        return QString();
    case ColLights: {
        QStringList lines;
        for (const auto &c : row.desk) lines << cellLine(c);
        return lines.join(QLatin1Char('\n'));
    }
    case ColTime: {
        QStringList lines;
        for (const auto &c : row.desk) lines << cellTime(c);
        return lines.join(QLatin1Char('\n')).trimmed();
    }
    case ColNotes: {
        QStringList parts;
        if (q && row.kind == m::Row::Kind::Quewi && !q->notes.isEmpty()) parts << q->notes.simplified();
        for (const auto &c : row.desk) {
            if (!c.cue.scene.isEmpty())
                parts << (c.cue.sceneEnd ? tr("Scene ends: %1") : tr("Scene: %1")).arg(c.cue.scene);
            if (!c.cue.notes.isEmpty()) parts << c.cue.notes.simplified();
        }
        return parts.join(QStringLiteral("  ·  "));
    }
    }
    return {};
}

Qt::ItemFlags MatrixTableModel::flags(const QModelIndex &index) const
{
    Qt::ItemFlags f = QAbstractTableModel::flags(index);
    if (m_locked) return f;
    if (!index.isValid()) return f | Qt::ItemIsDropEnabled;
    f |= Qt::ItemIsDropEnabled;
    if (!m_build.result.rows[size_t(index.row())].desk.empty()) f |= Qt::ItemIsDragEnabled;
    return f;
}

QStringList MatrixTableModel::mimeTypes() const { return {QString::fromLatin1(kMime)}; }

Qt::DropActions MatrixTableModel::supportedDropActions() const { return Qt::MoveAction | Qt::CopyAction; }
Qt::DropActions MatrixTableModel::supportedDragActions() const { return Qt::MoveAction; }

QMimeData *MatrixTableModel::mimeData(const QModelIndexList &indexes) const
{
    QJsonArray cues;
    QSet<int> rows;
    for (const auto &i : indexes) rows.insert(i.row());
    QList<int> sorted(rows.begin(), rows.end());
    std::sort(sorted.begin(), sorted.end());
    for (int r : sorted) {
        if (r < 0 || r >= rowCount()) continue;
        for (const auto &c : m_build.result.rows[size_t(r)].desk) cues.append(c.cue.toJson());
    }
    if (cues.isEmpty()) return nullptr;
    auto *md = new QMimeData;
    md->setData(QString::fromLatin1(kMime), QJsonDocument(cues).toJson(QJsonDocument::Compact));
    return md;
}

bool MatrixTableModel::dropTarget(int row, bool onto, m::ManualPlacement::Mode *mode, QUuid *anchor) const
{
    const int n = rowCount();
    *anchor = {};
    if (onto && row >= 0 && row < n) {
        const auto &r = m_build.result.rows[size_t(row)];
        if (r.kind == m::Row::Kind::Quewi && r.quewiIndex >= 0) {
            *mode = m::ManualPlacement::Mode::With;
            *anchor = m_build.quewi[size_t(r.quewiIndex)].id;
            return true;
        }
        ++row;                      // onto a desk/hit row = just after it
    }
    if (row < 0 || row > n) row = n;
    // The quewi cue the row before the gap belongs to.
    const int q = row <= 0 ? -1 : m_build.result.rows[size_t(row - 1)].quewiIndex;
    if (q < 0) {
        *mode = m::ManualPlacement::Mode::Start;
        return true;
    }
    *mode = m::ManualPlacement::Mode::After;
    *anchor = m_build.quewi[size_t(q)].id;
    return true;
}

bool MatrixTableModel::canDropMimeData(const QMimeData *data, Qt::DropAction, int, int,
                                       const QModelIndex &) const
{
    return !m_locked && data && data->hasFormat(QString::fromLatin1(kMime)) && m_list;
}

bool MatrixTableModel::dropMimeData(const QMimeData *data, Qt::DropAction action, int row,
                                    int column, const QModelIndex &parent)
{
    if (!canDropMimeData(data, action, row, column, parent)) return false;
    std::vector<m::DeskCue> cues;
    for (const auto v : QJsonDocument::fromJson(data->data(QString::fromLatin1(kMime))).array())
        cues.push_back(m::DeskCue::fromJson(v.toObject()));
    if (cues.empty()) return false;
    m::ManualPlacement::Mode mode;
    QUuid anchor;
    const bool onto = parent.isValid();
    if (!dropTarget(onto ? parent.row() : row, onto, &mode, &anchor)) return false;
    place(cues, mode, anchor);
    // false: the view mustn't try to remove the dragged rows — the drop
    // re-places cues, it doesn't move model rows.
    return false;
}

void MatrixTableModel::place(const std::vector<m::DeskCue> &cues, m::ManualPlacement::Mode mode,
                             const QUuid &anchor)
{
    if (!m_list || cues.empty()) return;
    auto cfg = m_list->matrixConfig();
    for (const auto &c : cues) cfg.place(c, mode, anchor);
    m_list->setMatrixConfig(cfg);
    emit placementsEdited();
}

// ── MatrixView ──────────────────────────────────────────────────────────

MatrixView::MatrixView(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("matrixView"));
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(6);

    auto *bar = new QHBoxLayout;
    m_title = new QLabel(this);
    m_title->setObjectName(QStringLiteral("matrixTitle"));
    QFont tf = m_title->font();
    tf.setBold(true);
    tf.setPointSizeF(tf.pointSizeF() * 1.15);
    m_title->setFont(tf);
    bar->addWidget(m_title);
    bar->addSpacing(12);
    bar->addWidget(new QLabel(tr("Sound & video from"), this));
    m_source = new QComboBox(this);
    m_source->setObjectName(QStringLiteral("matrixSource"));
    m_source->setToolTip(tr("The quewi cue list to interleave. GO runs this list while the Matrix List is up."));
    bar->addWidget(m_source);
    bar->addSpacing(8);
    bar->addWidget(new QLabel(tr("Lights from desk cue list"), this));
    m_deskList = new QComboBox(this);
    m_deskList->setObjectName(QStringLiteral("matrixDeskList"));
    m_deskList->setEditable(true);
    m_deskList->setMinimumContentsLength(6);
    m_deskList->setToolTip(tr("The Eos cue list to read. Type a number if the desk isn't connected yet."));
    bar->addWidget(m_deskList);
    m_refresh = new QPushButton(tr("Read the desk again"), this);
    m_refresh->setObjectName(QStringLiteral("matrixRefresh"));
    bar->addWidget(m_refresh);
    bar->addStretch(1);
    m_follow = new QCheckBox(tr("Follow the show"), this);
    m_follow->setObjectName(QStringLiteral("matrixFollow"));
    m_follow->setChecked(true);
    m_follow->setToolTip(tr("Keep standby (or the desk's live cue) in view as the show runs."));
    bar->addWidget(m_follow);
    root->addLayout(bar);

    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("matrixStatus"));
    m_status->setWordWrap(true);
    root->addWidget(m_status);

    m_model = new MatrixTableModel(this);
    m_table = new QTableView(this);
    m_table->setObjectName(QStringLiteral("matrixTable"));
    m_table->setModel(m_model);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_table->setDragEnabled(true);
    m_table->setAcceptDrops(true);
    m_table->setDropIndicatorShown(true);
    m_table->setDragDropMode(QAbstractItemView::DragDrop);
    m_table->setDefaultDropAction(Qt::MoveAction);
    m_table->setDragDropOverwriteMode(false);
    m_table->setWordWrap(true);
    m_table->setAlternatingRowColors(true);
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    m_table->verticalHeader()->setVisible(false);
    m_table->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    auto *hh = m_table->horizontalHeader();
    hh->setSectionResizeMode(QHeaderView::Interactive);
    hh->setStretchLastSection(true);
    m_table->setColumnWidth(MatrixTableModel::ColState, 120);
    m_table->setColumnWidth(MatrixTableModel::ColNumber, 54);
    m_table->setColumnWidth(MatrixTableModel::ColCue, 260);
    m_table->setColumnWidth(MatrixTableModel::ColLights, 260);
    m_table->setColumnWidth(MatrixTableModel::ColTime, 90);
    root->addWidget(m_table, 1);

    m_rebuildTimer = new QTimer(this);
    m_rebuildTimer->setSingleShot(true);
    m_rebuildTimer->setInterval(30);
    connect(m_rebuildTimer, &QTimer::timeout, this, &MatrixView::rebuildNow);
    m_liveTimer = new QTimer(this);
    m_liveTimer->setInterval(250);
    connect(m_liveTimer, &QTimer::timeout, this, &MatrixView::pollLive);

    connect(m_model, &MatrixTableModel::placementsEdited, this, [this] {
        emit modified();
        emit statusMessage(tr("Placement saved with the show"));
    });
    connect(m_table, &QTableView::doubleClicked, this, [this](const QModelIndex &i) {
        if (!i.isValid()) return;
        const auto &row = build().result.rows[size_t(i.row())];
        if (row.kind == m::Row::Kind::Quewi && row.quewiIndex >= 0)
            emit standbyRequested(build().quewi[size_t(row.quewiIndex)].id);
    });
    connect(m_table, &QWidget::customContextMenuRequested, this, &MatrixView::contextMenuAt);
    connect(m_source, &QComboBox::activated, this, [this](int i) {
        if (m_updatingCombos) return;
        const QUuid id = m_source->itemData(i).toUuid();
        editConfig([id](m::Config &c) { c.sourceList = id; });
    });
    auto deskPicked = [this] {
        if (m_updatingCombos) return;
        QString n = m_deskList->currentData().toString();
        if (n.isEmpty() || m_deskList->currentText() != m_deskList->itemText(m_deskList->currentIndex()))
            n = m_deskList->currentText().section(QLatin1Char(' '), 0, 0).trimmed();
        n = m::normalNumber(n);
        bool ok = false;
        n.toDouble(&ok);
        if (!ok) { refreshCombos(); return; }
        editConfig([n](m::Config &c) { c.deskList = n; });
        watchDeskList();
    };
    connect(m_deskList, &QComboBox::activated, this, deskPicked);
    connect(m_deskList->lineEdit(), &QLineEdit::editingFinished, this, deskPicked);
    connect(m_refresh, &QPushButton::clicked, this, [this] {
        if (!m_reader || !m_list) return;
        m_reader->refresh();
        m_reader->refreshList(m::normalNumber(m_list->matrixConfig().deskList));
        updateStatus();
    });
    updateStatus();
}

MatrixView::~MatrixView() = default;

void MatrixView::setWorkspace(core::Workspace *ws)
{
    if (m_ws == ws) return;
    if (m_ws) m_ws->disconnect(this);
    m_ws = ws;
    if (m_ws)
        connect(m_ws, &core::Workspace::cueListsChanged, this, [this] {
            refreshCombos();
            scheduleRebuild();
        });
    setCueList(nullptr);
}

void MatrixView::setCueList(core::CueList *matrix)
{
    if (m_list) m_list->disconnect(this);
    m_list = matrix;
    m_model->setMatrixList(matrix);
    if (m_list)
        connect(m_list, &core::CueList::matrixConfigChanged, this, [this] {
            refreshCombos();
            scheduleRebuild();
        });
    m_lastStandbyRow = m_lastDeskRow = -2;
    watchDeskList();
    refreshCombos();
    rebuildNow();
}

void MatrixView::setDesk(osc::EosCueLists *reader, osc::EosFeedback *feedback)
{
    if (m_reader) m_reader->disconnect(this);
    if (m_feedback) m_feedback->disconnect(this);
    m_reader = reader;
    m_feedback = feedback;
    if (m_reader) {
        connect(m_reader, &osc::EosCueLists::cuesChanged, this, &MatrixView::onDeskCues);
        connect(m_reader, &osc::EosCueLists::stateChanged, this, [this](const QString &l) {
            if (m_list && l == m::normalNumber(m_list->matrixConfig().deskList)) {
                updateStatus();
                scheduleRebuild();
            }
        });
        connect(m_reader, &osc::EosCueLists::cueListsChanged, this, &MatrixView::refreshCombos);
    }
    if (m_feedback) connect(m_feedback, &osc::EosFeedback::linkChanged, this, &MatrixView::updateStatus);
    watchDeskList();
    refreshCombos();
    rebuildNow();
}

void MatrixView::setLocked(bool locked)
{
    m_locked = locked;
    m_model->setLocked(locked);
    m_source->setEnabled(!locked);
    m_deskList->setEnabled(!locked);
    m_table->setDragEnabled(!locked);
    m_table->setAcceptDrops(!locked);
}

void MatrixView::watchDeskList()
{
    if (!m_reader || !m_list) return;
    const QString l = m::normalNumber(m_list->matrixConfig().deskList);
    QStringList w = m_reader->watched();
    if (!w.contains(l)) {
        w << l;
        m_reader->setWatched(w);
    }
}

void MatrixView::editConfig(const std::function<void(m::Config &)> &fn)
{
    if (!m_list) return;
    auto cfg = m_list->matrixConfig();
    fn(cfg);
    if (cfg == m_list->matrixConfig()) return;
    m_list->setMatrixConfig(cfg);
    emit modified();
}

void MatrixView::refreshCombos()
{
    m_updatingCombos = true;
    m_source->clear();
    const auto cfg = m_list ? m_list->matrixConfig() : m::Config{};
    auto *src = matrixSourceList(m_ws, m_list);
    if (m_ws)
        for (const auto &l : m_ws->cueLists()) {
            if (l->kind() != core::CueList::Kind::Normal) continue;
            m_source->addItem(l->name(), l->id());
            if (l.get() == src) m_source->setCurrentIndex(m_source->count() - 1);
        }

    m_deskList->clear();
    const QString want = m::normalNumber(cfg.deskList);
    bool found = false;
    if (m_reader)
        for (const auto &info : m_reader->cueLists()) {
            const QString text = info.label.isEmpty()
                ? info.number : QStringLiteral("%1 — %2").arg(info.number, info.label);
            m_deskList->addItem(text, info.number);
            if (info.number == want) {
                m_deskList->setCurrentIndex(m_deskList->count() - 1);
                found = true;
            }
        }
    if (!found) {
        m_deskList->addItem(want, want);
        m_deskList->setCurrentIndex(m_deskList->count() - 1);
    }
    m_title->setText(m_list ? m_list->name() : tr("Matrix List"));
    m_updatingCombos = false;
}

void MatrixView::scheduleRebuild()
{
    m_rebuildTimer->start();
}

void MatrixView::rebuildNow()
{
    m_rebuildTimer->stop();
    MatrixBuild b = buildMatrix(m_ws, m_list, m_reader);

    // The desk renumbered a cue placed by hand (same UID): follow it.
    if (m_list && !b.result.renumbered.empty()) {
        auto cfg = m_list->matrixConfig();
        for (const auto &r : b.result.renumbered)
            if (r.placement >= 0 && size_t(r.placement) < cfg.placements.size()) {
                cfg.placements[size_t(r.placement)].number = r.number;
                cfg.placements[size_t(r.placement)].part = r.part;
            }
        QSignalBlocker block(m_list);
        m_list->setMatrixConfig(cfg);
    }

    // Keep the source list's edits flowing in.
    if (m_sourceWatched != b.source) {
        for (const auto &c : std::as_const(m_listConns)) disconnect(c);
        m_listConns.clear();
        m_sourceWatched = b.source;
        if (b.source) {
            for (auto sig : {&core::CueList::cueInserted, &core::CueList::cueRemoved,
                             &core::CueList::cueChanged})
                m_listConns << connect(b.source, sig, this, &MatrixView::scheduleRebuild);
        }
    }

    // Keep the selection on the same quewi cue across a rebuild.
    QUuid keep;
    const auto cur = m_table->currentIndex();
    if (cur.isValid()) {
        const auto &row = build().result.rows[size_t(cur.row())];
        if (row.quewiIndex >= 0 && size_t(row.quewiIndex) < build().quewi.size())
            keep = build().quewi[size_t(row.quewiIndex)].id;
    }
    m_model->setBuild(b);
    if (!keep.isNull()) {
        const int r = b.rowOfQuewi(keep);
        if (r >= 0) m_table->setCurrentIndex(m_model->index(r, MatrixTableModel::ColCue));
    }
    m_lastStandbyRow = m_lastDeskRow = -2;
    pollLive();
    updateStatus();
}

void MatrixView::onDeskCues(const QString &list)
{
    if (!m_list || !m_reader || list != m::normalNumber(m_list->matrixConfig().deskList)) return;
    // Remember what the desk said, so the matrix still reads right offline.
    if (m_reader->state(list) == osc::EosCueLists::State::Ready) {
        auto cfg = m_list->matrixConfig();
        const auto now = toDeskCues(m_reader->cues(list));
        if (now != cfg.deskCache || !cfg.deskCachedAt.isValid()) {
            cfg.deskCache = now;
            cfg.deskCachedAt = QDateTime::currentDateTime();
            QSignalBlocker block(m_list);   // not an edit of yours: no "unsaved"
            m_list->setMatrixConfig(cfg);
        }
    }
    scheduleRebuild();
}

QString MatrixView::statusText() const
{
    return m_status->text();
}

void MatrixView::updateStatus()
{
    if (!m_list) {
        m_status->clear();
        return;
    }
    const auto cfg = m_list->matrixConfig();
    const QString list = m::normalNumber(cfg.deskList);
    const auto &b = build();
    const bool linkLive = m_feedback && m_feedback->link() == osc::EosFeedback::Link::Live;
    const auto st = m_reader ? m_reader->state(list) : osc::EosCueLists::State::Idle;
    int lx = 0;
    for (const auto &c : b.deskCues) if (c.part == 0) ++lx;
    QString t;
    if (linkLive && st == osc::EosCueLists::State::Fetching) {
        const int got = m_reader->received(list), of = m_reader->expected(list);
        t = of >= 0 ? tr("Reading desk cue list %1… %2 of %3").arg(list).arg(got).arg(of)
                    : tr("Reading desk cue list %1…").arg(list);
    } else if (b.desk == MatrixBuild::Desk::Live && st == osc::EosCueLists::State::Partial) {
        t = tr("Read %1 of %2 cues from desk cue list %3 — the desk stopped answering. "
               "Read the desk again to finish.").arg(m_reader->received(list)).arg(m_reader->expected(list)).arg(list);
    } else if (b.desk == MatrixBuild::Desk::Live) {
        t = tr("Live from the desk: %n cue(s) in cue list %1.", nullptr, lx).arg(list);
    } else if (b.desk == MatrixBuild::Desk::Cached) {
        t = tr("The desk isn't connected — showing cue list %1 as last read (%2).")
                .arg(list, QLocale().toString(cfg.deskCachedAt, QLocale::ShortFormat));
    } else {
        t = tr("No lighting cues yet. Set up an ETC Eos desk in Tools → Lighting Desk… with "
               "\"Read the desk's state back\" on, and quewi reads cue list %1 from it.").arg(list);
    }
    int missing = 0;
    for (const auto &r : b.result.rows) for (const auto &c : r.desk) if (c.missing) ++missing;
    if (missing > 0) t += QLatin1Char(' ') + tr("%n cue(s) marked ⚠ aren't on the desk.", nullptr, missing);
    if (!b.source) t += QLatin1Char(' ') + tr("There's no quewi cue list to interleave yet.");
    m_status->setText(t);
}

void MatrixView::pollLive()
{
    MatrixLive live = m_liveProvider ? m_liveProvider() : MatrixLive{};
    if (m_feedback) {
        live.deskActiveList = m_feedback->active().list;
        live.deskActiveCue = m_feedback->active().cue;
        live.deskPendingList = m_feedback->pending().list;
        live.deskPendingCue = m_feedback->pending().cue;
    }
    m_model->setLive(live);
    const auto &b = build();
    const int sb = b.rowOfQuewi(live.standby);
    const int dk = b.rowOfDesk(live.deskActiveList, live.deskActiveCue);
    if (sb != m_lastStandbyRow && sb >= 0) followTo(sb);
    else if (dk != m_lastDeskRow && dk >= 0) followTo(dk);
    m_lastStandbyRow = sb;
    m_lastDeskRow = dk;
}

void MatrixView::followTo(int row)
{
    if (!m_follow->isChecked() || row < 0 || row >= m_model->rowCount()) return;
    m_table->scrollTo(m_model->index(row, MatrixTableModel::ColCue), QAbstractItemView::PositionAtCenter);
}

void MatrixView::contextMenuAt(const QPoint &pos)
{
    const QModelIndex i = m_table->indexAt(pos);
    QMenu menu(this);
    if (i.isValid()) {
        const auto &row = build().result.rows[size_t(i.row())];
        if (row.kind == m::Row::Kind::Quewi && row.quewiIndex >= 0) {
            const QUuid id = build().quewi[size_t(row.quewiIndex)].id;
            menu.addAction(tr("Make this the standby cue"), this, [this, id] { emit standbyRequested(id); });
        }
        if (!m_locked) {
            for (const auto &c : row.desk) {
                if (c.how != m::How::Manual) continue;
                const auto cue = c.cue;
                const QString text = c.missing ? tr("Forget %1 (not on the desk)").arg(matrixDeskName(cue))
                                               : tr("Put %1 back automatically").arg(matrixDeskName(cue));
                menu.addAction(text, this, [this, cue] {
                    editConfig([&cue](m::Config &cfg) { cfg.unplace(cue.list, cue.number, cue.part); });
                });
            }
        }
    }
    if (!menu.isEmpty()) menu.addSeparator();
    auto *again = menu.addAction(tr("Read the desk again"), m_refresh, &QPushButton::click);
    again->setEnabled(m_reader != nullptr);
    menu.exec(m_table->viewport()->mapToGlobal(pos));
}

void MatrixView::showEvent(QShowEvent *e)
{
    QWidget::showEvent(e);
    m_liveTimer->start();
    pollLive();
}

void MatrixView::hideEvent(QHideEvent *e)
{
    QWidget::hideEvent(e);
    m_liveTimer->stop();
}

} // namespace quewi::ui
