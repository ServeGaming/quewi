#include "ui/ChannelEditorDialog.h"

#include "mix/MixShow.h"

#include <QDialogButtonBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

using quewi::mix::Ensemble;
using quewi::mix::MixChannel;
using quewi::mix::MixShow;

namespace quewi::ui {
namespace {

// Channel table columns.
enum { ColStrip = 0, ColName, ColActor, ColBackup, ChannelColCount };

// Read a cell as text, trimmed.
QString cellText(QTableWidget *t, int row, int col)
{
    auto *item = t->item(row, col);
    return item ? item->text().trimmed() : QString();
}

} // namespace

ChannelEditorDialog::ChannelEditorDialog(MixShow *show, QWidget *parent)
    : QDialog(parent), m_show(show)
{
    setWindowTitle(tr("Channels & Ensembles"));
    setMinimumSize(720, 560);

    auto *root = new QVBoxLayout(this);

    // ── Channels ─────────────────────────────────────────────────────
    auto *chanBox = new QGroupBox(tr("Channels"), this);
    auto *chanLayout = new QVBoxLayout(chanBox);

    auto *hint = new QLabel(
        tr("One row per radio mic. Strip is the console channel number; Name is "
           "what you call it in cues. Backup is the spare to switch to if it dies "
           "(0 = none)."), chanBox);
    hint->setWordWrap(true);
    chanLayout->addWidget(hint);

    m_channels = new QTableWidget(0, ChannelColCount, chanBox);
    m_channels->setHorizontalHeaderLabels(
        {tr("Strip"), tr("Name"), tr("Actor"), tr("Backup")});
    m_channels->horizontalHeader()->setSectionResizeMode(ColName, QHeaderView::Stretch);
    m_channels->horizontalHeader()->setSectionResizeMode(ColActor, QHeaderView::Stretch);
    m_channels->verticalHeader()->setVisible(false);
    m_channels->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_channels->setSelectionMode(QAbstractItemView::SingleSelection);
    chanLayout->addWidget(m_channels, 1);

    auto *chanBtns = new QHBoxLayout;
    auto *addChan = new QPushButton(tr("Add channel"), chanBox);
    m_removeChannel = new QPushButton(tr("Remove"), chanBox);
    chanBtns->addWidget(addChan);
    chanBtns->addWidget(m_removeChannel);
    chanBtns->addStretch(1);
    chanLayout->addLayout(chanBtns);

    root->addWidget(chanBox, 3);

    // ── Ensembles ────────────────────────────────────────────────────
    auto *ensBox = new QGroupBox(tr("Ensembles"), this);
    auto *ensOuter = new QVBoxLayout(ensBox);
    auto *ensHint = new QLabel(
        tr("A named group of channels you assign to a DCA as one unit — "
           "\"Ensemble Women\", \"Orchestra\". Editing membership updates every "
           "cue that uses the ensemble."), ensBox);
    ensHint->setWordWrap(true);
    ensOuter->addWidget(ensHint);

    auto *ensCols = new QHBoxLayout;

    // Left: the list of ensembles.
    auto *leftCol = new QVBoxLayout;
    m_ensembles = new QListWidget(ensBox);
    m_ensembles->setMaximumWidth(220);
    leftCol->addWidget(m_ensembles, 1);
    auto *ensBtns = new QHBoxLayout;
    auto *addEns = new QPushButton(tr("Add"), ensBox);
    m_renameEnsemble = new QPushButton(tr("Rename"), ensBox);
    m_removeEnsemble = new QPushButton(tr("Remove"), ensBox);
    ensBtns->addWidget(addEns);
    ensBtns->addWidget(m_renameEnsemble);
    ensBtns->addWidget(m_removeEnsemble);
    leftCol->addLayout(ensBtns);
    ensCols->addLayout(leftCol);

    // Right: membership of the selected ensemble.
    auto *rightCol = new QVBoxLayout;
    rightCol->addWidget(new QLabel(tr("Members"), ensBox));
    m_members = new QTableWidget(0, 1, ensBox);
    m_members->setHorizontalHeaderLabels({tr("Channel")});
    m_members->horizontalHeader()->setStretchLastSection(true);
    m_members->horizontalHeader()->setVisible(false);
    m_members->verticalHeader()->setVisible(false);
    m_members->setEditTriggers(QAbstractItemView::NoEditTriggers);
    rightCol->addWidget(m_members, 1);
    ensCols->addLayout(rightCol, 1);

    ensOuter->addLayout(ensCols, 1);
    root->addWidget(ensBox, 2);

    // ── Close ────────────────────────────────────────────────────────
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::accept);
    root->addWidget(buttons);

    // ── Wiring ───────────────────────────────────────────────────────
    connect(addChan, &QPushButton::clicked, this, &ChannelEditorDialog::addChannel);
    connect(m_removeChannel, &QPushButton::clicked, this, &ChannelEditorDialog::removeSelectedChannel);
    connect(m_channels, &QTableWidget::itemChanged, this, &ChannelEditorDialog::onChannelItemChanged);

