#include "ui/LightTriggersPanel.h"

#include "core/CueList.h"
#include "core/UndoCommands.h"
#include "core/Workspace.h"
#include "cues/Cue.h"
#include "ui/Theme.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTimer>
#include <QUndoStack>
#include <QVBoxLayout>

#include <algorithm>
#include <iterator>

namespace quewi::ui {

using audio::LightTrigger;
using audio::LightTriggers;
using audio::TriggerAction;

namespace {

constexpr double kDefaultRangeSeconds = 4.0;
constexpr int    kFlashMs = 450;

// Same edit as the Inspector's field edits, but structural changes (add,
// delete, move, toggle) don't merge into the previous step — undoing a
// delete shouldn't also undo the rename before it.
class TriggersEditCommand : public core::EditCueFieldCommand {
public:
    TriggersEditCommand(cues::Cue *cue, QVariant oldValue, QVariant newValue, bool mergeable)
        : core::EditCueFieldCommand(cue, QStringLiteral("lightTriggers"),
                                    std::move(oldValue), std::move(newValue))
        , m_mergeable(mergeable)
    {
        setText(QObject::tr("Edit lighting triggers"));
    }
    int id() const override { return m_mergeable ? core::EditCueFieldCommand::id() : -1; }

private:
    bool m_mergeable;
};

QString secondsText(double s) { return QString::number(s, 'f', 3); }

QLabel *capsLabel(const QString &text, QWidget *parent)
{
    auto *l = new QLabel(text, parent);
    l->setStyleSheet(QStringLiteral(
        "color:%1; font-size:10px; font-weight:700; letter-spacing:0.15em;")
        .arg(Theme::tokens().ink40.name()));
    return l;
}

QLabel *helpLabel(const QString &text, QWidget *parent)
{
    auto *l = new QLabel(text, parent);
    l->setWordWrap(true);
    l->setStyleSheet(QStringLiteral("color:%1; font-size:11px;")
                         .arg(Theme::tokens().ink40.name()));
    return l;
}

QSpinBox *spin(int lo, int hi, QWidget *parent)
{
    auto *s = new QSpinBox(parent);
    s->setRange(lo, hi);
    s->setKeyboardTracking(false);   // commit when typing is done, not per digit
    return s;
}

struct MscCommand { int code; const char *name; };
constexpr MscCommand kMscCommands[] = {
    {0x01, "GO"}, {0x02, "STOP"}, {0x03, "RESUME"}, {0x04, "TIMED GO"},
    {0x05, "LOAD"}, {0x06, "SET"}, {0x07, "FIRE"}, {0x08, "ALL OFF"},
    {0x09, "RESTORE"}, {0x0A, "RESET"}, {0x0B, "GO OFF"},
};

// Presets ▾. They only fill fields; the host is left alone (it's the desk's
// address, which the user set once) and the numbers are there to be edited.
struct Preset {
    const char *name;
    void (*apply)(TriggerAction &);
};

void oscPreset(TriggerAction &a, const char *address, const char *args = "")
{
    a.kind = TriggerAction::Kind::Osc;
    a.port = 8000;
    a.transport = 0;
    a.address = QString::fromLatin1(address);
    a.args = QString::fromLatin1(args);
}

const Preset kPresets[] = {
    {QT_TRANSLATE_NOOP("TriggerActionEditor", "ETC Eos — Fire cue (list/cue)"),
     [](TriggerAction &a) { oscPreset(a, "/eos/cue/1/1/fire"); }},
    {QT_TRANSLATE_NOOP("TriggerActionEditor", "ETC Eos — GO"),
     [](TriggerAction &a) { oscPreset(a, "/eos/key/go_0"); }},
    {QT_TRANSLATE_NOOP("TriggerActionEditor", "ETC Eos — Stop/Back"),
     [](TriggerAction &a) { oscPreset(a, "/eos/key/stop_back"); }},
    {QT_TRANSLATE_NOOP("TriggerActionEditor", "ETC Eos — Fire macro"),
     [](TriggerAction &a) { oscPreset(a, "/eos/macro/1/fire"); }},
    {QT_TRANSLATE_NOOP("TriggerActionEditor", "ETC Eos — Sub level"),
     [](TriggerAction &a) { oscPreset(a, "/eos/sub/1", "1.0"); }},
    {QT_TRANSLATE_NOOP("TriggerActionEditor", "grandMA3 — OSC command"),
     [](TriggerAction &a) { oscPreset(a, "/gma3/cmd", "\"Go+ Sequence 1\""); }},
    {QT_TRANSLATE_NOOP("TriggerActionEditor", "grandMA — MSC GO cue"),
     [](TriggerAction &a) {
         a.kind = TriggerAction::Kind::Msc;
         a.command = 0x01;
         a.commandFormat = 0x01;
         a.qNumber = QStringLiteral("1");
         a.qList = QStringLiteral("1");
     }},
    {QT_TRANSLATE_NOOP("TriggerActionEditor", "grandMA — MIDI note"),
     [](TriggerAction &a) {
         a.kind = TriggerAction::Kind::Midi;
         a.midiType = TriggerAction::MidiType::NoteOn;
         a.channel = 1;
         a.data1 = 60;
         a.data2 = 127;
     }},
};

} // namespace

// ══ TriggerActionEditor ══════════════════════════════════════════════════════

TriggerActionEditor::TriggerActionEditor(QWidget *parent) : QFrame(parent)
{
    setObjectName(QStringLiteral("ltCard"));
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(12, 10, 12, 12);
    outer->setSpacing(8);

    // Header: title · kind · Presets ▾ · Test
    auto *head = new QHBoxLayout();
    head->setSpacing(8);
    m_title = capsLabel(tr("SENDS"), this);
    m_kind = new QComboBox(this);
    m_kind->setObjectName(QStringLiteral("ltKind"));
    m_kind->addItem(tr("Nothing"),  int(TriggerAction::Kind::None));
    m_kind->addItem(tr("OSC"),      int(TriggerAction::Kind::Osc));
    m_kind->addItem(tr("MIDI"),     int(TriggerAction::Kind::Midi));
    m_kind->addItem(tr("MSC"),      int(TriggerAction::Kind::Msc));
    m_kind->addItem(tr("Fire cue"), int(TriggerAction::Kind::FireCue));
    m_presets = new QPushButton(tr("Presets  ▾"), this);
    m_presets->setObjectName(QStringLiteral("ltButton"));
    m_presets->setCursor(Qt::PointingHandCursor);
    m_presets->setToolTip(tr("Fill in a common lighting-desk message, then adjust the numbers"));
    m_test = new QPushButton(tr("Test"), this);
    m_test->setObjectName(QStringLiteral("ltButton"));
    m_test->setCursor(Qt::PointingHandCursor);
    m_test->setToolTip(tr("Send this now"));
    head->addWidget(m_title);
    head->addWidget(m_kind, 1);
    head->addWidget(m_presets);
    head->addWidget(m_test);
    outer->addLayout(head);

    m_pages = new QStackedWidget(this);
    outer->addWidget(m_pages);

    auto makePage = [this](QFormLayout *&form) {
        auto *w = new QWidget(m_pages);
        form = new QFormLayout(w);
        form->setContentsMargins(0, 0, 0, 0);
        form->setHorizontalSpacing(10);
        form->setVerticalSpacing(6);
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        m_pages->addWidget(w);
        return w;
    };

    // ── Nothing ────────────────────────────────────────────────────────
    {
        QFormLayout *f = nullptr;
        auto *w = makePage(f);
        f->addRow(helpLabel(tr("This edge sends nothing."), w));
    }
    // ── OSC ────────────────────────────────────────────────────────────
    {
        QFormLayout *f = nullptr;
        auto *w = makePage(f);
        m_oscHost = new QLineEdit(w);
        m_oscHost->setPlaceholderText(QStringLiteral("127.0.0.1"));
        m_oscPort = spin(1, 65535, w);
        m_oscTransport = new QComboBox(w);
        m_oscTransport->addItems({tr("UDP"), tr("TCP"), tr("WebSocket")});
        m_oscAddress = new QLineEdit(w);
        m_oscAddress->setPlaceholderText(QStringLiteral("/eos/cue/1/1/fire"));
        m_oscArgs = new QLineEdit(w);
        m_oscHost->setObjectName(QStringLiteral("ltOscHost"));
        m_oscPort->setObjectName(QStringLiteral("ltOscPort"));
        m_oscAddress->setObjectName(QStringLiteral("ltOscAddress"));
        m_oscArgs->setObjectName(QStringLiteral("ltOscArgs"));
        f->addRow(tr("Host"), m_oscHost);
        f->addRow(tr("Port"), m_oscPort);
        f->addRow(tr("Transport"), m_oscTransport);
        f->addRow(tr("Address"), m_oscAddress);
        f->addRow(tr("Args"), m_oscArgs);
        f->addRow(QString(), helpLabel(tr("Comma-separated, typed automatically: 42 is an int, "
                                          "1.5 a float, \"text\" a string, true/false a bool."), w));

        connect(m_oscHost, &QLineEdit::editingFinished, this, [this] {
            change([this](TriggerAction &a) { a.host = m_oscHost->text().trimmed(); });
        });
        connect(m_oscPort, &QSpinBox::valueChanged, this, [this](int v) {
            change([v](TriggerAction &a) { a.port = v; });
        });
        connect(m_oscTransport, &QComboBox::currentIndexChanged, this, [this](int i) {
            change([i](TriggerAction &a) { a.transport = i; });
        });
        connect(m_oscAddress, &QLineEdit::editingFinished, this, [this] {
            change([this](TriggerAction &a) { a.address = m_oscAddress->text().trimmed(); });
        });
        connect(m_oscArgs, &QLineEdit::editingFinished, this, [this] {
            change([this](TriggerAction &a) { a.args = m_oscArgs->text(); });
        });
    }
    // ── MIDI ───────────────────────────────────────────────────────────
    auto connectPort = [this](QComboBox *combo) {
        auto commitPort = [this, combo] {
            const QString p = combo->currentText().trimmed();
            change([p](TriggerAction &a) { a.midiPort = p; });
        };
        connect(combo, &QComboBox::activated, this, commitPort);
        connect(combo->lineEdit(), &QLineEdit::editingFinished, this, commitPort);
    };
    auto makePortCombo = [](QWidget *parent) {
        auto *c = new QComboBox(parent);
        c->setEditable(true);
        c->setInsertPolicy(QComboBox::NoInsert);
        c->lineEdit()->setPlaceholderText(tr("First available"));
        return c;
    };
    {
        auto *w = makePage(m_midiForm);
        m_midiPort = makePortCombo(w);
        m_midiType = new QComboBox(w);
        m_midiType->addItem(tr("Note on"),        int(TriggerAction::MidiType::NoteOn));
        m_midiType->addItem(tr("Note off"),       int(TriggerAction::MidiType::NoteOff));
        m_midiType->addItem(tr("Control change"), int(TriggerAction::MidiType::ControlChange));
        m_midiType->addItem(tr("Program change"), int(TriggerAction::MidiType::ProgramChange));
        m_midiType->addItem(tr("Raw bytes"),      int(TriggerAction::MidiType::Raw));
        m_midiChannel = spin(1, 16, w);
        m_midiData1 = spin(0, 127, w);
        m_midiData2 = spin(0, 127, w);
        m_midiRaw = new QLineEdit(w);
        m_midiRaw->setPlaceholderText(QStringLiteral("90 3C 7F"));
        m_midiForm->addRow(tr("Port"), m_midiPort);
        m_midiForm->addRow(tr("Type"), m_midiType);
        m_midiForm->addRow(tr("Channel"), m_midiChannel);
        m_midiForm->addRow(tr("Note"), m_midiData1);
        m_midiForm->addRow(tr("Velocity"), m_midiData2);
        m_midiForm->addRow(tr("Raw hex"), m_midiRaw);

        connectPort(m_midiPort);
        connect(m_midiType, &QComboBox::currentIndexChanged, this, [this](int) {
            const auto t = TriggerAction::MidiType(m_midiType->currentData().toInt());
            change([t](TriggerAction &a) { a.midiType = t; });
        });
        connect(m_midiChannel, &QSpinBox::valueChanged, this, [this](int v) {
            change([v](TriggerAction &a) { a.channel = v; });
        });
        connect(m_midiData1, &QSpinBox::valueChanged, this, [this](int v) {
            change([v](TriggerAction &a) { a.data1 = v; });
        });
        connect(m_midiData2, &QSpinBox::valueChanged, this, [this](int v) {
            change([v](TriggerAction &a) { a.data2 = v; });
        });
        connect(m_midiRaw, &QLineEdit::editingFinished, this, [this] {
            change([this](TriggerAction &a) { a.rawHex = m_midiRaw->text().trimmed(); });
        });
    }
    // ── MSC ────────────────────────────────────────────────────────────
    {
        QFormLayout *f = nullptr;
        auto *w = makePage(f);
        m_mscPort = makePortCombo(w);
        m_mscDevice = spin(0, 127, w);
        m_mscDevice->setToolTip(tr("The desk's MSC device ID. 127 = all-call (every device)."));
        m_mscCommand = new QComboBox(w);
        for (const auto &c : kMscCommands)
            m_mscCommand->addItem(QString::fromLatin1(c.name), c.code);
        m_mscFormat = new QComboBox(w);
        m_mscFormat->addItem(tr("Lighting"),      0x01);
        m_mscFormat->addItem(tr("Moving lights"), 0x02);
        m_mscFormat->addItem(tr("All types"),     0x7F);
        m_mscQNumber = new QLineEdit(w);
        m_mscQNumber->setPlaceholderText(tr("e.g. 12.5"));
        m_mscQList = new QLineEdit(w);
        m_mscQList->setPlaceholderText(tr("optional"));
        f->addRow(tr("Port"), m_mscPort);
        f->addRow(tr("Device ID"), m_mscDevice);
        f->addRow(tr("Command"), m_mscCommand);
        f->addRow(tr("Format"), m_mscFormat);
        f->addRow(tr("Cue number"), m_mscQNumber);
        f->addRow(tr("Cue list"), m_mscQList);

        connectPort(m_mscPort);
        connect(m_mscDevice, &QSpinBox::valueChanged, this, [this](int v) {
            change([v](TriggerAction &a) { a.deviceId = v; });
        });
        connect(m_mscCommand, &QComboBox::currentIndexChanged, this, [this](int) {
            const int c = m_mscCommand->currentData().toInt();
            change([c](TriggerAction &a) { a.command = c; });
        });
        connect(m_mscFormat, &QComboBox::currentIndexChanged, this, [this](int) {
            const int c = m_mscFormat->currentData().toInt();
            change([c](TriggerAction &a) { a.commandFormat = c; });
        });
        connect(m_mscQNumber, &QLineEdit::editingFinished, this, [this] {
            change([this](TriggerAction &a) { a.qNumber = m_mscQNumber->text().trimmed(); });
        });
        connect(m_mscQList, &QLineEdit::editingFinished, this, [this] {
            change([this](TriggerAction &a) { a.qList = m_mscQList->text().trimmed(); });
        });
    }
    // ── Fire cue ───────────────────────────────────────────────────────
    {
        QFormLayout *f = nullptr;
        auto *w = makePage(f);
        m_cueCombo = new QComboBox(w);
        m_cueCombo->setMinimumContentsLength(18);
        m_cueCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        f->addRow(tr("Cue"), m_cueCombo);
        f->addRow(QString(), helpLabel(tr("Fires that cue as if you pressed GO on it."), w));
        connect(m_cueCombo, &QComboBox::currentIndexChanged, this, [this](int) {
            const QUuid id = m_cueCombo->currentData().toUuid();
            change([id](TriggerAction &a) { a.cueId = id; });
        });
    }

    connect(m_kind, &QComboBox::currentIndexChanged, this, [this](int) {
        const auto k = TriggerAction::Kind(m_kind->currentData().toInt());
        change([k](TriggerAction &a) { a.kind = k; });
    });
    connect(m_test, &QPushButton::clicked, this, [this] { emit testRequested(m_action); });
    connect(m_presets, &QPushButton::clicked, this, [this] {
        QMenu menu(this);
        const QStringList names = presetNames();
        for (int i = 0; i < names.size(); ++i)
            menu.addAction(names.at(i), this, [this, i] { applyPreset(i); });
        menu.exec(m_presets->mapToGlobal(QPoint(0, m_presets->height())));
    });

    load();
}

void TriggerActionEditor::setTitle(const QString &title) { m_title->setText(title); }

void TriggerActionEditor::setAction(const TriggerAction &a)
{
    // The panel hands back what we just emitted after every commit; the
    // widgets already show it.
    if (a == m_action) return;
    m_action = a;
    load();
}

void TriggerActionEditor::setWorkspace(core::Workspace *ws)
{
    m_workspace = ws;
    load();
}

void TriggerActionEditor::setMidiPortsProvider(std::function<QStringList()> f)
{
    m_midiPorts = std::move(f);
    load();
}

QStringList TriggerActionEditor::presetNames()
{
    QStringList out;
    for (const auto &p : kPresets) out << tr(p.name);
    return out;
}

void TriggerActionEditor::applyPreset(int index)
{
    if (index < 0 || index >= int(std::size(kPresets))) return;
    TriggerAction a = m_action;
    kPresets[index].apply(a);
    m_action = a;
    load();
    emit edited(m_action);
}

void TriggerActionEditor::change(const std::function<void(TriggerAction &)> &f)
{
    if (m_loading) return;
    TriggerAction a = m_action;
    f(a);
    if (a == m_action) return;
    const bool newKind = a.kind != m_action.kind;
    m_action = a;
    // Only a new kind needs the widgets reloaded (to show and fill its page).
    // Reloading otherwise would clear the very combo whose signal got us here.
    if (newKind) load();
    emit edited(m_action);
}

void TriggerActionEditor::fillPorts(QComboBox *combo)
{
    combo->clear();
    if (m_midiPorts) combo->addItems(m_midiPorts());
    combo->setEditText(m_action.midiPort);
}

void TriggerActionEditor::fillCues()
{
    m_cueCombo->clear();
    m_cueCombo->addItem(tr("(choose a cue)"), QUuid());
    int sel = 0;
    if (m_workspace) {
        for (const auto &list : m_workspace->cueLists()) {
            for (int i = 0; i < list->cueCount(); ++i) {
                const auto *c = list->cueAt(i);
                if (!c) continue;
                m_cueCombo->addItem(QStringLiteral("%1 %2  (%3)")
                                        .arg(QString::number(c->number(), 'f', 2),
                                             c->name().isEmpty() ? c->typeName() : c->name(),
                                             list->name()),
                                    c->id());
                if (c->id() == m_action.cueId) sel = m_cueCombo->count() - 1;
            }
        }
    }
    if (sel == 0 && !m_action.cueId.isNull()) {
        m_cueCombo->addItem(tr("(missing cue)"), m_action.cueId);
        sel = m_cueCombo->count() - 1;
    }
    m_cueCombo->setCurrentIndex(sel);
}

void TriggerActionEditor::updateMidiRows()
{
    using T = TriggerAction::MidiType;
    const T t = m_action.midiType;
    const bool raw = t == T::Raw;
    m_midiForm->setRowVisible(m_midiChannel, !raw);
    m_midiForm->setRowVisible(m_midiData1, !raw);
    m_midiForm->setRowVisible(m_midiData2, !raw && t != T::ProgramChange && t != T::NoteOff);
    m_midiForm->setRowVisible(m_midiRaw, raw);
    if (auto *l = qobject_cast<QLabel *>(m_midiForm->labelForField(m_midiData1)))
        l->setText(t == T::ControlChange ? tr("Controller")
                 : t == T::ProgramChange ? tr("Program") : tr("Note"));
    if (auto *l = qobject_cast<QLabel *>(m_midiForm->labelForField(m_midiData2)))
        l->setText(t == T::ControlChange ? tr("Value") : tr("Velocity"));
}

void TriggerActionEditor::load()
{
    m_loading = true;
    const auto &a = m_action;
    using K = TriggerAction::Kind;
    m_kind->setCurrentIndex(std::max(0, m_kind->findData(int(a.kind))));
    m_pages->setCurrentIndex(int(a.kind));
    // Size the stack to the page showing, not the tallest one, so "Nothing"
    // doesn't leave a hole the height of the MIDI form.
    for (int i = 0; i < m_pages->count(); ++i) {
        const auto pol = i == int(a.kind) ? QSizePolicy::Preferred : QSizePolicy::Ignored;
        m_pages->widget(i)->setSizePolicy(pol, pol);
    }
    m_pages->adjustSize();
    m_test->setEnabled(a.kind != K::None);

    switch (a.kind) {
    case K::None:
        break;
    case K::Osc:
        m_oscHost->setText(a.host);
        m_oscPort->setValue(a.port);
        m_oscTransport->setCurrentIndex(std::clamp(a.transport, 0, 2));
        m_oscAddress->setText(a.address);
        m_oscArgs->setText(a.args);
        break;
    case K::Midi:
        if (!m_midiPort->view()->isVisible()) fillPorts(m_midiPort);
        m_midiType->setCurrentIndex(std::max(0, m_midiType->findData(int(a.midiType))));
        m_midiChannel->setValue(a.channel);
        m_midiData1->setValue(a.data1);
        m_midiData2->setValue(a.data2);
        m_midiRaw->setText(a.rawHex);
        updateMidiRows();
        break;
    case K::Msc: {
        if (!m_mscPort->view()->isVisible()) fillPorts(m_mscPort);
        m_mscDevice->setValue(a.deviceId);
        m_mscCommand->setCurrentIndex(std::max(0, m_mscCommand->findData(a.command)));
        int fi = m_mscFormat->findData(a.commandFormat);
        if (fi < 0) {   // an unusual format from a show file / remote: keep it
            m_mscFormat->addItem(QStringLiteral("0x%1").arg(a.commandFormat, 2, 16, QLatin1Char('0')),
                                 a.commandFormat);
            fi = m_mscFormat->count() - 1;
        }
        m_mscFormat->setCurrentIndex(fi);
        m_mscQNumber->setText(a.qNumber);
        m_mscQList->setText(a.qList);
        break;
    }
    case K::FireCue:
        fillCues();
        break;
    }
    m_loading = false;
}

// ══ LightTriggersPanel ═══════════════════════════════════════════════════════

LightTriggersPanel::LightTriggersPanel(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("lightTriggersPanel"));
    const auto &tk = Theme::tokens();
    setStyleSheet(QStringLiteral(
        "QFrame#ltCard { background:%1; border:none; border-radius:4px; }"
        "QPushButton#ltButton { background:%2; color:%3; border:none; border-radius:3px;"
        "  padding:5px 12px; font-size:12px; font-weight:600; }"
        "QPushButton#ltButton:hover   { background:%4; }"
        "QPushButton#ltButton:pressed { background:%5; }"
        "QPushButton#ltButton:disabled { color:%6; }"
        "QTableWidget#ltTable { background:%1; border:none; border-radius:4px; }")
        .arg(tk.bgPanel.name(),                    // %1 card
             tk.bgInteractive.name(),              // %2 button
             tk.ink100.name(),                     // %3 text
             tk.bgRowHover.name(),                 // %4 hover
             tk.bgInteractive.darker(115).name(),  // %5 pressed
             tk.ink40.name()));                    // %6 disabled text

