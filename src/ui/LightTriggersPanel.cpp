#include "ui/LightTriggersPanel.h"

#include "audio/DeskCommands.h"
#include "core/CueList.h"
#include "core/LightingDesk.h"
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
#include <QHash>
#include <QItemSelection>
#include <QShortcut>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QAbstractItemView>
#include <QMetaObject>
#include <QScrollArea>
#include <QSettings>
#include <QFutureWatcher>
#include <QtConcurrentRun>
#include <QPointer>
#include "ui/FillBeatsDialog.h"
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTimer>
#include <QUndoStack>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
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
        : TriggersEditCommand(cue, QStringLiteral("lightTriggers"), std::move(oldValue),
                              std::move(newValue), mergeable ? kMergeFieldEdits : -1)
    {
        setText(QObject::tr("Edit lighting triggers"));
    }
    // mergeId: kMergeFieldEdits folds into the previous edit of the same field;
    // -1 never merges; anything else merges only with the same id (one tap run).
    TriggersEditCommand(cues::Cue *cue, const QString &field, QVariant oldValue,
                        QVariant newValue, int mergeId)
        : core::EditCueFieldCommand(cue, field, std::move(oldValue), std::move(newValue))
        , m_id(mergeId)
    {
    }
    int id() const override { return m_id; }

    static constexpr int kMergeFieldEdits = 1;   // core::EditCueFieldCommand::id()

private:
    int m_id;
};

constexpr int kTapMergeBase = 0x7A900;           // + tap run (mod 4096)

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

// Presets ▾. They only fill fields, and the numbers are there to be edited.
// Host blank + port 0 = the lighting desk set in Preferences.
struct Preset {
    const char *name;
    void (*apply)(TriggerAction &);
};

void oscPreset(TriggerAction &a, const char *address, const char *args = "")
{
    a.kind = TriggerAction::Kind::Osc;
    a.host.clear();
    a.port = 0;
    a.transport = 0;
    a.address = QString::fromLatin1(address);
    a.args = QString::fromLatin1(args);
}

const Preset kPresets[] = {
    {QT_TRANSLATE_NOOP("TriggerActionEditor", "ETC Eos — Fire cue (list/cue)"),
     [](TriggerAction &a) { oscPreset(a, "/eos/cue/1/1/fire"); }},
    {QT_TRANSLATE_NOOP("TriggerActionEditor", "ETC Eos — GO"),
     [](TriggerAction &a) { oscPreset(a, "/eos/key/go_0"); }},
    {QT_TRANSLATE_NOOP("TriggerActionEditor", "ETC Eos — Stop"),
     [](TriggerAction &a) { oscPreset(a, "/eos/key/stop"); }},
    {QT_TRANSLATE_NOOP("TriggerActionEditor", "ETC Eos — Back"),
     [](TriggerAction &a) { oscPreset(a, "/eos/key/back"); }},
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

// "What should it do?" item data: a DeskDo, or one of these.
constexpr int kDoNothing = -1;
constexpr int kDoFireCue = 1000;

using DeskDo = TriggerAction::DeskDo;
constexpr DeskDo kAllDeskDo[] = {
    DeskDo::Go, DeskDo::GoList, DeskDo::Stop, DeskDo::Back, DeskDo::GoToCue, DeskDo::SubLevel,
    DeskDo::SubBump, DeskDo::FaderLevel, DeskDo::FaderBump, DeskDo::Macro, DeskDo::Command,
};

int doCodeOf(const TriggerAction &a)
{
    switch (a.kind) {
    case TriggerAction::Kind::Desk:    return int(a.deskDo);
    case TriggerAction::Kind::FireCue: return kDoFireCue;
    default:                           return kDoNothing;
    }
}

// One OSC arg as the custom editor's Args field types it back: numbers bare,
// strings quoted (so commas and spaces inside survive).
QString oscArgText(const QVariant &v)
{
    switch (v.typeId()) {
    case QMetaType::Float:
        return QString::number(double(v.toFloat()));
    case QMetaType::Double:
        return QString::number(v.toDouble());
    case QMetaType::Int:
    case QMetaType::LongLong:
        return QString::number(v.toLongLong());
    case QMetaType::Bool:
        return v.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    default: {
        QString s = v.toString();
        s.replace(QLatin1Char('"'), QLatin1Char('\''));
        return QLatin1Char('"') + s + QLatin1Char('"');
    }
    }
}

core::LightingDesk::Type deskType(int cached) { return core::LightingDesk::Type(cached); }

} // namespace

// ══ TriggerActionEditor ══════════════════════════════════════════════════════

