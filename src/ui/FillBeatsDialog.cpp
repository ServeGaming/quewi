#include "ui/FillBeatsDialog.h"

#include "ui/LightTriggersPanel.h"
#include "ui/Theme.h"

#include <QButtonGroup>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>

namespace quewi::ui {

using audio::TriggerAction;

FillBeatsDialog::FillBeatsDialog(const audio::BeatGrid &grid, double cursorSeconds,
                                 double songSeconds,
                                 std::optional<std::pair<double, double>> selected,
                                 QWidget *parent)
    : QDialog(parent)
    , m_grid(grid)
    , m_cursor(std::max(0.0, cursorSeconds))
    , m_song(std::max(0.0, songSeconds))
    , m_selected(selected)
{
    setWindowTitle(tr("Fill with Beats"));
    setObjectName(QStringLiteral("ltFillDialog"));
    const auto &tk = Theme::tokens();
    setStyleSheet(QStringLiteral(
        "QFrame#ltCard { background:%1; border:none; border-radius:4px; }")
        .arg(tk.bgPanel.name()));

    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(16, 14, 16, 14);
    v->setSpacing(10);

    m_hint = new QLabel(tr("Set the tempo first — type a BPM, Tap, or Detect."), this);
    m_hint->setObjectName(QStringLiteral("ltFillHint"));
    m_hint->setWordWrap(true);
    m_hint->setStyleSheet(QStringLiteral("color:%1; font-weight:600;").arg(tk.warn.name()));
    m_hint->setVisible(!m_grid.isSet());
    v->addWidget(m_hint);

    auto *form = new QFormLayout();
    form->setHorizontalSpacing(10);
    form->setVerticalSpacing(8);

    // Range
    auto *rangeBox = new QVBoxLayout();
    rangeBox->setSpacing(4);
    m_selRange = new QRadioButton(tr("Selected range"), this);
    m_selRange->setObjectName(QStringLiteral("ltFillSelected"));
    if (m_selected)
        m_selRange->setText(tr("Selected range (%1 – %2 s)")
                                .arg(m_selected->first, 0, 'f', 2)
                                .arg(m_selected->second, 0, 'f', 2));
    else
        m_selRange->setToolTip(tr("Select a range trigger first"));
    m_selRange->setEnabled(m_selected.has_value());
    m_fromCursor = new QRadioButton(tr("From cursor for"), this);
    m_fromCursor->setObjectName(QStringLiteral("ltFillCursor"));
    m_bars = new QSpinBox(this);
    m_bars->setObjectName(QStringLiteral("ltFillBars"));
    m_bars->setRange(1, 999);
    m_bars->setValue(4);
    m_bars->setSuffix(tr(" bars"));
    auto *cursorRow = new QHBoxLayout();
    cursorRow->setSpacing(6);
    cursorRow->addWidget(m_fromCursor);
    cursorRow->addWidget(m_bars);
    cursorRow->addStretch(1);
    m_whole = new QRadioButton(tr("Whole song"), this);
    m_whole->setObjectName(QStringLiteral("ltFillWhole"));
    auto *group = new QButtonGroup(this);
    for (auto *b : {m_selRange, m_fromCursor, m_whole}) group->addButton(b);
    rangeBox->addWidget(m_selRange);
    rangeBox->addLayout(cursorRow);
    rangeBox->addWidget(m_whole);
    (m_selected ? m_selRange : m_fromCursor)->setChecked(true);
    form->addRow(tr("Range"), rangeBox);

    // Every
    m_every = new QComboBox(this);
    m_every->setObjectName(QStringLiteral("ltFillEvery"));
    m_every->addItem(tr("Beat"), 1);
    m_every->addItem(tr("2 beats"), 2);
    const int bpb = std::max(1, m_grid.beatsPerBar);
    m_every->addItem(bpb == 1 ? tr("Bar (1 beat)") : tr("Bar (%1 beats)").arg(bpb), bpb);
    form->addRow(tr("Every"), m_every);

    m_prefix = new QLineEdit(tr("Beat"), this);
    m_prefix->setObjectName(QStringLiteral("ltFillPrefix"));
    m_prefix->setToolTip(tr("They're named \"Beat 1\", \"Beat 2\"…"));
    form->addRow(tr("Name"), m_prefix);
    v->addLayout(form);

    // Action: simple mode, Bump sub 1, held for 40% of a beat.
    m_action = new TriggerActionEditor(this);
    m_action->setTitle(tr("EACH ONE SENDS"));
    m_action->refreshDesk();
    TriggerAction a;
    a.kind = TriggerAction::Kind::Desk;
    a.deskDo = TriggerAction::DeskDo::SubBump;
    a.number = QStringLiteral("1");
    a.hold = m_grid.isSet() ? std::max(0.05, 0.4 * m_grid.beatLength()) : 0.2;
    m_action->setAction(a);
    v->addWidget(m_action);

    m_count = new QLabel(this);
    m_count->setObjectName(QStringLiteral("ltFillCount"));
    m_count->setStyleSheet(QStringLiteral("color:%1; font-weight:600;").arg(tk.ink60.name()));

    m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_buttons->button(QDialogButtonBox::Ok)->setText(tr("Add"));
    auto *bottom = new QHBoxLayout();
    bottom->addWidget(m_count, 1);
    bottom->addWidget(m_buttons);
    v->addLayout(bottom);

    connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(group, &QButtonGroup::buttonToggled, this, [this] { updateCount(); });
    connect(m_bars, &QSpinBox::valueChanged, this, [this] {
        if (!m_fromCursor->isChecked()) m_fromCursor->setChecked(true);
        updateCount();
    });
    connect(m_every, &QComboBox::currentIndexChanged, this, [this] { updateCount(); });
    connect(m_action, &TriggerActionEditor::edited, this, [this] { updateCount(); });
    connect(m_action, &TriggerActionEditor::testRequested, this, &FillBeatsDialog::testRequested);
    updateCount();
}

void FillBeatsDialog::setWorkspace(core::Workspace *ws) { m_action->setWorkspace(ws); }

void FillBeatsDialog::setMidiPortsProvider(std::function<QStringList()> f)
{
    m_action->setMidiPortsProvider(std::move(f));
}

void FillBeatsDialog::setRange(Range r)
{
    switch (r) {
    case Range::Selected:   if (m_selected) m_selRange->setChecked(true); break;
    case Range::FromCursor: m_fromCursor->setChecked(true); break;
    case Range::WholeSong:  m_whole->setChecked(true); break;
    }
    updateCount();
}

FillBeatsDialog::Range FillBeatsDialog::range() const
{
    if (m_selRange->isChecked()) return Range::Selected;
    if (m_whole->isChecked()) return Range::WholeSong;
    return Range::FromCursor;
}

void FillBeatsDialog::setBars(int bars) { m_bars->setValue(bars); }

void FillBeatsDialog::setEvery(int beats)
{
    const int i = m_every->findData(beats);
    if (i >= 0) m_every->setCurrentIndex(i);
}

int FillBeatsDialog::every() const { return std::max(1, m_every->currentData().toInt()); }

void FillBeatsDialog::setAction(const TriggerAction &a)
{
    m_action->setAction(a);
    updateCount();
}

TriggerAction FillBeatsDialog::action() const { return m_action->action(); }

void FillBeatsDialog::setNamePrefix(const QString &p) { m_prefix->setText(p); }

double FillBeatsDialog::from() const
{
    switch (range()) {
    case Range::Selected:  return m_selected ? m_selected->first : 0.0;
    case Range::WholeSong: return 0.0;
    case Range::FromCursor:
        // From the beat at the cursor, so a cursor a hair late still starts there.
        return m_grid.isSet() ? m_grid.snap(m_cursor) : m_cursor;
    }
    return 0.0;
}

double FillBeatsDialog::to() const
{
    switch (range()) {
    case Range::Selected:  return m_selected ? m_selected->second : 0.0;
    case Range::WholeSong: return m_song;
    case Range::FromCursor:
        return from() + m_bars->value() * std::max(1, m_grid.beatsPerBar) * m_grid.beatLength();
    }
    return 0.0;
}

audio::LightTriggers FillBeatsDialog::triggers() const
{
    if (!m_grid.isSet()) return {};
    QString prefix = m_prefix->text().trimmed();
    if (prefix.isEmpty()) prefix = tr("Beat");
    return audio::fillWithBeats(m_grid, from(), to(), every(), action(), prefix);
}

int FillBeatsDialog::count() const
{
    return m_grid.isSet() ? int(m_grid.beatsIn(from(), to(), every()).size()) : 0;
}

void FillBeatsDialog::updateCount()
{
    const int n = count();
    using Do = TriggerAction::DeskDo;
    const auto a = action();
    const bool bumps = a.kind == TriggerAction::Kind::Desk
                    && (a.deskDo == Do::SubBump || a.deskDo == Do::FaderBump);
    if (!m_grid.isSet())
        m_count->setText(QString());
    else if (n == 0)
        m_count->setText(tr("No beats in that range"));
    else
        m_count->setText(bumps ? (n == 1 ? tr("Adds 1 bump") : tr("Adds %1 bumps").arg(n))
                               : (n == 1 ? tr("Adds 1 trigger") : tr("Adds %1 triggers").arg(n)));
    m_buttons->button(QDialogButtonBox::Ok)->setEnabled(m_grid.isSet() && n > 0);
}

} // namespace quewi::ui
