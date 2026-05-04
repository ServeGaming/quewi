#include "ui/Inspector.h"

#include "core/UndoCommands.h"
#include "core/Workspace.h"
#include "cues/Cue.h"
#include "osc/OscCue.h"

#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QSpinBox>
#include <QUndoStack>
#include <QVBoxLayout>

namespace quewi::ui {

Inspector::Inspector(QWidget *parent)
    : QWidget(parent)
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(8, 8, 8, 8);

    m_typeLabel = new QLabel(tr("No cue selected"), this);
    auto font = m_typeLabel->font();
    font.setBold(true);
    font.setPointSizeF(font.pointSizeF() + 1.0);
    m_typeLabel->setFont(font);
    outer->addWidget(m_typeLabel);

    auto *form = new QFormLayout();
    form->setContentsMargins(0, 8, 0, 0);

    m_number = new QDoubleSpinBox(this);
    m_number->setDecimals(2);
    m_number->setRange(0.0, 99999.0);
    m_number->setSingleStep(1.0);
    form->addRow(tr("Number"), m_number);

    m_name = new QLineEdit(this);
    form->addRow(tr("Name"), m_name);

    m_preWait = new QDoubleSpinBox(this);
    m_preWait->setDecimals(2);
    m_preWait->setRange(0.0, 86400.0);
    m_preWait->setSuffix(QStringLiteral(" s"));
    form->addRow(tr("Pre-wait"), m_preWait);

    m_postWait = new QDoubleSpinBox(this);
    m_postWait->setDecimals(2);
    m_postWait->setRange(0.0, 86400.0);
    m_postWait->setSuffix(QStringLiteral(" s"));
    form->addRow(tr("Post-wait"), m_postWait);

    m_notes = new QPlainTextEdit(this);
    m_notes->setMaximumBlockCount(0);
    form->addRow(tr("Notes"), m_notes);

    outer->addLayout(form);

    // OSC-specific group, hidden unless the selected cue is an OscCue.
    m_oscGroup = new QGroupBox(tr("OSC"), this);
    auto *oscForm = new QFormLayout(m_oscGroup);
    m_oscAddress = new QLineEdit(m_oscGroup);
    m_oscAddress->setPlaceholderText(QStringLiteral("/eos/cue/3.5/fire"));
    oscForm->addRow(tr("Address"), m_oscAddress);

    m_oscHost = new QLineEdit(m_oscGroup);
    m_oscHost->setPlaceholderText(QStringLiteral("127.0.0.1"));
    oscForm->addRow(tr("Host"), m_oscHost);

    m_oscPort = new QSpinBox(m_oscGroup);
    m_oscPort->setRange(1, 65535);
    m_oscPort->setValue(53000);
    oscForm->addRow(tr("Port"), m_oscPort);

    m_oscArgs = new QLineEdit(m_oscGroup);
    m_oscArgs->setPlaceholderText(QStringLiteral("42, 0.5, hello"));
    oscForm->addRow(tr("Arguments"), m_oscArgs);

    outer->addWidget(m_oscGroup);
    outer->addStretch(1);

    connect(m_number,   &QDoubleSpinBox::editingFinished, this, &Inspector::commitNumber);
    connect(m_name,     &QLineEdit::editingFinished,      this, &Inspector::commitName);
    connect(m_preWait,  &QDoubleSpinBox::editingFinished, this, &Inspector::commitPreWait);
    connect(m_postWait, &QDoubleSpinBox::editingFinished, this, &Inspector::commitPostWait);
    connect(m_notes,    &QPlainTextEdit::textChanged,     this, &Inspector::commitNotes);

    connect(m_oscAddress, &QLineEdit::editingFinished,    this, &Inspector::commitOscAddress);
    connect(m_oscHost,    &QLineEdit::editingFinished,    this, &Inspector::commitOscHost);
    connect(m_oscPort,    &QSpinBox::editingFinished,     this, &Inspector::commitOscPort);
    connect(m_oscArgs,    &QLineEdit::editingFinished,    this, &Inspector::commitOscArgs);

    setCue(nullptr);
}

Inspector::~Inspector() = default;

void Inspector::setWorkspace(core::Workspace *workspace) { m_workspace = workspace; }

void Inspector::setCue(cues::Cue *cue)
{
    if (m_cue) disconnect(m_cue, nullptr, this, nullptr);
    m_cue = cue;
    if (m_cue) connect(m_cue, &cues::Cue::changed, this, &Inspector::onCueChanged);
    rebuild();
}

void Inspector::onCueChanged()
{
    if (!m_loading) rebuild();
}

void Inspector::rebuild()
{
    m_loading = true;
    const bool has = m_cue != nullptr;
    m_number->setEnabled(has);
    m_name->setEnabled(has);
    m_preWait->setEnabled(has);
    m_postWait->setEnabled(has);
    m_notes->setEnabled(has);

    if (!has) {
        m_typeLabel->setText(tr("No cue selected"));
        m_number->setValue(0.0);
        m_name->clear();
        m_preWait->setValue(0.0);
        m_postWait->setValue(0.0);
        m_notes->clear();
        m_oscGroup->setVisible(false);
    } else {
        m_typeLabel->setText(m_cue->typeName());
        m_number->setValue(m_cue->number());
        m_name->setText(m_cue->name());
        m_preWait->setValue(m_cue->preWait());
        m_postWait->setValue(m_cue->postWait());
        if (m_notes->toPlainText() != m_cue->notes())
            m_notes->setPlainText(m_cue->notes());

        auto *osc = qobject_cast<osc::OscCue *>(m_cue.data());
        m_oscGroup->setVisible(osc != nullptr);
        if (osc) {
            m_oscAddress->setText(osc->field(QStringLiteral("address")).toString());
            m_oscHost->setText(   osc->field(QStringLiteral("host")).toString());
            m_oscPort->setValue(  osc->field(QStringLiteral("port")).toInt());
            m_oscArgs->setText(   osc->field(QStringLiteral("rawArgs")).toString());
        }
    }
    m_loading = false;
}

void Inspector::pushFieldEdit(const QString &field, const QVariant &newValue)
{
    if (!m_workspace || !m_cue) return;
    const QVariant old = m_cue->field(field);
    if (old == newValue) return;
    m_workspace->undoStack()->push(
        new core::EditCueFieldCommand(m_cue, field, old, newValue));
}

void Inspector::commitNumber()      { if (!m_loading) pushFieldEdit(QStringLiteral("number"),    m_number->value()); }
void Inspector::commitName()        { if (!m_loading) pushFieldEdit(QStringLiteral("name"),      m_name->text()); }
void Inspector::commitPreWait()     { if (!m_loading) pushFieldEdit(QStringLiteral("preWait"),   m_preWait->value()); }
void Inspector::commitPostWait()    { if (!m_loading) pushFieldEdit(QStringLiteral("postWait"),  m_postWait->value()); }

void Inspector::commitNotes()
{
    if (m_loading) return;
    pushFieldEdit(QStringLiteral("notes"), m_notes->toPlainText());
}

void Inspector::commitOscAddress() { if (!m_loading) pushFieldEdit(QStringLiteral("address"),  m_oscAddress->text()); }
void Inspector::commitOscHost()    { if (!m_loading) pushFieldEdit(QStringLiteral("host"),     m_oscHost->text()); }
void Inspector::commitOscPort()    { if (!m_loading) pushFieldEdit(QStringLiteral("port"),     m_oscPort->value()); }
void Inspector::commitOscArgs()    { if (!m_loading) pushFieldEdit(QStringLiteral("rawArgs"),  m_oscArgs->text()); }

} // namespace quewi::ui