TriggerActionEditor::TriggerActionEditor(QWidget *parent) : QFrame(parent)
{
    setObjectName(QStringLiteral("ltCard"));
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(12, 10, 12, 12);
    outer->setSpacing(8);

    // Header: title · [kind · Presets ▾ when custom] · Custom toggle · Test
    auto *head = new QHBoxLayout();
    head->setSpacing(8);
    m_title = capsLabel(tr("SENDS"), this);
    m_kind = new QComboBox(this);
    m_kind->setObjectName(QStringLiteral("ltKind"));
    m_kind->addItem(tr("OSC"),      int(TriggerAction::Kind::Osc));
    m_kind->addItem(tr("MIDI"),     int(TriggerAction::Kind::Midi));
    m_kind->addItem(tr("MSC"),      int(TriggerAction::Kind::Msc));
    m_custom = new QCheckBox(tr("Custom OSC / MIDI"), this);
    m_custom->setObjectName(QStringLiteral("ltCustom"));
    m_custom->setToolTip(tr("For experts: type the exact OSC, MIDI or MSC message yourself"));
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
    head->addStretch(0);
    head->addWidget(m_custom);
    head->addWidget(m_presets);
    head->addWidget(m_test);
    outer->addLayout(head);

    auto makeForm = [](QWidget *w) {
        auto *form = new QFormLayout(w);
        form->setContentsMargins(0, 0, 0, 0);
        form->setHorizontalSpacing(10);
        form->setVerticalSpacing(6);
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        return form;
    };

    // ── Simple mode ────────────────────────────────────────────────────
    m_simpleBox = new QWidget(this);
    m_simpleForm = makeForm(m_simpleBox);
    {
        QWidget *w = m_simpleBox;
        m_do = new QComboBox(w);
        m_do->setObjectName(QStringLiteral("ltDo"));
        m_number = new QLineEdit(w);
        m_number->setObjectName(QStringLiteral("ltNumber"));
        m_list = spin(1, 9999, w);
        m_list->setObjectName(QStringLiteral("ltList"));

        m_levelRow = new QWidget(w);
        auto *lv = new QHBoxLayout(m_levelRow);
        lv->setContentsMargins(0, 0, 0, 0);
        lv->setSpacing(8);
        m_levelSlider = new QSlider(Qt::Horizontal, m_levelRow);
        m_levelSlider->setObjectName(QStringLiteral("ltLevelSlider"));
        m_levelSlider->setRange(0, 100);
        m_levelSlider->setPageStep(10);
        m_level = spin(0, 100, m_levelRow);
        m_level->setObjectName(QStringLiteral("ltLevel"));
        m_level->setSuffix(QStringLiteral(" %"));
        lv->addWidget(m_levelSlider, 1);
        lv->addWidget(m_level);

        m_hold = new QDoubleSpinBox(w);
        m_hold->setObjectName(QStringLiteral("ltHold"));
        m_hold->setRange(0.02, 30.0);
        m_hold->setDecimals(2);
        m_hold->setSingleStep(0.05);
        m_hold->setValue(0.25);
        m_hold->setSuffix(QStringLiteral(" s"));
        m_hold->setKeyboardTracking(false);
        m_hold->setToolTip(tr("How long the button stays pressed"));

        m_text = new QLineEdit(w);
        m_text->setObjectName(QStringLiteral("ltCommand"));
        m_text->setPlaceholderText(tr("e.g. Chan 1 At Full"));

        m_cueCombo = new QComboBox(w);
        m_cueCombo->setObjectName(QStringLiteral("ltFireCue"));
        m_cueCombo->setMinimumContentsLength(18);
        m_cueCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);

        m_preview = new QLabel(w);
        m_preview->setObjectName(QStringLiteral("ltPreview"));
        m_preview->setWordWrap(true);
        m_preview->setStyleSheet(QStringLiteral("color:%1; font-size:12px;")
                                     .arg(Theme::tokens().ink60.name()));
        m_problem = helpLabel(QString(), w);
        m_problem->setObjectName(QStringLiteral("ltProblem"));
        m_problem->setStyleSheet(QStringLiteral("color:%1; font-size:11px;")
                                     .arg(Theme::tokens().warn.name()));

        m_simpleForm->addRow(tr("What should it do?"), m_do);
        m_simpleForm->addRow(tr("Cue"), m_number);
        m_simpleForm->addRow(tr("Cue list"), m_list);
        m_simpleForm->addRow(tr("Level"), m_levelRow);
        m_simpleForm->addRow(tr("Hold"), m_hold);
        m_simpleForm->addRow(tr("Command"), m_text);
        m_simpleForm->addRow(tr("Cue"), m_cueCombo);
        m_simpleForm->addRow(QString(), m_preview);
        m_simpleForm->addRow(QString(), m_problem);

        connect(m_do, &QComboBox::currentIndexChanged, this, [this](int) {
            const int code = m_do->currentData().toInt();
            change([code](TriggerAction &a) {
                if (code == kDoNothing) {
                    a.kind = TriggerAction::Kind::None;
                } else if (code == kDoFireCue) {
                    a.kind = TriggerAction::Kind::FireCue;
                } else {
                    a.kind = TriggerAction::Kind::Desk;
                    a.deskDo = DeskDo(code);
                }
            });
        });
        connect(m_number, &QLineEdit::editingFinished, this, [this] {
            change([this](TriggerAction &a) { a.number = m_number->text().trimmed(); });
        });
        connect(m_list, &QSpinBox::valueChanged, this, [this](int v) {
            change([v](TriggerAction &a) { a.list = v; });
        });
        connect(m_levelSlider, &QSlider::valueChanged, this, [this](int v) {
            if (m_loading) return;
            {
                const QSignalBlocker b(m_level);
                m_level->setValue(v);
            }
            change([v](TriggerAction &a) { a.level = v; });
        });
        connect(m_level, &QSpinBox::valueChanged, this, [this](int v) {
            if (m_loading) return;
            {
                const QSignalBlocker b(m_levelSlider);
                m_levelSlider->setValue(v);
            }
            change([v](TriggerAction &a) { a.level = v; });
        });
        connect(m_hold, &QDoubleSpinBox::valueChanged, this, [this](double v) {
            change([v](TriggerAction &a) { a.hold = v; });
        });
        connect(m_text, &QLineEdit::editingFinished, this, [this] {
            change([this](TriggerAction &a) { a.text = m_text->text().trimmed(); });
        });
        connect(m_cueCombo, &QComboBox::currentIndexChanged, this, [this](int) {
            const QUuid id = m_cueCombo->currentData().toUuid();
            change([id](TriggerAction &a) { a.cueId = id; });
        });
    }
    outer->addWidget(m_simpleBox);

    // ── Custom (expert) mode: one page per kind ────────────────────────
    m_customBox = new QWidget(this);
    auto *cv = new QVBoxLayout(m_customBox);
    cv->setContentsMargins(0, 0, 0, 0);
    m_pages = new QStackedWidget(m_customBox);
    cv->addWidget(m_pages);
    outer->addWidget(m_customBox);

    auto makePage = [this, makeForm](QFormLayout *&form) {
        auto *w = new QWidget(m_pages);
        form = makeForm(w);
        m_pages->addWidget(w);
        return w;
    };

    // ── OSC ────────────────────────────────────────────────────────────
    {
        QFormLayout *f = nullptr;
        auto *w = makePage(f);
        m_oscHost = new QLineEdit(w);
        m_oscHost->setPlaceholderText(tr("lighting desk (Preferences)"));
        m_oscHost->setToolTip(tr("Leave blank to send to the lighting desk set in Preferences"));
        m_oscPort = spin(0, 65535, w);
        m_oscPort->setSpecialValueText(tr("desk port"));
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
    connect(m_kind, &QComboBox::currentIndexChanged, this, [this](int) {
        const auto k = TriggerAction::Kind(m_kind->currentData().toInt());
        change([k](TriggerAction &a) { a.kind = k; });
    });
    connect(m_custom, &QCheckBox::toggled, this, [this](bool on) {
        if (!m_loading) setCustom(on);
    });
    connect(m_test, &QPushButton::clicked, this, [this] { emit testRequested(m_action); });
    connect(m_presets, &QPushButton::clicked, this, [this] {
        QMenu menu(this);
        const QStringList names = presetNames();
        for (int i = 0; i < names.size(); ++i)
            menu.addAction(names.at(i), this, [this, i] { applyPreset(i); });
        menu.exec(m_presets->mapToGlobal(QPoint(0, m_presets->height())));
    });

    m_deskType = int(core::LightingDesk::load().type);
    load();
}

bool TriggerActionEditor::isCustom() const
{
    using K = TriggerAction::Kind;
    return m_action.kind == K::Osc || m_action.kind == K::Midi || m_action.kind == K::Msc;
}

void TriggerActionEditor::refreshDesk()
{
    const int t = int(core::LightingDesk::load().type);
    if (t == m_deskType) return;
    m_deskType = t;
    load();
}

void TriggerActionEditor::setCustom(bool on)
{
    if (on == isCustom()) return;
    change([this, on](TriggerAction &a) {
        using K = TriggerAction::Kind;
        if (!on) {                 // back to simple mode: "Nothing"
            a.kind = K::None;
            return;
        }
        const bool fromDesk = a.kind == K::Desk;
        const TriggerAction desk = a;
        a.kind = K::Osc;
        a.host.clear();
        a.port = 0;
        a.transport = 0;
        a.address.clear();
        a.args.clear();
        if (!fromDesk) return;
        // Show what simple mode would have sent, so it can be seen and tweaked.
        for (const auto &s : audio::deskSends(core::LightingDesk::load(), desk)) {
            if (s.type != audio::DeskSend::Type::Osc) continue;
            a.address = s.address;
            QStringList parts;
            for (const auto &v : s.args) parts << oscArgText(v);
            a.args = parts.join(QStringLiteral(", "));
            break;
        }
    });
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
    const bool newDo = a.deskDo != m_action.deskDo;
    m_action = a;
    // Only a new kind needs the widgets reloaded (to show and fill its page).
    // Reloading otherwise would clear the very combo whose signal got us here.
    if (newKind) {
        load();
    } else {
        updateSimpleRows();
        // Moving off an action marked "(not on this desk)" drops that item —
        // later, not inside the combo's own signal.
        if (newDo)
            QMetaObject::invokeMethod(this, [this] {
                m_loading = true;
                syncDoCombo();
                m_loading = false;
            }, Qt::QueuedConnection);
    }
    emit edited(m_action);
}

void TriggerActionEditor::syncDoCombo()
{
    const auto type = deskType(m_deskType);
    const bool isDesk = m_action.kind == TriggerAction::Kind::Desk;
    QList<QPair<QString, int>> want;
    want.append({tr("Nothing"), kDoNothing});
    for (const DeskDo d : kAllDeskDo) {
        const bool ok = audio::deskSupports(type, d);
        if (ok)
            want.append({TriggerAction::deskDoName(d), int(d)});
        else if (isDesk && m_action.deskDo == d)
            want.append({tr("%1 (not on this desk)").arg(TriggerAction::deskDoName(d)), int(d)});
    }
    want.append({tr("Fire a quewi cue"), kDoFireCue});

    bool same = m_do->count() == want.size();
    for (int i = 0; same && i < want.size(); ++i)
        same = m_do->itemText(i) == want[i].first && m_do->itemData(i).toInt() == want[i].second;
    if (!same && !m_do->view()->isVisible()) {
        m_do->clear();
        for (const auto &w : want) m_do->addItem(w.first, w.second);
    }
    m_do->setCurrentIndex(std::max(0, m_do->findData(doCodeOf(m_action))));
}