    auto *outer = new QHBoxLayout(this);
    outer->setContentsMargins(16, 14, 16, 16);
    outer->setSpacing(14);

    // ── Left: list + buttons ───────────────────────────────────────────
    auto *left = new QVBoxLayout();
    left->setSpacing(8);
    auto *btnRow = new QHBoxLayout();
    btnRow->setSpacing(6);
    auto *addPoint = new QPushButton(tr("+ Point at cursor"), this);
    auto *addRange = new QPushButton(tr("+ Range"), this);
    m_duplicateBtn = new QPushButton(tr("Duplicate"), this);
    m_deleteBtn    = new QPushButton(tr("Delete"), this);
    addRange->setToolTip(tr("A range from the cursor, %1 s long: sends one thing as the song "
                            "enters it and another as it leaves").arg(kDefaultRangeSeconds));
    addPoint->setToolTip(tr("A single hit at the edit cursor"));
    for (auto *b : {addPoint, addRange, m_duplicateBtn, m_deleteBtn}) {
        b->setObjectName(QStringLiteral("ltButton"));
        b->setCursor(Qt::PointingHandCursor);
        btnRow->addWidget(b);
    }
    btnRow->addStretch(1);
    m_cursorLabel = new QLabel(this);
    m_cursorLabel->setStyleSheet(QStringLiteral(
        "color:%1; font-family:'Space Grotesk','JetBrains Mono',monospace; font-size:11px;")
        .arg(tk.ink40.name()));
    btnRow->addWidget(m_cursorLabel);
    left->addLayout(btnRow);