    connect(addEns, &QPushButton::clicked, this, &ChannelEditorDialog::addEnsemble);
    connect(m_renameEnsemble, &QPushButton::clicked, this, &ChannelEditorDialog::renameSelectedEnsemble);
    connect(m_removeEnsemble, &QPushButton::clicked, this, &ChannelEditorDialog::removeSelectedEnsemble);
    connect(m_ensembles, &QListWidget::currentRowChanged, this, &ChannelEditorDialog::onEnsembleSelected);
    connect(m_members, &QTableWidget::itemChanged, this, &ChannelEditorDialog::onMemberToggled);

    reloadChannels();
    reloadEnsembleList();
    reloadMembers();
}

ChannelEditorDialog::~ChannelEditorDialog() = default;

// ── Channels ─────────────────────────────────────────────────────────

void ChannelEditorDialog::reloadChannels()
{
    if (!m_show) return;
    m_loading = true;

    const auto channels = m_show->channels();   // sorted by strip
    m_channels->setRowCount(int(channels.size()));
    int row = 0;
    for (const auto &c : channels) {
        auto *strip = new QTableWidgetItem(QString::number(c.strip));
        strip->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        // Stamp the row's current strip so an edit to any cell can find the
        // record — and so a strip renumber knows what it's renumbering FROM.
        strip->setData(Qt::UserRole, c.strip);
        m_channels->setItem(row, ColStrip, strip);
        m_channels->setItem(row, ColName,  new QTableWidgetItem(c.name));
        m_channels->setItem(row, ColActor, new QTableWidgetItem(c.actor));
        auto *backup = new QTableWidgetItem(c.backupStrip ? QString::number(c.backupStrip)
                                                          : QString());
        backup->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_channels->setItem(row, ColBackup, backup);
        ++row;
    }
    m_loading = false;
    m_removeChannel->setEnabled(!channels.isEmpty());
}

int ChannelEditorDialog::nextFreeStrip() const
{
    if (!m_show) return 1;
    int strip = 1;
    while (m_show->hasChannel(strip)) ++strip;
    return strip;
}

void ChannelEditorDialog::addChannel()
{
    if (!m_show) return;
    MixChannel c;
    c.strip = nextFreeStrip();
    c.name  = tr("Ch %1").arg(c.strip);
    m_show->setChannel(c);
    reloadChannels();
    // Also refresh the ensemble membership list — a new channel is a new
    // possible member of every ensemble.
    reloadMembers();

    // Select the new row and jump into editing its name.
    for (int row = 0; row < m_channels->rowCount(); ++row)
        if (cellText(m_channels, row, ColStrip).toInt() == c.strip) {
            m_channels->setCurrentCell(row, ColName);
            m_channels->editItem(m_channels->item(row, ColName));
            break;
        }
}

void ChannelEditorDialog::removeSelectedChannel()
{
    if (!m_show) return;
    const int row = m_channels->currentRow();
    if (row < 0) return;
    const int strip = cellText(m_channels, row, ColStrip).toInt();
    if (strip < 1) return;

    m_show->removeChannel(strip);   // also purges it from ensembles + backups
    reloadChannels();
    reloadMembers();
}

void ChannelEditorDialog::onChannelItemChanged(QTableWidgetItem *item)
{
    if (m_loading || !m_show || !item) return;

    const int row = item->row();
    // The row's record is identified by the strip stamped in the strip cell's
    // UserRole at load time — never by the cell's text, which the user may be
    // editing right now.
    auto *stripItem = m_channels->item(row, ColStrip);
    const int prior = stripItem ? stripItem->data(Qt::UserRole).toInt() : -1;
    if (prior < 1) return;

    if (item->column() == ColStrip) {
        // Renumbering a channel. Validate; refuse a collision rather than
        // silently overwrite another mic.
        bool ok = false;
        const int newStrip = item->text().trimmed().toInt(&ok);
        if (!ok || newStrip < 1) { reloadChannels(); return; }
        if (newStrip == prior) return;
        if (m_show->hasChannel(newStrip)) {
            QMessageBox::warning(this, tr("Strip in use"),
                tr("Channel %1 already exists. Pick a free strip number.").arg(newStrip));
            reloadChannels();
            return;
        }
        // reassignStrip moves the record and every reference (ensembles,
        // backups) in one go.
        m_show->reassignStrip(prior, newStrip);
        reloadChannels();
        reloadMembers();
        return;
    }

    // Name / actor / backup edit on an existing channel.
    MixChannel c = m_show->channel(prior);
    if (!c.isValid()) return;

    switch (item->column()) {
    case ColName:  c.name  = item->text().trimmed(); break;
    case ColActor: c.actor = item->text().trimmed(); break;
    case ColBackup: {
        const QString t = item->text().trimmed();
        c.backupStrip = t.isEmpty() ? 0 : t.toInt();
        if (c.backupStrip < 0) c.backupStrip = 0;
        break;
    }
    default: return;
    }
    m_show->setChannel(c);

    // A name change reworks how this channel reads in the ensemble membership
    // list, so keep those labels current. (Actor/backup don't show there, but
    // refreshing on any commit is cheap and keeps the two panels honest.)
    if (item->column() == ColName)
        reloadMembers();
}

