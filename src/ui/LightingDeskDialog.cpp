#include "ui/LightingDeskDialog.h"

#include "midi/MidiEngine.h"
#include "ui/Theme.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QEvent>
#include <QGridLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHostAddress>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QSettings>
#include <QSpinBox>
#include <QStyle>
#include <QVBoxLayout>

namespace quewi::ui {

namespace {

using Type = core::LightingDesk::Type;

int quewiOwnOscPort()
{
    QSettings s(QStringLiteral("ServeGaming"), QStringLiteral("quewi"));
    return s.value(QStringLiteral("osc/udpPort"), 53535).toInt();
}

bool isThisComputer(const QString &host)
{
    const QString h = host.trimmed().toLower();
    return h == QLatin1String("localhost") || h == QLatin1String("127.0.0.1")
        || h == QLatin1String("::1");
}

// IPv4 / IPv6 literal, or a plain hostname (letters, digits, hyphens, dots).
bool looksLikeHost(const QString &text)
{
    const QString h = text.trimmed();
    if (h.isEmpty()) return false;
    if (!QHostAddress(h).isNull()) return true;
    static const QRegularExpression hostRe(QStringLiteral(
        "^[A-Za-z0-9]([A-Za-z0-9-]{0,61}[A-Za-z0-9])?"
        "(\\.[A-Za-z0-9]([A-Za-z0-9-]{0,61}[A-Za-z0-9])?)*$"));
    return hostRe.match(h).hasMatch();
}

QLabel *makeNote(const QString &text, QWidget *parent)
{
    auto *l = new QLabel(text, parent);
    l->setWordWrap(true);
    l->setTextInteractionFlags(Qt::TextSelectableByMouse);
    l->setStyleSheet(QStringLiteral("color:%1;").arg(Theme::tokens().ink60.name()));
    return l;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────

LightingDeskDialog::LightingDeskDialog(QWidget *parent)
    : QDialog(parent)
    , m_loaded(core::LightingDesk::load())
{
    const auto &t = Theme::tokens();
    setWindowTitle(tr("Lighting Desk"));
    setModal(true);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(20, 18, 20, 16);
    root->setSpacing(14);
    // The dialog always fits its content: word-wrapped notes and the
    // per-desk steps make a user-shrinkable height clip, and SetFixedSize
    // also re-fits the window when the desk type swaps the groups.
    root->setSizeConstraint(QLayout::SetFixedSize);
    root->addItem(new QSpacerItem(540, 0, QSizePolicy::Minimum, QSizePolicy::Fixed));

    // ── Intro ─────────────────────────────────────────────────────────
    auto *intro = new QLabel(tr(
        "Lighting triggers in your cues are sent to this desk. "
        "Set it up once per computer — the desk belongs to the venue, not the show."), this);
    intro->setWordWrap(true);
    root->addWidget(intro);

    // ── Desk ──────────────────────────────────────────────────────────
    auto *deskTitle = new QLabel(tr("Desk"), this);
    deskTitle->setStyleSheet(QStringLiteral(
        "color:%1; font-size:11px; font-weight:600; letter-spacing:0.06em;")
        .arg(t.ink60.name()));
    root->addWidget(deskTitle);

    m_typeGroup = new QButtonGroup(this);
    m_typeGroup->setExclusive(true);

    auto *cards = new QVBoxLayout();
    cards->setSpacing(6);
    cards->addWidget(makeDeskCard(Type::Eos, tr("ETC Eos / Ion / Element / Nomad"),
        tr("Eos-family consoles and Nomad, over OSC on the network.")));
    cards->addWidget(makeDeskCard(Type::Ma3, tr("grandMA3"),
        tr("MA Lighting grandMA3, over OSC on the network.")));
    cards->addWidget(makeDeskCard(Type::Ma2Msc, tr("grandMA2 / MSC"),
        tr("grandMA2 or any desk that takes MIDI Show Control, over a MIDI cable.")));
    root->addLayout(cards);

    // ── Connection (OSC desks) ────────────────────────────────────────
    m_oscGroup = new QGroupBox(tr("Connection"), this);
    auto *oscLay = new QVBoxLayout(m_oscGroup);
    oscLay->setSpacing(10);
    // A grid rather than QFormLayout: the inline error / note under the
    // address field are word-wrapped, and QFormLayout's spanning rows don't
    // size those reliably at the dialog's minimum.
    auto *oscGrid = new QGridLayout();
    oscGrid->setHorizontalSpacing(14);
    oscGrid->setVerticalSpacing(8);
    oscGrid->setColumnStretch(1, 1);

    auto *hostLabel = new QLabel(tr("Desk IP address"), m_oscGroup);
    m_host = new QLineEdit(m_loaded.host, m_oscGroup);
    m_host->setObjectName(QStringLiteral("deskHost"));
    m_host->setPlaceholderText(tr("e.g. 192.168.1.100 — or 127.0.0.1 for Nomad on this computer"));
    m_host->setClearButtonEnabled(true);
    oscGrid->addWidget(hostLabel, 0, 0, Qt::AlignRight | Qt::AlignVCenter);
    oscGrid->addWidget(m_host, 0, 1);

    m_hostError = new QLabel(m_oscGroup);
    m_hostError->setObjectName(QStringLiteral("deskHostError"));
    m_hostError->setWordWrap(true);
    m_hostError->setStyleSheet(QStringLiteral("color:%1;").arg(t.err.name()));
    m_hostError->hide();
    oscGrid->addWidget(m_hostError, 1, 1);

    m_hostNote = makeNote(tr("127.0.0.1 is this computer — right for Nomad running here."),
                          m_oscGroup);
    m_hostNote->setObjectName(QStringLiteral("deskHostNote"));
    m_hostNote->hide();
    oscGrid->addWidget(m_hostNote, 2, 1);

    auto *portLabel = new QLabel(tr("Desk's OSC receive port"), m_oscGroup);
    m_port = new QSpinBox(m_oscGroup);
    m_port->setObjectName(QStringLiteral("deskPort"));
    m_port->setRange(1, 65535);
    m_port->setValue(m_loaded.port);
    m_port->setMinimumWidth(110);
    oscGrid->addWidget(portLabel, 3, 0, Qt::AlignRight | Qt::AlignVCenter);
    oscGrid->addWidget(m_port, 3, 1, Qt::AlignLeft);

    m_prefixLabel = new QLabel(tr("OSC prefix"), m_oscGroup);
    m_prefix = new QLineEdit(m_loaded.ma3Prefix, m_oscGroup);
    m_prefix->setObjectName(QStringLiteral("deskPrefix"));
    m_prefix->setPlaceholderText(tr("optional — e.g. gma3"));
    oscGrid->addWidget(m_prefixLabel, 4, 0, Qt::AlignRight | Qt::AlignVCenter);
    oscGrid->addWidget(m_prefix, 4, 1);
    oscLay->addLayout(oscGrid);

    // Eos: read the desk's running / pending cue back for Show Mode and the
    // Lighting panel, over TCP to the same address.
    m_feedback = new QCheckBox(tr("Show what the desk is doing (its running and next cue)"), m_oscGroup);
    m_feedback->setObjectName(QStringLiteral("deskFeedback"));
    m_feedback->setChecked(m_loaded.feedback);
    m_feedback->setToolTip(tr("quewi connects to the desk's OSC TCP port and reads its state. "
                              "It never changes anything on the desk"));
    m_feedbackPort = new QSpinBox(m_oscGroup);
    m_feedbackPort->setObjectName(QStringLiteral("deskFeedbackPort"));
    m_feedbackPort->setRange(1, 65535);
    m_feedbackPort->setValue(m_loaded.feedbackPort);
    m_feedbackPort->setPrefix(tr("TCP "));
    m_feedbackPort->setToolTip(tr("Eos's OSC TCP port: 3032 unless it's been changed"));
    m_feedbackRow = new QWidget(m_oscGroup);
    {
        auto *h = new QHBoxLayout(m_feedbackRow);
        h->setContentsMargins(0, 0, 0, 0);
        h->addWidget(m_feedback, 1);
        h->addWidget(m_feedbackPort);
    }
    connect(m_feedback, &QCheckBox::toggled, m_feedbackPort, &QWidget::setEnabled);
    m_feedbackPort->setEnabled(m_loaded.feedback);
    oscLay->addWidget(m_feedbackRow);

    m_ownPortNote = makeNote(QString(), m_oscGroup);
    m_ownPortNote->setObjectName(QStringLiteral("deskOwnPortNote"));
    oscLay->addWidget(m_ownPortNote);
    root->addWidget(m_oscGroup);

    // ── MIDI (MSC desks) ──────────────────────────────────────────────
    m_mscGroup = new QGroupBox(tr("MIDI"), this);
    auto *mscLay = new QVBoxLayout(m_mscGroup);
    mscLay->setSpacing(8);
    auto *mscGrid = new QGridLayout();
    mscGrid->setHorizontalSpacing(14);
    mscGrid->setVerticalSpacing(8);
    mscGrid->setColumnStretch(1, 1);

    m_midiPort = new QComboBox(m_mscGroup);
    m_midiPort->setObjectName(QStringLiteral("deskMidiPort"));
    m_midiPort->setEditable(true);
    m_midiPort->addItem(tr("(first available)"), QString());
    {
        midi::MidiEngine ports;
        for (const auto &name : ports.outputPortNames()) m_midiPort->addItem(name, name);
    }
    if (m_loaded.midiPort.isEmpty()) m_midiPort->setCurrentIndex(0);
    else if (const int i = m_midiPort->findData(m_loaded.midiPort); i >= 0) m_midiPort->setCurrentIndex(i);
    else m_midiPort->setEditText(m_loaded.midiPort);
    mscGrid->addWidget(new QLabel(tr("MIDI output"), m_mscGroup), 0, 0, Qt::AlignRight | Qt::AlignVCenter);
    mscGrid->addWidget(m_midiPort, 0, 1);

    m_deviceId = new QSpinBox(m_mscGroup);
    m_deviceId->setObjectName(QStringLiteral("deskDeviceId"));
    m_deviceId->setRange(0, 127);
    m_deviceId->setValue(m_loaded.mscDeviceId);
    m_deviceId->setMinimumWidth(110);
    mscGrid->addWidget(new QLabel(tr("MSC device ID"), m_mscGroup), 1, 0, Qt::AlignRight | Qt::AlignVCenter);
    mscGrid->addWidget(m_deviceId, 1, 1, Qt::AlignLeft);
    mscLay->addLayout(mscGrid);
    mscLay->addWidget(makeNote(tr("0–127. 127 is all-call: every desk on the cable listens."),
                               m_mscGroup));
    root->addWidget(m_mscGroup);

    // ── On the desk ───────────────────────────────────────────────────
    auto *steps = new QFrame(this);
    steps->setObjectName(QStringLiteral("deskSteps"));
    steps->setStyleSheet(QStringLiteral(
        "QFrame#deskSteps { background:%1; border:1px solid %2; border-radius:4px; }")
        .arg(t.bgPanel.name(), t.divider.name()));
    m_stepsLayout = new QVBoxLayout(steps);
    m_stepsLayout->setContentsMargins(14, 12, 14, 12);
    m_stepsLayout->setSpacing(8);
    m_stepsTitle = new QLabel(steps);
    m_stepsTitle->setStyleSheet(QStringLiteral(
        "color:%1; font-size:11px; font-weight:600; letter-spacing:0.06em;")
        .arg(t.ink60.name()));
    m_stepsLayout->addWidget(m_stepsTitle);
    root->addWidget(steps);

    root->addStretch(1);

    // ── Buttons ───────────────────────────────────────────────────────
    m_buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    m_buttons->button(QDialogButtonBox::Save)->setDefault(true);
    m_buttons->button(QDialogButtonBox::Save)->setObjectName(QStringLiteral("deskSave"));
    connect(m_buttons, &QDialogButtonBox::accepted, this, &LightingDeskDialog::save);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(m_buttons);

    // ── Wiring ────────────────────────────────────────────────────────
    connect(m_typeGroup, &QButtonGroup::idToggled, this, [this](int, bool on) {
        if (on) refreshForType();
    });
    connect(m_host, &QLineEdit::textChanged, this, &LightingDeskDialog::validateHost);
    // The steps quote the port / device ID, so keep them in step.
    connect(m_port, &QSpinBox::valueChanged, this, &LightingDeskDialog::rebuildSteps);
    connect(m_deviceId, &QSpinBox::valueChanged, this, &LightingDeskDialog::rebuildSteps);
    connect(m_feedback, &QCheckBox::toggled, this, &LightingDeskDialog::rebuildSteps);
    connect(m_feedbackPort, &QSpinBox::valueChanged, this, &LightingDeskDialog::rebuildSteps);

    selectType(m_loaded.type);
    refreshForType();
    validateHost();
}

bool LightingDeskDialog::edit(QWidget *parent)
{
    LightingDeskDialog dlg(parent);
    return dlg.exec() == QDialog::Accepted;
}

// A selectable "card": a radio with the desk's name, and a one-line blurb
// underneath. Clicking anywhere on the card picks it.
QFrame *LightingDeskDialog::makeDeskCard(Type type, const QString &name, const QString &blurb)
{
    const auto &t = Theme::tokens();
    auto *card = new QFrame(this);
    card->setObjectName(QStringLiteral("deskCard"));
    card->setProperty("deskType", static_cast<int>(type));
    card->setCursor(Qt::PointingHandCursor);
    card->setStyleSheet(QStringLiteral(
        "QFrame#deskCard { background:%1; border:1px solid %2; border-radius:4px; }"
        "QFrame#deskCard:hover { border-color:%3; }"
        "QFrame#deskCard[checked=\"true\"] { border-color:%4; background:%5; }")
        .arg(t.bgPanel.name(), t.outline.name(), t.ink40.name(),
             t.accent.name(), t.bgRow.name()));

    auto *lay = new QVBoxLayout(card);
    lay->setContentsMargins(12, 8, 12, 8);
    lay->setSpacing(2);

    auto *radio = new QRadioButton(name, card);
    radio->setObjectName(QStringLiteral("deskType_%1").arg(core::LightingDesk::typeKey(type)));
    radio->setStyleSheet(QStringLiteral("background:transparent; font-weight:600;"));
    m_typeGroup->addButton(radio, static_cast<int>(type));
    lay->addWidget(radio);

    auto *desc = new QLabel(blurb, card);
    desc->setWordWrap(true);
    desc->setStyleSheet(QStringLiteral("color:%1;").arg(t.ink60.name()));
    desc->setContentsMargins(24, 0, 0, 0);     // sit under the radio's text
    lay->addWidget(desc);

    card->installEventFilter(this);
    desc->installEventFilter(this);
    m_cards.append(card);
    return card;
}

bool LightingDeskDialog::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonPress) {
        for (auto *card : m_cards) {
            if (watched == card || watched->parent() == card) {
                selectType(static_cast<Type>(card->property("deskType").toInt()));
                return true;
            }
        }
    }
    return QDialog::eventFilter(watched, event);
}

LightingDeskDialog::Type LightingDeskDialog::selectedType() const
{
    const int id = m_typeGroup->checkedId();
    return id < 0 ? Type::Eos : static_cast<Type>(id);
}

void LightingDeskDialog::selectType(Type type)
{
    if (auto *b = m_typeGroup->button(static_cast<int>(type))) b->setChecked(true);
}

void LightingDeskDialog::refreshForType()
{
    const Type type = selectedType();

    for (auto *card : m_cards) {
        const bool on = static_cast<Type>(card->property("deskType").toInt()) == type;
        if (card->property("checked").toBool() != on) {
            card->setProperty("checked", on);
            card->style()->unpolish(card);
            card->style()->polish(card);
        }
    }

    const bool osc = type != Type::Ma2Msc;
    m_oscGroup->setVisible(osc);
    m_mscGroup->setVisible(!osc);
    m_prefixLabel->setVisible(type == Type::Ma3);
    m_prefix->setVisible(type == Type::Ma3);
    m_feedbackRow->setVisible(type == Type::Eos);

    m_ownPortNote->setText(tr(
        "quewi sends to this address and port. It's not quewi's own OSC port "
        "(Preferences → OSC, currently %1) — that one is for remotes like HeliOSC "
        "talking to quewi.").arg(quewiOwnOscPort()));

    rebuildSteps();
    validateHost();
}

void LightingDeskDialog::validateHost()
{
    const QString host = m_host->text().trimmed();
    const bool osc = selectedType() != Type::Ma2Msc;

    QString error;
    if (osc) {
        if (host.isEmpty())
            error = tr("Enter the desk's IP address.");
        else if (!looksLikeHost(host))
            error = tr("That doesn't look like an IP address or hostname — e.g. 192.168.1.100.");
    }
    m_hostValid = error.isEmpty();
    m_hostError->setText(error);
    m_hostError->setVisible(!m_hostValid);
    m_hostNote->setVisible(m_hostValid && osc && isThisComputer(host));
    m_buttons->button(QDialogButtonBox::Save)->setEnabled(m_hostValid);
}

void LightingDeskDialog::rebuildSteps()
{
    const auto &t = Theme::tokens();
    for (auto *row : m_stepRows) {
        while (auto *item = row->takeAt(0)) { delete item->widget(); delete item; }
        m_stepsLayout->removeItem(row);
        delete row;
    }
    m_stepRows.clear();

    QString title;
    QStringList steps;
    switch (selectedType()) {
    case Type::Eos:
        title = tr("On the desk (Eos, Ion, Element or Nomad)");
        steps << tr("Open Setup → System → Show Control (on Nomad: the same place in the Setup tab).")
              << tr("Turn OSC RX on, and set the OSC UDP RX port to %1 — the port above.")
                     .arg(m_port->value())
              << tr("The IP address to enter above is the desk's own. For Nomad running on this "
                    "computer, use 127.0.0.1.");
        if (m_feedback->isChecked())
            steps << tr("To show the desk's running cue in quewi, leave OSC TCP on (it is unless "
                        "someone turned it off) — quewi reads it on port %1.")
                         .arg(m_feedbackPort->value());
        break;
    case Type::Ma3:
        title = tr("On the desk (grandMA3)");
        steps << tr("Open Menu → In & Outputs → OSC and add a new OSC data line.")
              << tr("Set its destination IP to this computer's address and its port to %1 — "
                    "the port above.").arg(m_port->value())
              << tr("Enable Receive and Receive Command on that line. If the line has a prefix, "
                    "enter the same prefix above.");
        break;
    case Type::Ma2Msc:
        title = tr("On the desk (grandMA2 / MSC)");
        steps << tr("Connect a MIDI interface between this computer and the desk's MIDI In.")
              << tr("Open Setup → Show → MIDI Show Control and turn MSC In on.")
              << tr("Set the desk's MSC device ID to %1 — the same as above (127 means all-call).")
                     .arg(m_deviceId->value());
        break;
    }
    m_stepsTitle->setText(title);

    int n = 0;
    for (const auto &text : steps) {
        auto *box = m_stepsTitle->parentWidget();
        auto *h = new QHBoxLayout();
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(10);
        auto *num = new QLabel(QString::number(++n), box);
        num->setStyleSheet(QStringLiteral("color:%1; font-weight:600;").arg(t.accent.name()));
        num->setFixedWidth(16);
        num->setAlignment(Qt::AlignRight | Qt::AlignTop);
        auto *body = new QLabel(text, box);
        body->setWordWrap(true);
        body->setTextInteractionFlags(Qt::TextSelectableByMouse);
        h->addWidget(num, 0, Qt::AlignTop);
        h->addWidget(body, 1);
        m_stepsLayout->addLayout(h);
        m_stepRows.append(h);
    }
}

core::LightingDesk LightingDeskDialog::currentDesk() const
{
    core::LightingDesk d = m_loaded;          // keeps eosFaderBank as loaded
    d.type      = selectedType();
    d.host      = m_host->text().trimmed();
    d.port      = m_port->value();
    d.ma3Prefix = m_prefix->text().trimmed().remove(QLatin1Char('/'));
    const QString midiText = m_midiPort->currentText().trimmed();
    d.midiPort  = (m_midiPort->currentIndex() == 0 && midiText == m_midiPort->itemText(0))
                      ? QString() : midiText;
    d.mscDeviceId = m_deviceId->value();
    d.feedback  = m_feedback->isChecked();
    d.feedbackPort = m_feedbackPort->value();
    return d;
}

void LightingDeskDialog::save()
{
    validateHost();
    if (!m_hostValid) { m_host->setFocus(); return; }
    core::LightingDesk d = core::LightingDesk::load();   // keep anything newer on disk
    const auto cur = currentDesk();
    d.type = cur.type; d.host = cur.host; d.port = cur.port; d.ma3Prefix = cur.ma3Prefix;
    d.midiPort = cur.midiPort; d.mscDeviceId = cur.mscDeviceId;
    d.feedback = cur.feedback; d.feedbackPort = cur.feedbackPort;
    d.save();
    accept();
}

} // namespace quewi::ui