    m_table = new QTableWidget(0, 5, this);
    m_table->setObjectName(QStringLiteral("ltTable"));
    m_table->setHorizontalHeaderLabels({tr("On"), tr("Name"), tr("Start"), tr("End"), tr("Sends")});
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setShowGrid(false);
    m_table->setWordWrap(false);
    m_table->verticalHeader()->setDefaultSectionSize(24);
    auto *hh = m_table->horizontalHeader();
    hh->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    hh->setSectionResizeMode(1, QHeaderView::Interactive);
    hh->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    hh->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    hh->setSectionResizeMode(4, QHeaderView::Stretch);
    m_table->setColumnWidth(1, 140);
    left->addWidget(m_table, 1);
    outer->addLayout(left, 3);

    // ── Right: editor for the selected trigger ─────────────────────────
    m_editorStack = new QStackedWidget(this);
    auto *placeholder = new QLabel(tr("Select a trigger, or add one at the cursor.\n\n"
                                      "In the timeline's lighting lane: click to add a point, "
                                      "drag to add a range."), m_editorStack);
    placeholder->setAlignment(Qt::AlignCenter);
    placeholder->setWordWrap(true);
    placeholder->setStyleSheet(QStringLiteral("color:%1;").arg(tk.ink40.name()));
    m_editorStack->addWidget(placeholder);

