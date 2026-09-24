#include "ui/DcaAssignDialog.h"

#include "mix/MixShow.h"
#include "ui/ChannelEditorDialog.h"

#include <QDialogButtonBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

namespace quewi::ui {

using mix::MixShow;

DcaAssignDialog::DcaAssignDialog(MixShow *show, int dca,
                                 const QSet<int> &strips, const QStringList &ensembles,
                                 QWidget *parent)
    : QDialog(parent)
    , m_show(show)
    , m_dca(dca)
    , m_strips(strips)
    , m_ensembles(ensembles)
{
    setWindowTitle(tr("DCA %1 — who's on it?").arg(dca));
    setMinimumWidth(380);

    auto *root = new QVBoxLayout(this);

    auto *hint = new QLabel(
        tr("Tick the mics and ensembles assigned to DCA %1 for this cue.").arg(dca), this);
    hint->setWordWrap(true);
    root->addWidget(hint);

    auto *chanGroup = new QGroupBox(tr("Channels"), this);
    auto *chanLay = new QVBoxLayout(chanGroup);
    m_channelList = new QListWidget(chanGroup);
    chanLay->addWidget(m_channelList);
    root->addWidget(chanGroup, 2);

    auto *ensGroup = new QGroupBox(tr("Ensembles"), this);
    auto *ensLay = new QVBoxLayout(ensGroup);
    m_ensembleList = new QListWidget(ensGroup);
    ensLay->addWidget(m_ensembleList);
    root->addWidget(ensGroup, 1);

    rebuildLists();

    // Shortcut into the channel/ensemble editor for the "I haven't set up my
    // channels yet" case — the picker is useless with nothing to pick, and this
    // is exactly the moment the operator realises it.
    auto *editBtn = new QPushButton(tr("Edit channels && ensembles…"), this);
    connect(editBtn, &QPushButton::clicked, this, [this] {
        // Preserve the ticks made so far before the editor (which may add or
        // rename channels) reshapes the lists.
        m_strips    = selectedStrips();
        m_ensembles = selectedEnsembles();
        if (m_show) {
            ChannelEditorDialog dlg(m_show, this);
            dlg.exec();
        }
        rebuildLists();
    });

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *bottom = new QHBoxLayout();
    bottom->addWidget(editBtn);
    bottom->addStretch(1);
    bottom->addWidget(buttons);
    root->addLayout(bottom);
}

void DcaAssignDialog::rebuildLists()
{
    m_channelList->clear();
    m_ensembleList->clear();
    if (!m_show) return;

    const auto channels = m_show->channels();
    for (const auto &ch : channels) {
        QString label = QString::number(ch.strip);
        if (!ch.name.isEmpty())  label += QStringLiteral(" · %1").arg(ch.name);
        if (!ch.actor.isEmpty()) label += QStringLiteral("  (%1)").arg(ch.actor);
        auto *item = new QListWidgetItem(label, m_channelList);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(m_strips.contains(ch.strip) ? Qt::Checked : Qt::Unchecked);
        item->setData(Qt::UserRole, ch.strip);
    }
    if (channels.isEmpty()) {
        auto *item = new QListWidgetItem(
            tr("No channels yet — use “Edit channels…” below."), m_channelList);
        item->setFlags(Qt::ItemIsEnabled);   // visible, not checkable/selectable
    }

    const auto ensembles = m_show->ensembleNames();
    for (const auto &name : ensembles) {
        auto *item = new QListWidgetItem(name, m_ensembleList);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(m_ensembles.contains(name) ? Qt::Checked : Qt::Unchecked);
        item->setData(Qt::UserRole, name);
    }
    if (ensembles.isEmpty()) {
        auto *item = new QListWidgetItem(tr("No ensembles."), m_ensembleList);
        item->setFlags(Qt::ItemIsEnabled);
    }
}

QSet<int> DcaAssignDialog::selectedStrips() const
{
    QSet<int> out;
    for (int i = 0; i < m_channelList->count(); ++i) {
        auto *item = m_channelList->item(i);
        if ((item->flags() & Qt::ItemIsUserCheckable) && item->checkState() == Qt::Checked)
            out.insert(item->data(Qt::UserRole).toInt());
    }
    return out;
}

QStringList DcaAssignDialog::selectedEnsembles() const
{
    QStringList out;
    for (int i = 0; i < m_ensembleList->count(); ++i) {
        auto *item = m_ensembleList->item(i);
        if ((item->flags() & Qt::ItemIsUserCheckable) && item->checkState() == Qt::Checked)
            out.push_back(item->data(Qt::UserRole).toString());
    }
    return out;
}

} // namespace quewi::ui
