#include "ui/MixView.h"

#include "core/CueList.h"
#include "core/UndoCommands.h"
#include "core/Workspace.h"
#include "mix/Dm7Link.h"
#include "mix/MixCue.h"
#include "mix/MixShow.h"
#include "mix/X32Link.h"
#include "ui/ChannelEditorDialog.h"
#include "ui/MixGridModel.h"
#include "ui/Theme.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTableView>
#include <QShortcut>
#include <QToolButton>
#include <QVBoxLayout>
#include <QUndoStack>

using quewi::mix::ConsoleLink;
using quewi::mix::Dm7Link;
using quewi::mix::MixCue;
using quewi::mix::X32Link;

namespace quewi::ui {

MixView::MixView(QWidget *parent) : QWidget(parent)
{
    buildUi();
}

MixView::~MixView() = default;

void MixView::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(6);

    // ── Connection bar ───────────────────────────────────────────────
    auto *bar = new QHBoxLayout;
    bar->setSpacing(6);

    bar->addWidget(new QLabel(tr("Console:"), this));

    m_protocol = new QComboBox(this);
    m_protocol->addItem(tr("Behringer X32 / Midas M32"), QStringLiteral("x32"));
    m_protocol->addItem(tr("Yamaha DM7"), QStringLiteral("dm7"));
    bar->addWidget(m_protocol);

    m_host = new QLineEdit(this);
    m_host->setPlaceholderText(tr("IP address"));
    // No discovery: Yamaha has none at all, and X32 broadcast doesn't cross
    // routers. Typing the IP is the honest interface.
    m_host->setMaximumWidth(160);
    bar->addWidget(m_host);

    m_connect = new QPushButton(tr("Connect"), this);
    connect(m_connect, &QPushButton::clicked, this, &MixView::onConnectClicked);
    bar->addWidget(m_connect);

    m_status = new QLabel(tr("Not connected"), this);
    m_status->setStyleSheet(QStringLiteral("color: %1;").arg(Theme::tokens().ink60.name()));
    bar->addWidget(m_status);

    bar->addStretch(1);

    bar->addWidget(new QLabel(tr("DCAs:"), this));
    m_dcaCount = new QSpinBox(this);
    m_dcaCount->setRange(mix::MixShow::kMinDcaCount, mix::MixShow::kMaxDcaCount);
    m_dcaCount->setToolTip(tr("How many DCA columns this show uses.\n"
                              "An X32 has 8; a DM7 has 24."));
    connect(m_dcaCount, &QSpinBox::valueChanged, this, [this](int v) {
        if (m_workspace && m_workspace->mixShow()) m_workspace->mixShow()->setDcaCount(v);
    });
    bar->addWidget(m_dcaCount);

    root->addLayout(bar);

    // ── Cue actions ──────────────────────────────────────────────────
    auto *actions = new QHBoxLayout;
    actions->setSpacing(6);

    m_go = new QPushButton(tr("GO"), this);
    m_go->setToolTip(tr("Apply the selected cue to the console, then advance."));
    connect(m_go, &QPushButton::clicked, this, [this] { fireSelected(); });
    actions->addWidget(m_go);

    auto *addBtn = new QPushButton(tr("Add cue"), this);
    connect(addBtn, &QPushButton::clicked, this, &MixView::onAddCue);
    actions->addWidget(addBtn);

    auto *delBtn = new QPushButton(tr("Delete cue"), this);
    connect(delBtn, &QPushButton::clicked, this, &MixView::onDeleteCue);
    actions->addWidget(delBtn);

    actions->addStretch(1);

    // The channel/ensemble editor. Without it, the grid can only hold bare
    // strip numbers and the change-highlighting stays inert (resolve() drops
    // unregistered strips), so this is where a usable mix show actually begins.
    // "&&" so Qt doesn't eat the ampersand as a mnemonic accelerator.
    auto *channelsBtn = new QPushButton(tr("Channels && ensembles…"), this);
    connect(channelsBtn, &QPushButton::clicked, this, &MixView::onEditChannels);
    actions->addWidget(channelsBtn);

    root->addLayout(actions);

    // ── Warning banner ───────────────────────────────────────────────
    //
    // Reserved for the things that make the tool silently wrong rather than
    // visibly broken — Scene Safe being off, or losing the /xremote slot. Both
    // are invisible until a show goes wrong, so they get a banner, not a
    // status-bar blip.
    m_warning = new QLabel(this);
    m_warning->setWordWrap(true);
    m_warning->setVisible(false);
    m_warning->setStyleSheet(QStringLiteral(
        "background: %1; color: %2; border-radius: 3px; padding: 6px;")
        .arg(QColor(Theme::tokens().err).darker(180).name(),
             Theme::tokens().ink100.name()));
    root->addWidget(m_warning);