    auto *scroll = new QScrollArea(m_editorStack);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setStyleSheet(QStringLiteral("QScrollArea{background:transparent;}"
                                         "QScrollArea > QWidget > QWidget{background:transparent;}"));
    auto *form = new QWidget(scroll);
    auto *fv = new QVBoxLayout(form);
    fv->setContentsMargins(0, 0, 6, 0);
    fv->setSpacing(10);

    auto *card = new QFrame(form);
    card->setObjectName(QStringLiteral("ltCard"));
    auto *cf = new QFormLayout(card);
    cf->setContentsMargins(12, 10, 12, 12);
    cf->setHorizontalSpacing(10);
    cf->setVerticalSpacing(6);
    cf->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    m_name = new QLineEdit(card);
    m_name->setObjectName(QStringLiteral("ltName"));
    m_name->setPlaceholderText(tr("e.g. Chorus wash"));
    auto makeTime = [card] {
        auto *s = new QDoubleSpinBox(card);
        s->setDecimals(3);
        s->setRange(0.0, 86400.0);
        s->setSingleStep(0.1);
        s->setSuffix(QStringLiteral(" s"));
        s->setKeyboardTracking(false);
        return s;
    };
    m_start = makeTime();
    m_end   = makeTime();
    m_start->setObjectName(QStringLiteral("ltStart"));
    m_end->setObjectName(QStringLiteral("ltEnd"));
    m_isRange = new QCheckBox(tr("Range"), card);
    m_isRange->setToolTip(tr("A range sends one thing as the song enters it and another "
                             "as it leaves (or stops inside it)"));
    auto *endRow = new QHBoxLayout();
    endRow->setContentsMargins(0, 0, 0, 0);
    endRow->addWidget(m_isRange);
    endRow->addWidget(m_end, 1);
    m_enabled = new QCheckBox(tr("Enabled"), card);
    cf->addRow(tr("Name"), m_name);
    cf->addRow(tr("Start"), m_start);
    cf->addRow(tr("End"), endRow);
    cf->addRow(QString(), m_enabled);
    fv->addWidget(card);