void TriggerActionEditor::updateSimpleRows()
{
    using K = TriggerAction::Kind;
    const auto &a = m_action;
    const bool desk = a.kind == K::Desk;
    const auto type = deskType(m_deskType);
    const DeskDo d = a.deskDo;

    bool number = false, list = false, level = false, hold = false, text = false;
    QString numberLabel, listLabel, numberHint = tr("e.g. 3");
    if (desk) {
        switch (d) {
        case DeskDo::Go: case DeskDo::Stop: case DeskDo::Back:
            break;
        case DeskDo::GoList:
            list = true;
            if (type == core::LightingDesk::Type::Ma3) {
                listLabel = tr("Sequence");
            } else {
                // Eos can only GO another list through the fader it's on.
                number = true;
                numberLabel = tr("Fader");
                numberHint = tr("the fader the cue list is on");
                listLabel = tr("Fader page");
            }
            break;
        case DeskDo::GoToCue:
            number = list = true;
            numberLabel = tr("Cue");
            numberHint = tr("e.g. 12.5");
            listLabel = type == core::LightingDesk::Type::Ma3 ? tr("Sequence") : tr("Cue list");
            break;
        case DeskDo::SubLevel:
            number = level = true;
            numberLabel = tr("Sub");
            break;
        case DeskDo::SubBump:
            number = hold = true;
            numberLabel = tr("Sub");
            break;
        case DeskDo::FaderLevel:
            number = list = level = true;
            numberLabel = tr("Fader");
            listLabel = tr("Fader page");
            break;
        case DeskDo::FaderBump:
            number = list = hold = true;
            numberLabel = tr("Fader");
            listLabel = tr("Fader page");
            break;
        case DeskDo::Macro:
            number = true;
            numberLabel = tr("Macro");
            break;
        case DeskDo::Command:
            text = true;
            break;
        }
    }
    auto setLabel = [this](QWidget *field, const QString &text) {
        if (auto *l = qobject_cast<QLabel *>(m_simpleForm->labelForField(field)))
            l->setText(text);
    };
    m_simpleForm->setRowVisible(m_number, number);
    m_simpleForm->setRowVisible(m_list, list);
    m_simpleForm->setRowVisible(m_levelRow, level);
    m_simpleForm->setRowVisible(m_hold, hold);
    m_simpleForm->setRowVisible(m_text, text);
    m_simpleForm->setRowVisible(m_cueCombo, a.kind == K::FireCue);
    if (number) {
        setLabel(m_number, numberLabel);
        m_number->setPlaceholderText(numberHint);
    }
    if (list) setLabel(m_list, listLabel);

    // The plain-English line, and why it won't work if it won't.
    QString preview, problem;
    switch (a.kind) {
    case K::None:    preview = tr("Sends nothing."); break;
    case K::FireCue: preview = tr("Fires that cue as if you pressed GO on it."); break;
    case K::Desk: {
        preview = a.summary();
        QString err;
        if (audio::deskSends(core::LightingDesk{.type = type}, a, &err).empty()) problem = err;
        break;
    }
    default:         preview = a.summary(); break;
    }
    m_preview->setText(preview);
    m_problem->setText(problem);
    m_simpleForm->setRowVisible(m_problem, !problem.isEmpty());
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
    const bool custom = isCustom();
    m_custom->setChecked(custom);
    m_kind->setVisible(custom);
    m_presets->setVisible(custom);
    m_simpleBox->setVisible(!custom);
    m_customBox->setVisible(custom);
    m_test->setEnabled(a.kind != K::None);

    if (custom) {
        const int page = int(a.kind) - int(K::Osc);   // Osc, Midi, Msc
        m_kind->setCurrentIndex(std::max(0, m_kind->findData(int(a.kind))));
        m_pages->setCurrentIndex(page);
        // Size the stack to the page showing, not the tallest one, so OSC
        // doesn't leave a hole the height of the MIDI form.
        for (int i = 0; i < m_pages->count(); ++i) {
            const auto pol = i == page ? QSizePolicy::Preferred : QSizePolicy::Ignored;
            m_pages->widget(i)->setSizePolicy(pol, pol);
        }
        m_pages->adjustSize();
    } else {
        syncDoCombo();
        m_number->setText(a.number);
        m_list->setValue(std::max(1, a.list));
        m_level->setValue(std::clamp(a.level, 0, 100));
        m_levelSlider->setValue(std::clamp(a.level, 0, 100));
        m_hold->setValue(std::clamp(a.hold, 0.02, 30.0));
        m_text->setText(a.text);
        updateSimpleRows();
    }

    switch (a.kind) {
    case K::None:
    case K::Desk:
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
        "QPushButton#ltButton, QPushButton[ltButton=\"true\"] { background:%2; color:%3;"
        "  border:none; border-radius:3px; padding:5px 12px; font-size:12px; font-weight:600; }"
        "QPushButton#ltButton:hover, QPushButton[ltButton=\"true\"]:hover { background:%4; }"
        "QPushButton#ltButton:pressed, QPushButton[ltButton=\"true\"]:pressed { background:%5; }"
        "QPushButton#ltButton:disabled, QPushButton[ltButton=\"true\"]:disabled { color:%6; }"
        "QTableWidget#ltTable { background:%1; border:none; border-radius:4px; }")
        .arg(tk.bgPanel.name(),                    // %1 card
             tk.bgInteractive.name(),              // %2 button
             tk.ink100.name(),                     // %3 text
             tk.bgRowHover.name(),                 // %4 hover
             tk.bgInteractive.darker(115).name(),  // %5 pressed
             tk.ink40.name()));                    // %6 disabled text

    auto *page = new QVBoxLayout(this);
    page->setContentsMargins(16, 10, 16, 16);
    page->setSpacing(10);

    // ── Top: which desk the triggers talk to ───────────────────────────
    auto *deskRow = new QHBoxLayout();
    deskRow->setSpacing(8);
    m_deskLabel = new QLabel(this);
    m_deskLabel->setObjectName(QStringLiteral("ltDesk"));
    m_deskLabel->setStyleSheet(QStringLiteral("color:%1; font-size:12px;").arg(tk.ink60.name()));
    m_deskLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto *changeDesk = new QPushButton(tr("Change…"), this);
    changeDesk->setObjectName(QStringLiteral("ltButton"));
    changeDesk->setCursor(Qt::PointingHandCursor);
    changeDesk->setToolTip(tr("Set the lighting desk in Preferences → Lighting"));
    deskRow->addWidget(m_deskLabel);
    deskRow->addWidget(changeDesk);
    deskRow->addStretch(1);
    page->addLayout(deskRow);
    connect(changeDesk, &QPushButton::clicked, this, &LightTriggersPanel::deskSettingsRequested);

    // ── Beat grid: the song's tempo, for snapping and "Fill with beats…" ──
    // TEMPO [Off ▴▾] BPM  Tap  Detect │ First beat [0.000 s] Set to cursor │
    // Beats/bar [4]  ☑ Snap │ Fill with beats…   Detected 128.00 BPM
    auto *gridRow = new QHBoxLayout();
    gridRow->setSpacing(6);
    gridRow->addWidget(capsLabel(tr("TEMPO"), this));
    m_bpm = new QDoubleSpinBox(this);
    m_bpm->setObjectName(QStringLiteral("ltBpm"));
    m_bpm->setRange(0.0, 300.0);
    m_bpm->setDecimals(2);
    m_bpm->setSingleStep(1.0);
    m_bpm->setSpecialValueText(tr("Off"));
    m_bpm->setSuffix(tr(" BPM"));
    m_bpm->setKeyboardTracking(false);
    m_bpm->setToolTip(tr("The song's tempo. 0 = no beat grid"));
    m_tapBtn = new QPushButton(tr("Tap"), this);
    m_tapBtn->setObjectName(QStringLiteral("ltTap"));
    m_tapBtn->setToolTip(tr("Tap along to the beat (or press T). While the preview plays, "
                            "the taps also place the first beat"));
    m_detectBtn = new QPushButton(tr("Detect"), this);
    m_detectBtn->setObjectName(QStringLiteral("ltDetect"));
    m_detectBtn->setToolTip(tr("Listen to the song and find its tempo and first beat"));
    auto *firstLabel = new QLabel(tr("First beat"), this);
    m_firstBeat = new QDoubleSpinBox(this);
    m_firstBeat->setObjectName(QStringLiteral("ltFirstBeat"));
    m_firstBeat->setRange(0.0, 86400.0);
    m_firstBeat->setDecimals(3);
    m_firstBeat->setSingleStep(0.01);
    m_firstBeat->setSuffix(QStringLiteral(" s"));
    m_firstBeat->setKeyboardTracking(false);
    m_firstBeat->setToolTip(tr("Where beat 1 of bar 1 falls in the song"));
    auto *setFirst = new QPushButton(tr("Set to cursor"), this);
    setFirst->setObjectName(QStringLiteral("ltSetFirstBeat"));
    setFirst->setToolTip(tr("Beat 1 of bar 1 is at the edit cursor"));
    auto *bpbLabel = new QLabel(tr("Beats/bar"), this);
    m_beatsPerBar = spin(1, 16, this);
    m_beatsPerBar->setObjectName(QStringLiteral("ltBeatsPerBar"));
    m_beatsPerBar->setValue(4);
    m_snap = new QCheckBox(tr("Snap"), this);
    m_snap->setObjectName(QStringLiteral("ltSnap"));
    m_snap->setToolTip(tr("Markers you place or drag in the lighting lane land on the beat "
                          "(hold Alt to place freely)"));
    {
        QSettings s(QStringLiteral("ServeGaming"), QStringLiteral("quewi"));
        m_snap->setChecked(s.value(QStringLiteral("triggers/snapToBeats"), true).toBool());
    }
    m_fillBtn = new QPushButton(tr("Fill with beats…"), this);
    m_fillBtn->setObjectName(QStringLiteral("ltFill"));
    m_fillBtn->setToolTip(tr("Add a marker on every beat (or bar) of a section — "
                             "e.g. bump a sub on each beat of the chorus"));
    m_gridStatus = new QLabel(this);
    m_gridStatus->setObjectName(QStringLiteral("ltGridStatus"));
    m_gridStatus->setStyleSheet(QStringLiteral("color:%1; font-size:11px;").arg(tk.ink60.name()));
    for (auto *l : {firstLabel, bpbLabel})
        l->setStyleSheet(QStringLiteral("color:%1; font-size:12px;").arg(tk.ink60.name()));
    for (auto *b : {m_tapBtn, m_detectBtn, setFirst, m_fillBtn}) {
        b->setProperty("ltButton", true);      // the ltButton look, keeping its own name
        b->setCursor(Qt::PointingHandCursor);
    }
    gridRow->addWidget(m_bpm);
    gridRow->addWidget(m_tapBtn);
    gridRow->addWidget(m_detectBtn);
    gridRow->addSpacing(10);
    gridRow->addWidget(firstLabel);
    gridRow->addWidget(m_firstBeat);
    gridRow->addWidget(setFirst);
    gridRow->addSpacing(10);
    gridRow->addWidget(bpbLabel);
    gridRow->addWidget(m_beatsPerBar);
    gridRow->addWidget(m_snap);
    gridRow->addSpacing(10);
    gridRow->addWidget(m_fillBtn);
    gridRow->addWidget(m_gridStatus, 1);
    page->addLayout(gridRow);
    m_tapClock.start();

    connect(m_bpm, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        if (m_loading || !m_cue) return;
        auto g = m_cue->beatGrid();
        g.bpm = v;
        setBeatGrid(g, true);
    });
    connect(m_firstBeat, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        if (m_loading || !m_cue) return;
        auto g = m_cue->beatGrid();
        g.firstBeat = v;
        setBeatGrid(g, true);
    });
    connect(m_beatsPerBar, &QSpinBox::valueChanged, this, [this](int v) {
        if (m_loading || !m_cue) return;
        auto g = m_cue->beatGrid();
        g.beatsPerBar = v;
        setBeatGrid(g, true);
    });
    // On press, not release: the tap is when the finger lands.
    connect(m_tapBtn, &QPushButton::pressed, this, [this] { tap(); });
    connect(m_detectBtn, &QPushButton::clicked, this, [this] { detectTempo(); });
    connect(setFirst, &QPushButton::clicked, this, [this] {
        if (!m_cue) return;
        auto g = m_cue->beatGrid();
        g.firstBeat = m_cursor;
        setBeatGrid(g, false);
    });
    connect(m_snap, &QCheckBox::toggled, this, [this](bool on) {
        QSettings s(QStringLiteral("ServeGaming"), QStringLiteral("quewi"));
        s.setValue(QStringLiteral("triggers/snapToBeats"), on);
        emit snapToBeatsChanged(on);
    });
    connect(m_fillBtn, &QPushButton::clicked, this, [this] { openFillDialog(); });

    auto *outer = new QHBoxLayout();
    outer->setSpacing(14);
    page->addLayout(outer, 1);

    // ── Left: list + buttons ───────────────────────────────────────────
    auto *left = new QVBoxLayout();
    left->setSpacing(8);
    auto *btnRow = new QHBoxLayout();
    btnRow->setSpacing(6);
    auto *addPoint = new QPushButton(tr("+ Point here"), this);
    auto *addRange = new QPushButton(tr("+ Range"), this);
    m_duplicateBtn = new QPushButton(tr("Duplicate"), this);
    m_deleteBtn    = new QPushButton(tr("Delete"), this);
    addRange->setToolTip(tr("A range from where the song is playing (or the edit cursor), "
                            "%1 s long: sends one thing as the song enters it and another as "
                            "it leaves").arg(kDefaultRangeSeconds));
    addPoint->setToolTip(tr("A single hit where the song is playing (or at the edit "
                            "cursor when stopped). Shortcut: M"));
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

    m_duplicateBtn->setToolTip(tr("Copy the selected triggers. Several are copied as a "
                                  "block, straight after the originals"));
    m_deleteBtn->setToolTip(tr("Delete the selected triggers (Delete)"));

    m_table = new QTableWidget(0, 6, this);
    m_table->setObjectName(QStringLiteral("ltTable"));
    m_table->setHorizontalHeaderLabels({tr("On"), tr("Name"), tr("Group"), tr("Start"),
                                        tr("End"), tr("Sends")});
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    // Ctrl / Shift pick several; Ctrl+A picks all.
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setShowGrid(false);
    m_table->setWordWrap(false);
    m_table->verticalHeader()->setDefaultSectionSize(24);
    auto *hh = m_table->horizontalHeader();
    hh->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    hh->setSectionResizeMode(1, QHeaderView::Interactive);
    hh->setSectionResizeMode(2, QHeaderView::Interactive);
    hh->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    hh->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    hh->setSectionResizeMode(5, QHeaderView::Stretch);
    m_table->setColumnWidth(1, 130);
    m_table->setColumnWidth(2, 90);
    auto *delKey = new QShortcut(QKeySequence::Delete, m_table);
    delKey->setContext(Qt::WidgetShortcut);
    connect(delKey, &QShortcut::activated, this, &LightTriggersPanel::deleteSelection);
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_table, &QWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        const int row = m_table->rowAt(pos.y());
        if (row < 0 || row >= m_rowIds.size()) return;
        const QUuid id = m_rowIds.at(row);
        const int i = indexOf(id);
        if (i < 0) return;
        const auto t = m_cue->lightTriggers()[size_t(i)];
        const int n = m_selection.contains(id) ? int(m_selection.size()) : 1;
        QMenu menu(this);
        auto *groupAct = menu.addAction(n > 1 ? tr("Group %1 Triggers…").arg(n) : tr("Group…"));
        QAction *selectGroupAct = nullptr, *ungroupAct = nullptr;
        if (!t.group.isEmpty()) {
            selectGroupAct = menu.addAction(tr("Select Group \"%1\"").arg(t.group));
            ungroupAct = menu.addAction(tr("Ungroup"));
        }
        menu.addSeparator();
        auto *dupAct = menu.addAction(tr("Duplicate"));
        auto *delAct = menu.addAction(n > 1 ? tr("Delete %1 Triggers").arg(n) : tr("Delete"));
        QAction *chosen = menu.exec(m_table->viewport()->mapToGlobal(pos));
        if (!chosen) return;
        if (!m_selection.contains(id)) setSelection({id}, id);
        if (chosen == groupAct)                      applyTriggerAction(id, QStringLiteral("group"));
        else if (chosen == selectGroupAct)           selectGroup(t.group);
        else if (chosen == ungroupAct)               applyTriggerAction(id, QStringLiteral("ungroup"));
        else if (chosen == dupAct)                   duplicateSelection();
        else if (chosen == delAct)                   deleteSelection();
    });
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

    m_multiLabel = new QLabel(form);
    m_multiLabel->setObjectName(QStringLiteral("ltMultiLabel"));
    m_multiLabel->setWordWrap(true);
    m_multiLabel->setStyleSheet(QStringLiteral("color:%1; font-weight:600;").arg(tk.accent.name()));
    m_multiLabel->hide();
    fv->addWidget(m_multiLabel);

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
    m_group = new QComboBox(card);
    m_group->setObjectName(QStringLiteral("ltGroup"));
    m_group->setEditable(true);
    m_group->setInsertPolicy(QComboBox::NoInsert);
    m_group->lineEdit()->setPlaceholderText(tr("None"));
    m_group->setToolTip(tr("Triggers in a group are picked and edited together: click one in "
                           "the lighting lane and the whole group is selected (Alt+click picks "
                           "just one). Type a new name to make a group"));
    m_selectGroupBtn = new QPushButton(card);
    m_selectGroupBtn->setObjectName(QStringLiteral("ltButton"));
    m_selectGroupBtn->setCursor(Qt::PointingHandCursor);
    auto *groupRow = new QHBoxLayout();
    groupRow->setContentsMargins(0, 0, 0, 0);
    groupRow->addWidget(m_group, 1);
    groupRow->addWidget(m_selectGroupBtn);
    cf->addRow(tr("Name"), m_name);
    cf->addRow(tr("Group"), groupRow);
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
    connect(addPoint, &QPushButton::clicked, this, [this] { addPointHere(); });
    connect(addRange, &QPushButton::clicked, this, [this] {
        const double s = hereSeconds();
        addTrigger(s, s + kDefaultRangeSeconds);
    });
    connect(m_duplicateBtn, &QPushButton::clicked, this, &LightTriggersPanel::duplicateSelection);
    connect(m_deleteBtn, &QPushButton::clicked, this, &LightTriggersPanel::deleteSelection);

    connect(m_table, &QTableWidget::itemSelectionChanged, this, [this] {
        if (m_loading) return;
        auto rows = m_table->selectionModel()->selectedRows();
        if (rows.isEmpty()) return;               // keep the editor on what it showed
        std::sort(rows.begin(), rows.end(),
                  [](const QModelIndex &a, const QModelIndex &b) { return a.row() < b.row(); });
        QList<QUuid> ids;
        for (const auto &r : rows)
            if (r.row() < m_rowIds.size()) ids << m_rowIds.at(r.row());
        const int cur = m_table->currentRow();
        const QUuid primary = cur >= 0 && cur < m_rowIds.size() && ids.contains(m_rowIds.at(cur))
                                  ? m_rowIds.at(cur) : ids.value(0);
        setSelection(ids, primary);
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
    connect(m_enabled, &QCheckBox::checkStateChanged, this, [this](Qt::CheckState st) {
        if (st == Qt::PartiallyChecked) return;   // "some are": only shown, never set
        const bool on = st == Qt::Checked;
        editSelected([on](LightTrigger &t) { t.enabled = on; }, false);
    });
    auto setGroup = [this] {
        if (m_loading || m_selection.isEmpty()) return;
        const QString g = m_group->currentText().trimmed();
        editSelected([g](LightTrigger &t) { t.group = g; }, false);
    };
    connect(m_group, &QComboBox::activated, this, setGroup);
    connect(m_group->lineEdit(), &QLineEdit::editingFinished, this, setGroup);
    connect(m_selectGroupBtn, &QPushButton::clicked, this, [this] {
        if (const int i = indexOf(m_selectedId); i >= 0)
            selectGroup(m_cue->lightTriggers()[size_t(i)].group);
    });
    // With several selected, the editor shows the primary's actions; an edit
    // goes to it as is and to the others as just the fields that changed.
    connect(m_enter, &TriggerActionEditor::edited, this, [this](const TriggerAction &a) {
        const int i = indexOf(m_selectedId);
        if (i < 0) return;
        const TriggerAction before = m_cue->lightTriggers()[size_t(i)].enter;
        const QUuid primary = m_selectedId;
        editSelected([&](LightTrigger &t) {
            if (t.id == primary) t.enter = a;
            else                 t.enter.applyChange(before, a);
        });
    });
    connect(m_exit, &TriggerActionEditor::edited, this, [this](const TriggerAction &a) {
        const int i = indexOf(m_selectedId);
        if (i < 0) return;
        const TriggerAction before = m_cue->lightTriggers()[size_t(i)].exit;
        const QUuid primary = m_selectedId;
        editSelected([&](LightTrigger &t) {
            if (t.id == primary)  t.exit = a;
            else if (t.isRange()) t.exit.applyChange(before, a);   // points have no exit
        });
    });
    connect(m_enter, &TriggerActionEditor::testRequested, this, &LightTriggersPanel::testRequested);
    connect(m_exit,  &TriggerActionEditor::testRequested, this, &LightTriggersPanel::testRequested);

    setCursorSeconds(0.0);
    refreshDesk();
    refresh();
}

void LightTriggersPanel::refreshDesk()
{
    m_deskLabel->setText(tr("Lighting desk: %1").arg(core::LightingDesk::load().summary()));
    m_enter->refreshDesk();
    m_exit->refreshDesk();
}

void LightTriggersPanel::showEvent(QShowEvent *e)
{
    QWidget::showEvent(e);
    refreshDesk();     // it may have changed in Preferences meanwhile
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
    m_midiPorts = f;
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
    setSelection(id.isNull() ? QList<QUuid>{} : QList<QUuid>{id}, id);
}

QList<QUuid> LightTriggersPanel::selectedTriggers() const
{
    QList<QUuid> out;
    for (const QUuid &id : m_rowIds)             // song order
        if (m_selection.contains(id)) out << id;
    return out;
}

void LightTriggersPanel::setSelection(const QList<QUuid> &ids, const QUuid &primary)
{
    QList<QUuid> clean;
    for (const QUuid &id : ids)
        if (indexOf(id) >= 0 && !clean.contains(id)) clean << id;
    const QUuid p = clean.contains(primary) ? primary : clean.value(0);
    if (clean == m_selection && p == m_selectedId) return;
    const bool primaryChanged = p != m_selectedId;
    m_selection = clean;
    m_selectedId = p;
    applyTableSelection();
    loadEditor();
    if (primaryChanged) emit selectionChanged(p);
    emit selectionSetChanged(m_selection);
}

void LightTriggersPanel::clickTrigger(const QUuid &id, Qt::KeyboardModifiers mods)
{
    const int i = indexOf(id);
    if (i < 0) return;
    const QString group = m_cue->lightTriggers()[size_t(i)].group;
    auto withGroup = [&] {
        QList<QUuid> g;
        if (group.isEmpty()) return QList<QUuid>{id};
        for (const auto &t : m_cue->lightTriggers())
            if (t.group == group) g << t.id;
        return g;
    };
    QList<QUuid> sel = m_selection;
    QUuid primary = id;
    if (mods & Qt::ControlModifier) {
        if (sel.contains(id)) {
            sel.removeAll(id);
            primary = m_selectedId == id ? sel.value(sel.size() - 1) : m_selectedId;
        } else {
            sel << id;
        }
    } else if (mods & Qt::ShiftModifier) {
        for (const QUuid &g : withGroup())
            if (!sel.contains(g)) sel << g;
    } else if (mods & Qt::AltModifier) {
        sel = {id};
    } else {
        sel = withGroup();
    }
    setSelection(sel, primary);
    // The lane already drew its own guess; make sure it matches.
    emit selectionSetChanged(m_selection);
}

void LightTriggersPanel::selectSpan(double from, double to)
{
    if (!m_cue) return;
    QList<QUuid> sel = m_selection;
    for (const auto &t : m_cue->lightTriggers())
        if (t.start >= from && t.start <= to && !sel.contains(t.id)) sel << t.id;
    setSelection(sel, m_selectedId.isNull() ? sel.value(0) : m_selectedId);
    emit selectionSetChanged(m_selection);
}

void LightTriggersPanel::selectGroup(const QString &group)
{
    if (!m_cue || group.isEmpty()) return;
    QList<QUuid> sel;
    for (const auto &t : m_cue->lightTriggers())
        if (t.group == group) sel << t.id;
    setSelection(sel, sel.contains(m_selectedId) ? m_selectedId : sel.value(0));
}

void LightTriggersPanel::moveTriggersBy(const QList<QUuid> &ids, double delta)
{
    if (!m_cue || ids.isEmpty() || delta == 0.0) return;
    LightTriggers next = m_cue->lightTriggers();
    double minStart = 1e300;
    for (const auto &t : next)
        if (ids.contains(t.id)) minStart = std::min(minStart, t.start);
    if (minStart > 1e299) return;
    delta = std::max(delta, -minStart);          // nothing before the song starts
    for (auto &t : next) {
        if (!ids.contains(t.id)) continue;
        const bool range = t.isRange();
        t.start += delta;
        if (range) t.end += delta;
    }
    commit(next, false);
}

void LightTriggersPanel::deleteSelection()
{
    if (!m_cue || m_selection.isEmpty()) return;
    // Keep the row after the block selected so Delete can be pressed again.
    int lastRow = -1;
    for (const QUuid &id : m_selection) lastRow = std::max(lastRow, int(m_rowIds.indexOf(id)));
    QUuid neighbour;
    for (int r = lastRow + 1; r < m_rowIds.size() && neighbour.isNull(); ++r)
        if (!m_selection.contains(m_rowIds.at(r))) neighbour = m_rowIds.at(r);
    for (int r = lastRow - 1; r >= 0 && neighbour.isNull(); --r)
        if (!m_selection.contains(m_rowIds.at(r))) neighbour = m_rowIds.at(r);
    LightTriggers next = m_cue->lightTriggers();
    const QList<QUuid> gone = m_selection;
    next.erase(std::remove_if(next.begin(), next.end(),
                              [&](const LightTrigger &t) { return gone.contains(t.id); }),
               next.end());
    m_selection.clear();
    m_selectedId = QUuid();
    commit(next, false);
    setSelection(neighbour.isNull() ? QList<QUuid>{} : QList<QUuid>{neighbour}, neighbour);
    emit selectionChanged(m_selectedId);
    emit selectionSetChanged(m_selection);
}

void LightTriggersPanel::groupSelection(const QString &name)
{
    const QString g = name.trimmed();
    editSelected([&g](LightTrigger &t) { t.group = g; }, false);
}

void LightTriggersPanel::duplicateSelection()
{
    if (!m_cue || m_selection.isEmpty()) return;
    const LightTriggers &all = m_cue->lightTriggers();
    LightTriggers picked;
    for (const auto &t : all)
        if (m_selection.contains(t.id)) picked.push_back(t);
    if (picked.empty()) return;

    // One trigger: a second later, as ever. A block: straight after itself —
    // on the next bar when there's a beat grid, so a pattern repeats in time.
    double offset = 1.0;
    if (picked.size() > 1) {
        double lo = 1e300, hi = -1e300;
        for (const auto &t : picked) {
            lo = std::min(lo, t.start);
            hi = std::max(hi, t.isRange() ? t.end : t.start);
        }
        const double span = hi - lo;
        const auto &grid = m_cue->beatGrid();
        if (grid.isSet()) {
            const double bar = grid.beatLength() * std::max(1, grid.beatsPerBar);
            offset = std::max(1.0, std::ceil((span + grid.beatLength() * 0.5) / bar)) * bar;
        } else {
            offset = span + span / double(picked.size() - 1);
        }
        if (offset <= 0.0) offset = 1.0;
    }
    LightTriggers copies;
    QHash<QString, QString> newGroup;           // each group copied gets its own
    LightTriggers withCopies = all;
    for (auto t : picked) {
        t.id = QUuid::createUuid();
        if (picked.size() == 1 && !t.name.isEmpty()) t.name = tr("%1 copy").arg(t.name);
        t.start += offset;
        if (t.end >= 0.0) t.end += offset;
        if (!t.group.isEmpty()) {
            if (!newGroup.contains(t.group))
                newGroup.insert(t.group, audio::uniqueGroupName(withCopies, t.group));
            t.group = newGroup.value(t.group);
        }
        copies.push_back(t);
        withCopies.push_back(t);
    }
    addAndSelect(copies, QString());
}

void LightTriggersPanel::addAndSelect(const LightTriggers &added, const QString &groupBase)
{
    if (!m_cue || added.empty()) return;
    LightTriggers next = m_cue->lightTriggers();
    LightTriggers adding = added;
    if (!groupBase.isEmpty()) {
        const QString g = audio::uniqueGroupName(next, groupBase);
        for (auto &t : adding) t.group = g;
    }
    QList<QUuid> ids;
    for (const auto &t : adding) ids << t.id;
    next.insert(next.end(), adding.begin(), adding.end());
    commit(next, false);
    setSelection(ids, ids.value(0));
}

void LightTriggersPanel::applyTableSelection()
{
    const bool was = m_loading;
    m_loading = true;
    auto *sm = m_table->selectionModel();
    QItemSelection sel;
    int primaryRow = -1;
    for (int r = 0; r < m_rowIds.size(); ++r) {
        if (!m_selection.contains(m_rowIds.at(r))) continue;
        sel.select(m_table->model()->index(r, 0),
                   m_table->model()->index(r, m_table->columnCount() - 1));
        if (m_rowIds.at(r) == m_selectedId) primaryRow = r;
    }
    sm->select(sel, QItemSelectionModel::ClearAndSelect);
    if (primaryRow >= 0) {
        sm->setCurrentIndex(m_table->model()->index(primaryRow, 0), QItemSelectionModel::NoUpdate);
        m_table->scrollTo(m_table->model()->index(primaryRow, 0));
    } else {
        sm->setCurrentIndex(QModelIndex(), QItemSelectionModel::NoUpdate);
    }
    m_duplicateBtn->setEnabled(!m_selection.isEmpty());
    m_deleteBtn->setEnabled(!m_selection.isEmpty());
    m_loading = was;
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
    if (m_loading || !m_cue || m_selection.isEmpty()) return;
    LightTriggers next = m_cue->lightTriggers();
    for (auto &t : next)
        if (m_selection.contains(t.id)) f(t);
    commit(next, mergeable);
}

double LightTriggersPanel::hereSeconds() const
{
    // While the preview plays, "here" is what you're hearing — the edit
    // cursor stays where playback started, so every point used to land there.
    const double play = m_playhead ? m_playhead() : -1.0;
    const double at = play >= 0.0 ? play : m_cursor;
    return m_cue && snapToBeats() ? m_cue->beatGrid().snap(at) : at;
}

QUuid LightTriggersPanel::addPointHere() { return addTrigger(hereSeconds()); }

QUuid LightTriggersPanel::addTrigger(double start, double end)
{
    if (!m_cue) return {};
    LightTrigger t;
    t.name  = nextName();
    t.start = std::max(0.0, start);
    t.end   = end > t.start ? end : -1.0;
    // A fresh marker does the obvious thing: GO on the desk. A range's exit
    // starts as nothing.
    t.enter.kind   = TriggerAction::Kind::Desk;
    t.enter.deskDo = TriggerAction::DeskDo::Go;
    addAndSelect({t}, QString());
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
        if (!m_selection.contains(id)) setSelection({id}, id);
        const bool on = !t.enabled;
        editSelected([on](LightTrigger &x) { x.enabled = on; }, false);
    } else if (action == QLatin1String("delete")) {
        if (!m_selection.contains(id)) setSelection({id}, id);
        deleteSelection();
    } else if (action == QLatin1String("group")) {
        if (!m_selection.contains(id)) setSelection({id}, id);
        // Offer the selection's own group if it has one, else a fresh name.
        QString common = t.group;
        for (const auto &x : m_cue->lightTriggers())
            if (m_selection.contains(x.id) && x.group != common) { common.clear(); break; }
        bool ok = false;
        const QString n = QInputDialog::getText(
            this, tr("Group Triggers"),
            m_selection.size() > 1 ? tr("Group %1 triggers as:").arg(m_selection.size())
                                   : tr("Group name:"),
            QLineEdit::Normal,
            common.isEmpty() ? audio::uniqueGroupName(m_cue->lightTriggers(), tr("Group")) : common,
            &ok);
        if (!ok || !m_cue || n.trimmed().isEmpty()) return;
        groupSelection(n);
    } else if (action == QLatin1String("ungroup")) {
        if (!m_selection.contains(id)) setSelection({id}, id);
        groupSelection(QString());
    } else if (action == QLatin1String("selectGroup")) {
        selectGroup(t.group);
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

    // Triggers deleted elsewhere (undo, remote) leave the selection.
    const QUuid oldPrimary = m_selectedId;
    const int oldCount = int(m_selection.size());
    m_selection.removeIf([this](const QUuid &id) { return indexOf(id) < 0; });
    if (!m_selection.contains(m_selectedId)) m_selectedId = m_selection.value(0);
    const bool lostSelection = m_selectedId != oldPrimary;
    const bool selectionShrank = int(m_selection.size()) != oldCount;

    m_table->setRowCount(int(list.size()));
    m_rowIds.clear();
    for (int r = 0; r < int(list.size()); ++r) {
        const auto &t = list[size_t(r)];
        m_rowIds << t.id;

        auto *on = new QTableWidgetItem();
        on->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
        on->setCheckState(t.enabled ? Qt::Checked : Qt::Unchecked);
        on->setToolTip(tr("Enabled"));
        QString sends = actionText(t.enter);
        if (t.isRange()) sends += QStringLiteral("  →  ") + actionText(t.exit);
        QTableWidgetItem *cells[] = {
            on,
            new QTableWidgetItem(t.name.isEmpty() ? tr("(unnamed)") : t.name),
            new QTableWidgetItem(t.group),
            new QTableWidgetItem(secondsText(t.start)),
            new QTableWidgetItem(t.isRange() ? secondsText(t.end) : QStringLiteral("—")),
            new QTableWidgetItem(sends),
        };
        cells[3]->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        cells[4]->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        cells[5]->setToolTip(sends);
        for (int c = 0; c < 6; ++c) {
            if (c > 0) {
                cells[c]->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
                if (!t.enabled) cells[c]->setForeground(tk.ink40);
            }
            m_table->setItem(r, c, cells[c]);
        }
        colourRow(r);
    }
    applyTableSelection();
    loadGrid();
    m_loading = false;
    loadEditor();
    if (lostSelection) emit selectionChanged(m_selectedId);
    if (lostSelection || selectionShrank) emit selectionSetChanged(m_selection);
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
    const auto &all = m_cue->lightTriggers();
    const auto &t = all[size_t(i)];
    const bool multi = isMulti();
    m_editorStack->setCurrentIndex(1);
    {
        const QSignalBlocker b1(m_start), b2(m_end), b3(m_isRange), b4(m_enabled), b5(m_group);
        int on = 0, n = 0;
        QString common = t.group;
        for (const auto &x : all) {
            if (!m_selection.contains(x.id)) continue;
            ++n;
            if (x.enabled) ++on;
            if (x.group != common) common.clear();
        }
        // Don't overwrite a name being typed unless the selection moved on.
        if (multi) {
            m_name->clear();
            m_name->setPlaceholderText(tr("%1 triggers").arg(n));
        } else if (t.id != m_editorId || !m_name->hasFocus()) {
            m_name->setPlaceholderText(tr("e.g. Chorus wash"));
            m_name->setText(t.name);
        }
        m_editorId = t.id;
        m_name->setEnabled(!multi);
        m_start->setEnabled(!multi);
        m_isRange->setEnabled(!multi);
        m_start->setValue(t.start);
        m_isRange->setChecked(t.isRange());
        m_end->setEnabled(!multi && t.isRange());
        m_end->setValue(t.isRange() ? t.end : t.start);
        if (on == 0 || on == n) {
            m_enabled->setTristate(false);
            m_enabled->setChecked(on == n);
        } else {
            m_enabled->setCheckState(Qt::PartiallyChecked);   // some on, some off
        }

        const QStringList groups = audio::triggerGroups(all);
        if (!m_group->lineEdit()->hasFocus() || t.id != m_editorId) {
            m_group->clear();
            m_group->addItems(groups);
            m_group->setEditText(common);
            m_group->lineEdit()->setPlaceholderText(multi && common.isEmpty() && !t.group.isEmpty()
                                                        ? tr("(mixed)") : tr("None"));
        }
        int inGroup = 0;
        for (const auto &x : all) if (!t.group.isEmpty() && x.group == t.group) ++inGroup;
        int selectedInGroup = 0;
        for (const auto &x : all)
            if (!t.group.isEmpty() && x.group == t.group && m_selection.contains(x.id)) ++selectedInGroup;
        m_selectGroupBtn->setText(tr("Select all %1").arg(inGroup));
        m_selectGroupBtn->setToolTip(tr("Select every trigger in \"%1\"").arg(t.group));
        m_selectGroupBtn->setVisible(inGroup > 1 && selectedInGroup < inGroup);

        m_multiLabel->setVisible(multi);
        if (multi)
            m_multiLabel->setText(common.isEmpty()
                ? tr("%1 triggers selected. Changes apply to all of them.").arg(n)
                : tr("%1 triggers selected (group \"%2\"). Changes apply to all of them.")
                      .arg(n).arg(common));
    }
    m_enter->setTitle(t.isRange() ? tr("ON ENTER") : tr("SENDS"));
    m_enter->setAction(t.enter);
    m_exit->setAction(t.exit);
    m_exit->setVisible(t.isRange());
    m_duplicateBtn->setEnabled(true);
    m_deleteBtn->setEnabled(true);
    m_enter->setToolTip(multi ? tr("Shows the trigger you picked last. A change here is made "
                                   "to every selected trigger, and only that change: "
                                   "their other settings stay as they are") : QString());
    m_loading = wasLoading;
}

// ── Beat grid ────────────────────────────────────────────────────────────────

void LightTriggersPanel::loadGrid()
{
    const auto g = m_cue ? m_cue->beatGrid() : audio::BeatGrid{};
    const QSignalBlocker b1(m_bpm), b2(m_firstBeat), b3(m_beatsPerBar);
    // Don't fight a spin box being typed into or stepped.
    if (!m_bpm->hasFocus() || m_bpm->value() != g.bpm) m_bpm->setValue(g.bpm);
    if (!m_firstBeat->hasFocus() || m_firstBeat->value() != g.firstBeat) m_firstBeat->setValue(g.firstBeat);
    m_beatsPerBar->setValue(std::clamp(g.beatsPerBar, 1, 16));
    const bool on = m_cue != nullptr;
    for (QWidget *w : std::initializer_list<QWidget *>{m_bpm, m_tapBtn, m_firstBeat, m_beatsPerBar, m_fillBtn})
        w->setEnabled(on);
    m_detectBtn->setEnabled(on && !m_detecting);
    if (!g.isSet() && !m_detecting) m_gridStatus->clear();   // cleared (undo, remote)
}

void LightTriggersPanel::commitGrid(const audio::BeatGrid &g, int mergeId)
{
    if (!m_cue) return;
    const QJsonObject oldJson = m_cue->beatGrid().toJson();
    const QJsonObject newJson = audio::BeatGrid::fromJson(g.toJson()).toJson();
    if (oldJson == newJson) return;
    if (m_undo) {
        auto *cmd = new TriggersEditCommand(m_cue, QStringLiteral("beatGrid"), QVariant(oldJson),
                                            QVariant(newJson), mergeId);
        cmd->setText(tr("Edit beat grid"));
        m_undo->push(cmd);
    } else {
        m_cue->setField(QStringLiteral("beatGrid"), QVariant(newJson));
    }
    loadGrid();
}

void LightTriggersPanel::setBeatGrid(const audio::BeatGrid &g, bool mergeable)
{
    commitGrid(g, mergeable ? TriggersEditCommand::kMergeFieldEdits : -1);
}

void LightTriggersPanel::setPlayheadProvider(std::function<double()> f) { m_playhead = std::move(f); }

void LightTriggersPanel::setDetectSource(std::function<bool(std::vector<float> &, int &)> f)
{
    m_detectSource = std::move(f);
}

void LightTriggersPanel::setSongLength(double seconds) { m_songLength = std::max(0.0, seconds); }

bool LightTriggersPanel::snapToBeats() const { return m_snap->isChecked(); }

void LightTriggersPanel::setSnapToBeats(bool on) { m_snap->setChecked(on); }

void LightTriggersPanel::tap()
{
    tapAt(double(m_tapClock.nsecsElapsed()) / 1e9, m_playhead ? m_playhead() : -1.0);
}

void LightTriggersPanel::tapAt(double seconds, double playheadSeconds)
{
    if (!m_cue) return;
    // TapTempo starts a new count after a 2 s pause; so does the undo step.
    if (!m_hadTap || seconds - m_tapTempo.lastTap() > 2.0 || seconds <= m_tapTempo.lastTap())
        ++m_tapRun;
    m_hadTap = true;
    const double bpm = m_tapTempo.tap(seconds);
    if (bpm <= 0.0) {
        m_gridStatus->setText(tr("Tap…"));
        return;
    }
    auto g = m_cue->beatGrid();
    g.bpm = std::clamp(std::round(bpm * 100.0) / 100.0, 1.0, 300.0);
    if (playheadSeconds >= 0.0) {
        // The tap is on a beat: the first beat is that one, wound back by
        // whole beats to the earliest at or after 0.
        g.firstBeat = std::fmod(playheadSeconds, g.beatLength());
    }
    m_gridStatus->setText(tr("Tapped %1 BPM").arg(g.bpm, 0, 'f', 2));
    commitGrid(g, kTapMergeBase + (m_tapRun & 0xFFF));
}

void LightTriggersPanel::detectTempo()
{
    if (!m_cue || m_detecting) return;
    if (!m_detectSource) {
        m_gridStatus->setText(tr("Detect needs the audio editor"));
        return;
    }
    m_gridStatus->setText(tr("Rendering the song…"));
    std::vector<float> pcm;
    int rate = 0;
    if (!m_detectSource(pcm, rate) || pcm.empty() || rate <= 0) {
        m_gridStatus->setText(tr("Couldn't read the song to detect its tempo"));
        return;
    }
    detectFromPcm(std::move(pcm), rate, 2);
}

void LightTriggersPanel::detectFromPcm(std::vector<float> interleaved, int sampleRate, int channels)
{
    if (!m_cue || m_detecting) return;
    m_detecting = true;
    m_detectBtn->setEnabled(false);
    m_detectBtn->setText(tr("Detecting…"));
    m_gridStatus->setText(tr("Listening for the beat…"));

    QPointer<audio::AudioCue> forCue = m_cue.data();
    auto *watcher = new QFutureWatcher<audio::TempoEstimate>(this);
    connect(watcher, &QFutureWatcher<audio::TempoEstimate>::finished, this, [this, watcher, forCue] {
        const auto est = watcher->result();
        watcher->deleteLater();
        finishDetect(est, forCue.data());
    });
    const int ch = std::max(1, channels);
    watcher->setFuture(QtConcurrent::run([pcm = std::move(interleaved), sampleRate, ch] {
        const size_t frames = pcm.size() / size_t(ch);
        std::vector<float> mono(frames);
        for (size_t i = 0; i < frames; ++i) {
            float s = 0.0f;
            for (int c = 0; c < ch; ++c) s += pcm[i * size_t(ch) + size_t(c)];
            mono[i] = s / float(ch);
        }
        return audio::estimateTempo(mono.data(), mono.size(), sampleRate);
    }));
}

void LightTriggersPanel::finishDetect(const audio::TempoEstimate &est, audio::AudioCue *forCue)
{
    m_detecting = false;
    m_detectBtn->setText(tr("Detect"));
    m_detectBtn->setEnabled(m_cue != nullptr);
    emit tempoDetected(est.bpm, est.firstBeat, est.confidence);
    if (!forCue || forCue != m_cue) {          // the editor moved on to another cue
        m_gridStatus->clear();
        return;
    }
    if (est.bpm <= 0.0) {
        m_gridStatus->setText(tr("Couldn't find a steady beat — try Tap"));
        return;
    }
    auto g = m_cue->beatGrid();
    g.bpm = std::clamp(est.bpm, 1.0, 300.0);
    g.firstBeat = std::max(0.0, est.firstBeat);
    QString msg = tr("Detected %1 BPM").arg(est.bpm, 0, 'f', 2);
    if (est.confidence < 0.35) msg += tr(" — low confidence, try Tap");
    m_gridStatus->setText(msg);
    commitGrid(g, -1);
}

int LightTriggersPanel::fillWithBeats(double from, double to, int every,
                                      const TriggerAction &action, const QString &namePrefix)
{
    if (!m_cue) return 0;
    const auto added = audio::fillWithBeats(m_cue->beatGrid(), from, to, every, action, namePrefix);
    if (added.empty()) return 0;
    // One step; the new row shares a group and ends up selected, ready to
    // edit as one.
    addAndSelect(added, namePrefix.trimmed().isEmpty() ? tr("Beat") : namePrefix.trimmed());
    return int(added.size());
}

void LightTriggersPanel::openFillDialog()
{
    if (!m_cue) return;
    std::optional<std::pair<double, double>> sel;
    if (const int i = indexOf(m_selectedId); i >= 0) {
        const auto &t = m_cue->lightTriggers()[size_t(i)];
        if (t.isRange()) sel = std::make_pair(t.start, t.end);
    }
    const double song = std::max(m_songLength, [this] {
        double last = 0.0;      // no length known yet: at least cover the markers
        for (const auto &t : m_cue->lightTriggers()) last = std::max(last, t.isRange() ? t.end : t.start);
        return last;
    }());
    FillBeatsDialog dlg(m_cue->beatGrid(), m_cursor, song, sel, this);
    dlg.setWorkspace(m_workspace);
    if (m_midiPorts) dlg.setMidiPortsProvider(m_midiPorts);
    connect(&dlg, &FillBeatsDialog::testRequested, this, &LightTriggersPanel::testRequested);
    if (dlg.exec() != QDialog::Accepted || !m_cue) return;
    const auto added = dlg.triggers();
    if (added.empty()) return;
    addAndSelect(added, dlg.namePrefix());
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