    // ── Grid ─────────────────────────────────────────────────────────
    m_model = new MixGridModel(this);
    connect(m_model, &MixGridModel::cueEdited, this, &MixView::onCueEdited);

    m_table = new QTableView(this);
    m_table->setModel(m_model);
    // The main transport bar's DCA GO tracks the selection (its playhead), so
    // tell it whenever the selected row moves.
    connect(m_table->selectionModel(), &QItemSelectionModel::currentChanged,
            this, [this] { emit mixStateChanged(); });
    // No alternating rows — the cue list makes the same call in a paragraph of
    // comment: on a long show they read as noise, and a single calm surface
    // with the change-tints doing the talking scans far better.
    m_table->setAlternatingRowColors(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectItems);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_table->horizontalHeader()->setDefaultSectionSize(110);
    // Hide the row-number gutter: the "Cue" column already IS the identifier
    // (1.00, 2.00…), so the numbers only duplicated it — and their header was
    // the beveled corner button. Gone, both problems with it.
    m_table->verticalHeader()->setVisible(false);
    m_table->verticalHeader()->setDefaultSectionSize(24);
    m_table->setEditTriggers(QAbstractItemView::DoubleClicked
                             | QAbstractItemView::SelectedClicked
                             | QAbstractItemView::EditKeyPressed);
    root->addWidget(m_table, 1);

    // Scoped to this widget: the set list has its own GO on Space, and the two
    // must never fight over a keypress.
    auto *goSc = new QShortcut(QKeySequence(Qt::Key_Space), this);
    goSc->setContext(Qt::WidgetWithChildrenShortcut);
    connect(goSc, &QShortcut::activated, this, [this] { fireSelected(); });

    auto *delSc = new QShortcut(QKeySequence::Delete, this);
    delSc->setContext(Qt::WidgetWithChildrenShortcut);
    connect(delSc, &QShortcut::activated, this, &MixView::onDeleteCue);

    refreshConnectionUi();
}

void MixView::setWorkspace(core::Workspace *ws)
{
    m_workspace = ws;
    auto *show = ws ? ws->mixShow() : nullptr;
    m_model->setMixShow(show);
    if (show) {
        QSignalBlocker b(m_dcaCount);
        m_dcaCount->setValue(show->dcaCount());
    }
}

void MixView::setCueList(core::CueList *list)
{
    m_model->setCueList(list);
    if (m_model->rowCount() > 0)
        m_table->setCurrentIndex(m_model->index(0, MixGridModel::kFixedCols));
    emit mixStateChanged();
}

MixCue *MixView::selectedCue() const
{
    const auto idx = m_table->currentIndex();
    return idx.isValid() ? m_model->cueAt(idx.row()) : nullptr;
}

// ── Cue editing ──────────────────────────────────────────────────────

void MixView::addCue()
{
    auto *list = m_model->cueList();
    if (!list || !m_workspace) return;

    auto cue = std::make_unique<mix::MixCue>();

    // Number it after the selection, so inserting mid-show doesn't force a
    // renumber. A .5 between neighbours is the convention the rest of quewi
    // already uses.
    const int selRow = m_table->currentIndex().isValid() ? m_table->currentIndex().row() : -1;
    const int insertAt = (selRow >= 0) ? selRow + 1 : list->cueCount();

    double number = 1.0;
    if (auto *prev = m_model->cueAt(insertAt - 1)) {
        auto *next = m_model->cueAt(insertAt);
        number = next ? (prev->number() + next->number()) / 2.0 : prev->number() + 1.0;
    }
    cue->setField(QStringLiteral("number"), number);

    // Carry the previous cue's assignments forward as the starting point.
    // Scenes change a few mics, not all of them — starting from blank would
    // mean retyping the entire cast on every cue.
    if (auto *prev = m_model->cueAt(insertAt - 1)) {
        for (int dca : prev->assignedDcas()) {
            cue->setDcaStrips(dca, prev->dcaStrips(dca));
            cue->setDcaEnsembles(dca, prev->dcaEnsembles(dca));
        }
    }

    if (auto *stack = m_workspace->undoStack())
        stack->push(new core::InsertCueCommand(list, insertAt, std::move(cue)));
    else
        list->insertCue(insertAt, std::move(cue));

    m_table->setCurrentIndex(m_model->index(insertAt, MixGridModel::kColName));
    m_table->edit(m_table->currentIndex());
}