    m_enter = new TriggerActionEditor(form);
    m_exit  = new TriggerActionEditor(form);
    m_exit->setTitle(tr("ON EXIT"));
    fv->addWidget(m_enter);
    fv->addWidget(m_exit);
    fv->addStretch(1);
    scroll->setWidget(form);
    m_editorStack->addWidget(scroll);
    // The editor scrolls; its full height mustn't size the editor's bottom
    // area (it would squeeze the timeline). The list sets the panel's height.
    m_editorStack->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Ignored);
    outer->addWidget(m_editorStack, 2);

    // ── Wiring ─────────────────────────────────────────────────────────
    connect(addPoint, &QPushButton::clicked, this, [this] { addTrigger(m_cursor); });
    connect(addRange, &QPushButton::clicked, this, [this] {
        addTrigger(m_cursor, m_cursor + kDefaultRangeSeconds);
    });
    connect(m_duplicateBtn, &QPushButton::clicked, this, [this] {
        const int i = indexOf(m_selectedId);
        if (!m_cue || i < 0) return;
        LightTriggers next = m_cue->lightTriggers();
        LightTrigger copy = next[size_t(i)];
        copy.id = QUuid::createUuid();
        copy.name = copy.name.isEmpty() ? QString() : tr("%1 copy").arg(copy.name);
        copy.start += 1.0;
        if (copy.end >= 0.0) copy.end += 1.0;
        next.push_back(copy);
        m_selectedId = copy.id;
        commit(next, false);
        emit selectionChanged(m_selectedId);
    });
    connect(m_deleteBtn, &QPushButton::clicked, this, [this] {
        applyTriggerAction(m_selectedId, QStringLiteral("delete"));
    });

    connect(m_table, &QTableWidget::itemSelectionChanged, this, [this] {
        if (m_loading) return;
        const int row = m_table->currentRow();
        const auto sel = m_table->selectionModel()->selectedRows();
        if (sel.isEmpty() || row < 0 || row >= m_rowIds.size()) return;
        setSelected(m_rowIds.at(row));
    });
    connect(m_table, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *item) {
        if (m_loading || item->column() != 0 || item->row() >= m_rowIds.size()) return;
        const QUuid id = m_rowIds.at(item->row());
        const bool on = item->checkState() == Qt::Checked;
        const int i = indexOf(id);
        if (!m_cue || i < 0) return;
        LightTriggers next = m_cue->lightTriggers();
        next[size_t(i)].enabled = on;
        commit(next, false);
    });

    connect(m_name, &QLineEdit::editingFinished, this, [this] {
        const QString n = m_name->text().trimmed();
        editSelected([n](LightTrigger &t) { t.name = n; });
    });
    connect(m_start, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        editSelected([v](LightTrigger &t) {
            const double len = t.isRange() ? t.end - t.start : 0.0;
            t.start = v;
            if (len > 0.0) t.end = v + len;      // a range keeps its length
        });
    });
    connect(m_end, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        editSelected([v](LightTrigger &t) {
            if (t.isRange()) t.end = std::max(v, t.start + 0.001);
        });
    });
    connect(m_isRange, &QCheckBox::toggled, this, [this](bool on) {
        if (m_loading) return;
        editSelected([on](LightTrigger &t) {
            t.end = on ? t.start + kDefaultRangeSeconds : -1.0;
        }, false);
    });
    connect(m_enabled, &QCheckBox::toggled, this, [this](bool on) {
        editSelected([on](LightTrigger &t) { t.enabled = on; }, false);
    });
    connect(m_enter, &TriggerActionEditor::edited, this, [this](const TriggerAction &a) {
        editSelected([a](LightTrigger &t) { t.enter = a; });
    });
    connect(m_exit, &TriggerActionEditor::edited, this, [this](const TriggerAction &a) {
        editSelected([a](LightTrigger &t) { t.exit = a; });
    });
    connect(m_enter, &TriggerActionEditor::testRequested, this, &LightTriggersPanel::testRequested);
    connect(m_exit,  &TriggerActionEditor::testRequested, this, &LightTriggersPanel::testRequested);

    setCursorSeconds(0.0);
    refresh();
}