// ── Ensembles ────────────────────────────────────────────────────────

QString ChannelEditorDialog::selectedEnsemble() const
{
    auto *item = m_ensembles->currentItem();
    return item ? item->text() : QString();
}

void ChannelEditorDialog::reloadEnsembleList()
{
    if (!m_show) return;
    const QString keep = selectedEnsemble();
    m_ensembles->clear();
    m_ensembles->addItems(m_show->ensembleNames());   // sorted

    // Restore the selection if the same ensemble still exists.
    for (int i = 0; i < m_ensembles->count(); ++i)
        if (m_ensembles->item(i)->text() == keep) { m_ensembles->setCurrentRow(i); break; }
    if (m_ensembles->currentRow() < 0 && m_ensembles->count() > 0)
        m_ensembles->setCurrentRow(0);

    const bool has = m_ensembles->count() > 0;
    m_renameEnsemble->setEnabled(has);
    m_removeEnsemble->setEnabled(has);
}

void ChannelEditorDialog::addEnsemble()
{
    if (!m_show) return;
    bool ok = false;
    const QString name = QInputDialog::getText(
        this, tr("New ensemble"), tr("Name:"), QLineEdit::Normal, QString(), &ok).trimmed();
    if (!ok || name.isEmpty()) return;
    if (!m_show->ensemble(name).isEmpty() || m_show->ensembleNames().contains(name)) {
        QMessageBox::warning(this, tr("Ensemble exists"),
            tr("An ensemble called \"%1\" already exists.").arg(name));
        return;
    }
    m_show->setEnsemble(name, {});   // empty; membership set via the checkboxes
    reloadEnsembleList();
    // Select the new one so the user can tick members immediately.
    for (int i = 0; i < m_ensembles->count(); ++i)
        if (m_ensembles->item(i)->text() == name) { m_ensembles->setCurrentRow(i); break; }
}

void ChannelEditorDialog::renameSelectedEnsemble()
{
    if (!m_show) return;
    const QString old = selectedEnsemble();
    if (old.isEmpty()) return;

    bool ok = false;
    const QString name = QInputDialog::getText(
        this, tr("Rename ensemble"), tr("Name:"), QLineEdit::Normal, old, &ok).trimmed();
    if (!ok || name.isEmpty() || name == old) return;
    if (m_show->ensembleNames().contains(name)) {
        QMessageBox::warning(this, tr("Ensemble exists"),
            tr("An ensemble called \"%1\" already exists.").arg(name));
        return;
    }
    const Ensemble members = m_show->ensemble(old);
    m_show->removeEnsemble(old);
    m_show->setEnsemble(name, members);
    reloadEnsembleList();
}

void ChannelEditorDialog::removeSelectedEnsemble()
{
    if (!m_show) return;
    const QString name = selectedEnsemble();
    if (name.isEmpty()) return;
    m_show->removeEnsemble(name);
    reloadEnsembleList();
    reloadMembers();
}

void ChannelEditorDialog::onEnsembleSelected()
{
    reloadMembers();
}

void ChannelEditorDialog::reloadMembers()
{
    if (!m_show) return;
    m_loading = true;

    const QString ens = selectedEnsemble();
    const Ensemble members = ens.isEmpty() ? Ensemble{} : m_show->ensemble(ens);
    const auto channels = m_show->channels();

    m_members->setRowCount(int(channels.size()));
    int row = 0;
    for (const auto &c : channels) {
        const QString label = c.name.isEmpty()
            ? tr("Strip %1").arg(c.strip)
            : tr("%1 — strip %2").arg(c.name).arg(c.strip);
        auto *item = new QTableWidgetItem(label);
        item->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        item->setCheckState(members.contains(c.strip) ? Qt::Checked : Qt::Unchecked);
        item->setData(Qt::UserRole, c.strip);
        m_members->setItem(row, 0, item);
        ++row;
    }
    // Nothing selectable to tick if there's no ensemble.
    m_members->setEnabled(!ens.isEmpty());
    m_loading = false;
}

void ChannelEditorDialog::onMemberToggled(QTableWidgetItem *item)
{
    if (m_loading || !m_show || !item) return;
    const QString ens = selectedEnsemble();
    if (ens.isEmpty()) return;

    const int strip = item->data(Qt::UserRole).toInt();
    Ensemble members = m_show->ensemble(ens);
    if (item->checkState() == Qt::Checked) members.insert(strip);
    else                                   members.remove(strip);
    m_show->setEnsemble(ens, members);
}

} // namespace quewi::ui