void MixView::deleteSelectedCue()
{
    auto *list = m_model->cueList();
    const auto idx = m_table->currentIndex();
    if (!list || !idx.isValid() || !m_workspace) return;

    const int row = idx.row();
    if (auto *stack = m_workspace->undoStack())
        stack->push(new core::RemoveCueCommand(list, row));
    else
        list->takeCue(row);

    if (m_model->rowCount() > 0)
        m_table->setCurrentIndex(m_model->index(qMin(row, m_model->rowCount() - 1),
                                                idx.column()));
}

void MixView::onEditChannels()
{
    if (!m_workspace || !m_workspace->mixShow()) return;
    // Modeless would let you edit channels while watching the grid, but modal
    // is simpler and the grid already updates live from MixShow's signals when
    // the dialog closes — or even while it's open, since it mutates the show
    // directly. Modal keeps the interaction obvious.
    ChannelEditorDialog dlg(m_workspace->mixShow(), this);
    dlg.exec();
}

// ── Connection ───────────────────────────────────────────────────────

void MixView::onConnectClicked()
{
    if (m_link && m_link->state() != ConsoleLink::State::Disconnected) {
        m_link->disconnectFromConsole();
        m_link.reset();
        setLiveCue(nullptr);   // nothing is on the desk once we're unplugged
        refreshConnectionUi();
        return;
    }

    const QString host = m_host->text().trimmed();
    if (host.isEmpty()) {
        m_host->setFocus();
        emit statusMessage(tr("Enter the console's IP address first."));
        return;
    }

    const QString proto = m_protocol->currentData().toString();
    if (proto == QLatin1String("dm7")) m_link = std::make_unique<Dm7Link>();
    else                               m_link = std::make_unique<X32Link>();

    connect(m_link.get(), &ConsoleLink::stateChanged, this, &MixView::onLinkState);
    connect(m_link.get(), &ConsoleLink::resyncRequired, this, &MixView::onResyncRequired);
    connect(m_link.get(), &ConsoleLink::errorOccurred, this, [this](const QString &m) {
        emit statusMessage(tr("Console: %1").arg(m));
    });

    if (auto *x32 = qobject_cast<X32Link *>(m_link.get())) {
        connect(x32, &X32Link::sceneSafeGroupsChanged, this, [this] { checkSceneSafe(); });
        connect(x32, &X32Link::remoteRegistrationLost, this, [this] {
            // The console accepts only four remote clients. Silently not
            // receiving surface changes is the worst failure mode there is for
            // this tool, so it gets the banner.
            m_warning->setText(tr(
                "The console isn't reporting changes back to quewi. It allows only four "
                "remote clients at once — X32-Edit, a tablet, or Companion may have taken "
                "the last slot. Close one and reconnect."));
            m_warning->setVisible(true);
        });
    }
    if (auto *dm7 = qobject_cast<Dm7Link *>(m_link.get())) {
        connect(dm7, &Dm7Link::splitModeDetected, this, [this](bool split) {
            if (!split) return;
            m_warning->setText(tr(
                "This DM7 is running in Split mode, so its DCAs are divided between two "
                "units. quewi Mix hasn't been tested against a split console — check your "
                "DCA numbering carefully before running a show on it."));
            m_warning->setVisible(true);
        });
    }

    m_link->connectToConsole(host);
    refreshConnectionUi();
}