void LightTriggersPanel::setCue(audio::AudioCue *cue)
{
    if (m_cue == cue) return;
    if (m_cue) disconnect(m_cue, nullptr, this, nullptr);
    m_cue = cue;
    m_selectedId = QUuid();
    if (m_cue) {
        connect(m_cue, &cues::Cue::changed, this, &LightTriggersPanel::refresh);
        connect(m_cue, &QObject::destroyed, this, [this] { QTimer::singleShot(0, this, [this] { refresh(); }); });
    }
    refresh();
}

void LightTriggersPanel::setUndoStack(QUndoStack *undo) { m_undo = undo; }

void LightTriggersPanel::setWorkspace(core::Workspace *ws)
{
    m_workspace = ws;
    m_enter->setWorkspace(ws);
    m_exit->setWorkspace(ws);
    refresh();
}

void LightTriggersPanel::setMidiPortsProvider(std::function<QStringList()> f)
{
    m_enter->setMidiPortsProvider(f);
    m_exit->setMidiPortsProvider(std::move(f));
}

void LightTriggersPanel::setCursorSeconds(double s)
{
    m_cursor = std::max(0.0, s);
    m_cursorLabel->setText(tr("cursor %1 s").arg(secondsText(m_cursor)));
}

void LightTriggersPanel::selectTrigger(const QUuid &id)
{
    if (id == m_selectedId) return;
    setSelected(id);
    refresh();
}

void LightTriggersPanel::setSelected(const QUuid &id)
{
    if (id == m_selectedId) return;
    m_selectedId = id;
    loadEditor();
    emit selectionChanged(id);
}

int LightTriggersPanel::indexOf(const QUuid &id) const
{
    if (!m_cue || id.isNull()) return -1;
    const auto &t = m_cue->lightTriggers();
    for (size_t i = 0; i < t.size(); ++i)
        if (t[i].id == id) return int(i);
    return -1;
}

QString LightTriggersPanel::nextName() const
{
    const int n = m_cue ? int(m_cue->lightTriggers().size()) : 0;
    return tr("Trigger %1").arg(n + 1);
}

// ── Commit: the one write path ───────────────────────────────────────────────

void LightTriggersPanel::commit(const LightTriggers &next, bool mergeable)
{
    if (!m_cue) return;
    const QJsonArray oldJson = audio::triggersToJson(m_cue->lightTriggers());
    const QJsonArray newJson = audio::triggersToJson(next);
    if (oldJson == newJson) return;
    if (m_undo)
        m_undo->push(new TriggersEditCommand(m_cue, QVariant(oldJson), QVariant(newJson), mergeable));
    else
        m_cue->setField(QStringLiteral("lightTriggers"), QVariant(newJson));
    // The cue's changed() already refreshed us; this covers a no-signal path.
    refresh();
    emit triggersEdited();
}

void LightTriggersPanel::editSelected(const std::function<void(LightTrigger &)> &f, bool mergeable)
{
    if (m_loading) return;
    const int i = indexOf(m_selectedId);
    if (i < 0) return;
    LightTriggers next = m_cue->lightTriggers();
    f(next[size_t(i)]);
    commit(next, mergeable);
}

QUuid LightTriggersPanel::addTrigger(double start, double end)
{
    if (!m_cue) return {};
    LightTrigger t;
    t.name  = nextName();
    t.start = std::max(0.0, start);
    t.end   = end > t.start ? end : -1.0;
    LightTriggers next = m_cue->lightTriggers();
    next.push_back(t);
    m_selectedId = t.id;
    commit(next, false);
    emit selectionChanged(t.id);
    return t.id;
}

void LightTriggersPanel::moveTrigger(const QUuid &id, double start, double end)
{
    const int i = indexOf(id);
    if (i < 0) return;
    LightTriggers next = m_cue->lightTriggers();
    auto &t = next[size_t(i)];
    t.start = std::max(0.0, start);
    t.end   = end > t.start ? end : -1.0;
    commit(next, false);
}

void LightTriggersPanel::applyTriggerAction(const QUuid &id, const QString &action)
{
    const int i = indexOf(id);
    if (i < 0) return;
    LightTriggers next = m_cue->lightTriggers();
    auto &t = next[size_t(i)];

    if (action == QLatin1String("rename")) {
        selectTrigger(id);
        bool ok = false;
        const QString n = QInputDialog::getText(this, tr("Rename Trigger"), tr("Trigger name:"),
                                                QLineEdit::Normal, t.name, &ok);
        if (!ok || !m_cue) return;
        const int j = indexOf(id);           // the dialog spun the event loop
        if (j < 0) return;
        next = m_cue->lightTriggers();
        next[size_t(j)].name = n.trimmed();
        commit(next, false);
    } else if (action == QLatin1String("toggleRange")) {
        t.end = t.isRange() ? -1.0 : t.start + kDefaultRangeSeconds;
        commit(next, false);
    } else if (action == QLatin1String("toggleEnabled")) {
        t.enabled = !t.enabled;
        commit(next, false);
    } else if (action == QLatin1String("delete")) {
        next.erase(next.begin() + i);
        if (id == m_selectedId) {
            // Keep a neighbour selected so Delete can be pressed again.
            QUuid neighbour;
            const int row = m_rowIds.indexOf(id);
            if (row >= 0 && row + 1 < m_rowIds.size()) neighbour = m_rowIds.at(row + 1);
            else if (row > 0)                         neighbour = m_rowIds.at(row - 1);
            m_selectedId = neighbour;
            emit selectionChanged(neighbour);
        }
        commit(next, false);
    }
}