void MixView::onLinkState(ConsoleLink::State state)
{
    refreshConnectionUi();
    if (state != ConsoleLink::State::Connected || !m_link) return;

    const auto caps = m_link->capabilities();
    emit statusMessage(tr("Connected to %1 (firmware %2)")
                           .arg(caps.model, caps.firmware));

    // Offer to widen the grid rather than doing it silently: the show's DCA
    // count is the operator's decision, and a DM7 has 24 where the default is 8.
    if (m_workspace && m_workspace->mixShow()
        && caps.dcaCount > m_workspace->mixShow()->dcaCount()) {
        const auto answer = QMessageBox::question(
            this, tr("More DCAs available"),
            tr("This %1 has %2 DCAs; this show is set up for %3.\n\n"
               "Use all %2?")
                .arg(caps.model).arg(caps.dcaCount).arg(m_workspace->mixShow()->dcaCount()),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
        if (answer == QMessageBox::Yes) {
            m_workspace->mixShow()->setDcaCount(caps.dcaCount);
            QSignalBlocker b(m_dcaCount);
            m_dcaCount->setValue(m_workspace->mixShow()->dcaCount());
        }
    }
    checkSceneSafe();
}

void MixView::checkSceneSafe()
{
    auto *x32 = qobject_cast<X32Link *>(m_link.get());
    if (!x32 || x32->state() != ConsoleLink::State::Connected) return;

    if (x32->sceneSafeGroupsEnabled()) {
        m_warning->setVisible(false);
        return;
    }

    // This is the one that would quietly ruin a show: with Scene Safe "Groups"
    // off, any scene recall — by the operator, or by a cue — silently reverts
    // every assignment quewi made. No error, nothing on screen, the mix just
    // stops matching the script.
    m_warning->setText(tr(
        "Scene Safe \"Groups\" is OFF on this console. If any scene is recalled, the desk "
        "will silently undo every DCA assignment quewi makes — mid-show, with no warning. "
        "Turn it on before running a show."));
    m_warning->setVisible(true);
}

void MixView::onResyncRequired(const QString &reason)
{
    emit statusMessage(tr("%1 Re-reading console state.").arg(reason));
    setLiveCue(nullptr);   // our view of the desk is gone; nothing is "applied"
}

void MixView::setLiveCue(mix::MixCue *cue)
{
    m_liveCue = cue;
    // Keep the grid's live marker and our own pointer as one fact. The model
    // paints the row; we compare against it for live edits. If these two ever
    // disagree, the operator sees a "live" row that isn't the one being pushed.
    m_model->setLiveCue(cue);
}

void MixView::refreshConnectionUi()
{
    const auto state = m_link ? m_link->state() : ConsoleLink::State::Disconnected;
    const auto &t = Theme::tokens();

    QString text;
    QColor  colour = t.ink60;
    switch (state) {
    case ConsoleLink::State::Disconnected: text = tr("Not connected"); break;
    case ConsoleLink::State::Connecting:   text = tr("Connecting…");   colour = t.warn; break;
    case ConsoleLink::State::Connected:
        text = tr("Connected — %1").arg(m_link->capabilities().model);
        colour = t.running;
        break;
    case ConsoleLink::State::Failed:
        text = m_link->lastError().isEmpty() ? tr("Connection failed") : m_link->lastError();
        colour = t.err;
        break;
    }
    m_status->setText(text);
    m_status->setStyleSheet(QStringLiteral("color: %1;").arg(colour.name()));

    const bool up = (state == ConsoleLink::State::Connected
                     || state == ConsoleLink::State::Connecting);
    m_connect->setText(up ? tr("Disconnect") : tr("Connect"));
    m_protocol->setEnabled(!up);
    m_host->setEnabled(!up);

    if (state == ConsoleLink::State::Disconnected) m_warning->setVisible(false);

    // Connection state gates the DCA GO — refresh the transport bar.
    emit mixStateChanged();
}

// ── Firing ───────────────────────────────────────────────────────────

bool MixView::fireSelected()
{
    auto *cue = selectedCue();
    if (!cue || !m_workspace || !m_workspace->mixShow()) return false;
    if (!m_link || m_link->state() != ConsoleLink::State::Connected) {
        emit statusMessage(tr("No console connected."));
        return false;
    }

    m_link->applyCue(cue->channelAssignments(*m_workspace->mixShow()));
    setLiveCue(cue);

    const QString label = cue->name().isEmpty()
                        ? tr("Cue %1").arg(cue->number())
                        : cue->name();
    emit statusMessage(tr("Fired %1").arg(label));

    // Advance, the way a cue list does — the operator's next GO should be the
    // next cue without them having to reach for the mouse.
    const auto idx = m_table->currentIndex();
    if (idx.isValid() && idx.row() + 1 < m_model->rowCount())
        m_table->setCurrentIndex(m_model->index(idx.row() + 1, idx.column()));
    return true;
}

bool MixView::canFireNext() const
{
    return m_link && m_link->state() == ConsoleLink::State::Connected && selectedCue();
}

QString MixView::dcaGoTooltip() const
{
    if (!m_model->cueList())
        return tr("Add a Mix (DCA) list and connect a console to fire DCA cues.");
    if (!m_link || m_link->state() != ConsoleLink::State::Connected)
        return tr("Connect a console on the Mix page to fire DCA cues.");
    auto *cue = selectedCue();
    if (!cue)
        return tr("No DCA cue selected.");
    const QString name = cue->name().isEmpty()
                       ? tr("Cue %1").arg(cue->number())
                       : cue->name();
    return tr("DCA GO → %1  %2").arg(QString::number(cue->number(), 'f', 2), name);
}

void MixView::onCueEdited(MixCue *cue)
{
    // Live edit: if the operator changes the cue that's currently ON the desk,
    // push it. Editing any other cue is just programming and must not touch a
    // live console mid-show.
    if (!cue || cue != m_liveCue) return;
    if (!m_link || m_link->state() != ConsoleLink::State::Connected) return;
    if (!m_workspace || !m_workspace->mixShow()) return;

    m_link->applyCue(cue->channelAssignments(*m_workspace->mixShow()));
}

} // namespace quewi::ui