// ── Display ──────────────────────────────────────────────────────────────────

QString LightTriggersPanel::actionText(const TriggerAction &a) const
{
    if (a.kind == TriggerAction::Kind::FireCue) {
        if (m_workspace && !a.cueId.isNull()) {
            for (const auto &list : m_workspace->cueLists())
                for (int i = 0; i < list->cueCount(); ++i)
                    if (const auto *c = list->cueAt(i); c && c->id() == a.cueId)
                        return tr("fire cue %1 %2").arg(QString::number(c->number(), 'f', 2),
                                                       c->name()).trimmed();
        }
        return a.cueId.isNull() ? tr("fire cue (none chosen)") : tr("fire cue (missing)");
    }
    return a.summary();
}

void LightTriggersPanel::colourRow(int row)
{
    if (row < 0 || row >= m_rowIds.size()) return;
    const bool flash = m_flashing.contains(m_rowIds.at(row));
    QColor bg = Theme::tokens().accent;
    bg.setAlpha(110);
    for (int c = 0; c < m_table->columnCount(); ++c)
        if (auto *it = m_table->item(row, c))
            it->setBackground(flash ? QBrush(bg) : QBrush());
}

void LightTriggersPanel::refresh()
{
    m_loading = true;
    const auto &tk = Theme::tokens();
    LightTriggers list = m_cue ? m_cue->lightTriggers() : LightTriggers{};
    // The table reads in song order whatever order they were added in.
    std::stable_sort(list.begin(), list.end(),
                     [](const LightTrigger &a, const LightTrigger &b) { return a.start < b.start; });

    const bool lostSelection = !m_selectedId.isNull() && indexOf(m_selectedId) < 0;
    if (lostSelection) m_selectedId = QUuid();   // deleted elsewhere (undo, remote)

    m_table->setRowCount(int(list.size()));
    m_rowIds.clear();
    int selRow = -1;
    for (int r = 0; r < int(list.size()); ++r) {
        const auto &t = list[size_t(r)];
        m_rowIds << t.id;
        if (t.id == m_selectedId) selRow = r;

        auto *on = new QTableWidgetItem();
        on->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
        on->setCheckState(t.enabled ? Qt::Checked : Qt::Unchecked);
        on->setToolTip(tr("Enabled"));
        QString sends = actionText(t.enter);
        if (t.isRange()) sends += QStringLiteral("  →  ") + actionText(t.exit);
        QTableWidgetItem *cells[] = {
            on,
            new QTableWidgetItem(t.name.isEmpty() ? tr("(unnamed)") : t.name),
            new QTableWidgetItem(secondsText(t.start)),
            new QTableWidgetItem(t.isRange() ? secondsText(t.end) : QStringLiteral("—")),
            new QTableWidgetItem(sends),
        };
        cells[2]->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        cells[3]->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        cells[4]->setToolTip(sends);
        for (int c = 0; c < 5; ++c) {
            if (c > 0) {
                cells[c]->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
                if (!t.enabled) cells[c]->setForeground(tk.ink40);
            }
            m_table->setItem(r, c, cells[c]);
        }
        colourRow(r);
    }
    if (selRow >= 0) {
        m_table->selectRow(selRow);
    } else {
        m_table->clearSelection();
        m_table->setCurrentItem(nullptr);
    }
    m_duplicateBtn->setEnabled(selRow >= 0);
    m_deleteBtn->setEnabled(selRow >= 0);
    m_loading = false;
    loadEditor();
    if (lostSelection) emit selectionChanged(QUuid());
}

void LightTriggersPanel::loadEditor()
{
    const int i = indexOf(m_selectedId);
    if (i < 0) {
        m_editorStack->setCurrentIndex(0);
        return;
    }
    const bool wasLoading = m_loading;
    m_loading = true;
    const auto &t = m_cue->lightTriggers()[size_t(i)];
    m_editorStack->setCurrentIndex(1);
    {
        const QSignalBlocker b1(m_start), b2(m_end), b3(m_isRange), b4(m_enabled);
        // Don't overwrite a name being typed unless the selection moved on.
        if (t.id != m_editorId || !m_name->hasFocus()) m_name->setText(t.name);
        m_editorId = t.id;
        m_start->setValue(t.start);
        m_isRange->setChecked(t.isRange());
        m_end->setEnabled(t.isRange());
        m_end->setValue(t.isRange() ? t.end : t.start);
        m_enabled->setChecked(t.enabled);
    }
    m_enter->setTitle(t.isRange() ? tr("ON ENTER") : tr("SENDS"));
    m_enter->setAction(t.enter);
    m_exit->setAction(t.exit);
    m_exit->setVisible(t.isRange());
    m_duplicateBtn->setEnabled(true);
    m_deleteBtn->setEnabled(true);
    m_loading = wasLoading;
}

void LightTriggersPanel::flashTrigger(const QUuid &id)
{
    m_flashing.insert(id);
    colourRow(m_rowIds.indexOf(id));
    QTimer::singleShot(kFlashMs, this, [this, id] {
        m_flashing.remove(id);
        colourRow(m_rowIds.indexOf(id));
    });
}

} // namespace quewi::ui
