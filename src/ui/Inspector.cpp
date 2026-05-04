#include "ui/Inspector.h"

#include "audio/AudioCue.h"
#include "audio/AudioFile.h"
#include "core/CueList.h"
#include "core/UndoCommands.h"
#include "core/Workspace.h"
#include "cues/Cue.h"
#include "cues/FadeCue.h"
#include "osc/OscCue.h"
#include "ui/WaveformWidget.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QUndoStack>
#include <QVBoxLayout>

namespace quewi::ui {

Inspector::Inspector(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("inspectorPane"));

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(20, 16, 20, 16);
    outer->setSpacing(12);

    m_typeLabel = new QLabel(tr("No cue selected"), this);
    m_typeLabel->setObjectName(QStringLiteral("typeLabel"));
    outer->addWidget(m_typeLabel);

    auto *form = new QFormLayout();
    form->setContentsMargins(0, 4, 0, 0);
    form->setHorizontalSpacing(12);
    form->setVerticalSpacing(8);
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

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

    // ---------------- OSC group ----------------
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

    // ---------------- Audio group ----------------
    m_audioGroup = new QGroupBox(tr("Audio"), this);
    auto *audioOuter = new QVBoxLayout(m_audioGroup);

    auto *fileRow = new QHBoxLayout();
    m_audioPath = new QLineEdit(m_audioGroup);
    m_audioPath->setPlaceholderText(tr("(no file)"));
    m_audioPath->setReadOnly(true);
    m_audioBrowse = new QPushButton(tr("Browse…"), m_audioGroup);
    fileRow->addWidget(m_audioPath, 1);
    fileRow->addWidget(m_audioBrowse);
    audioOuter->addLayout(fileRow);

    m_audioWaveform = new WaveformWidget(m_audioGroup);
    m_audioWaveform->setMinimumHeight(96);
    audioOuter->addWidget(m_audioWaveform);

    m_audioMeta = new QLabel(QStringLiteral(" "), m_audioGroup);
    audioOuter->addWidget(m_audioMeta);

    auto *audioForm = new QFormLayout();
    audioForm->setHorizontalSpacing(12);
    audioForm->setVerticalSpacing(8);
    audioForm->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

    m_audioGain = new QDoubleSpinBox(m_audioGroup);
    m_audioGain->setDecimals(2);
    m_audioGain->setRange(-90.0, 12.0);
    m_audioGain->setSingleStep(0.5);
    m_audioGain->setSuffix(QStringLiteral(" dB"));
    audioForm->addRow(tr("Gain"), m_audioGain);

    m_audioFadeIn = new QDoubleSpinBox(m_audioGroup);
    m_audioFadeIn->setDecimals(2);
    m_audioFadeIn->setRange(0.0, 600.0);
    m_audioFadeIn->setSuffix(QStringLiteral(" s"));
    audioForm->addRow(tr("Fade in"), m_audioFadeIn);

    m_audioFadeOut = new QDoubleSpinBox(m_audioGroup);
    m_audioFadeOut->setDecimals(2);
    m_audioFadeOut->setRange(0.0, 600.0);
    m_audioFadeOut->setSuffix(QStringLiteral(" s"));
    audioForm->addRow(tr("Fade out"), m_audioFadeOut);

    m_audioLoop = new QCheckBox(tr("Loop"), m_audioGroup);
    audioForm->addRow(QString(), m_audioLoop);

    audioOuter->addLayout(audioForm);
    outer->addWidget(m_audioGroup);

    // ---------------- Fade group ----------------
    m_fadeGroup = new QGroupBox(tr("Fade"), this);
    auto *fadeForm = new QFormLayout(m_fadeGroup);

    m_fadeTarget = new QComboBox(m_fadeGroup);
    fadeForm->addRow(tr("Target"), m_fadeTarget);

    m_fadeParam = new QComboBox(m_fadeGroup);
    m_fadeParam->addItem(tr("Gain (dB)"), QStringLiteral("gainDb"));
    fadeForm->addRow(tr("Parameter"), m_fadeParam);

    m_fadeValue = new QDoubleSpinBox(m_fadeGroup);
    m_fadeValue->setDecimals(2);
    m_fadeValue->setRange(-90.0, 12.0);
    m_fadeValue->setSingleStep(0.5);
    m_fadeValue->setSuffix(QStringLiteral(" dB"));
    fadeForm->addRow(tr("Target value"), m_fadeValue);

    m_fadeDuration = new QDoubleSpinBox(m_fadeGroup);
    m_fadeDuration->setDecimals(2);
    m_fadeDuration->setRange(0.0, 600.0);
    m_fadeDuration->setValue(3.0);
    m_fadeDuration->setSuffix(QStringLiteral(" s"));
    fadeForm->addRow(tr("Duration"), m_fadeDuration);

    outer->addWidget(m_fadeGroup);
    outer->addStretch(1);

    // ---------------- Connect ----------------
    connect(m_number,   &QDoubleSpinBox::editingFinished, this, &Inspector::commitNumber);
    connect(m_name,     &QLineEdit::editingFinished,      this, &Inspector::commitName);
    connect(m_preWait,  &QDoubleSpinBox::editingFinished, this, &Inspector::commitPreWait);
    connect(m_postWait, &QDoubleSpinBox::editingFinished, this, &Inspector::commitPostWait);
    connect(m_notes,    &QPlainTextEdit::textChanged,     this, &Inspector::commitNotes);

    connect(m_oscAddress, &QLineEdit::editingFinished,    this, &Inspector::commitOscAddress);
    connect(m_oscHost,    &QLineEdit::editingFinished,    this, &Inspector::commitOscHost);
    connect(m_oscPort,    &QSpinBox::editingFinished,     this, &Inspector::commitOscPort);
    connect(m_oscArgs,    &QLineEdit::editingFinished,    this, &Inspector::commitOscArgs);

    connect(m_audioBrowse,  &QPushButton::clicked,            this, &Inspector::browseAudioFile);
    connect(m_audioGain,    &QDoubleSpinBox::editingFinished, this, &Inspector::commitAudioGain);
    connect(m_audioFadeIn,  &QDoubleSpinBox::editingFinished, this, &Inspector::commitAudioFadeIn);
    connect(m_audioFadeOut, &QDoubleSpinBox::editingFinished, this, &Inspector::commitAudioFadeOut);
    connect(m_audioLoop,    &QCheckBox::toggled,              this, &Inspector::commitAudioLoop);

    connect(m_fadeTarget, &QComboBox::currentIndexChanged,  this, [this](int){ commitFadeTarget(); });
    connect(m_fadeParam,  &QComboBox::currentIndexChanged,  this, [this](int){ commitFadeParameter(); });
    connect(m_fadeValue,  &QDoubleSpinBox::editingFinished, this, &Inspector::commitFadeTargetValue);
    connect(m_fadeDuration, &QDoubleSpinBox::editingFinished, this, &Inspector::commitFadeDuration);

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

    QWidget *commonWidgets[] = { m_number, m_name, m_preWait, m_postWait, m_notes };
    for (QWidget *w : commonWidgets) w->setEnabled(has);

    if (!has) {
        m_typeLabel->setText(tr("No cue selected"));
        m_number->setValue(0.0);
        m_name->clear();
        m_preWait->setValue(0.0);
        m_postWait->setValue(0.0);
        m_notes->clear();
        m_oscGroup->setVisible(false);
        m_audioGroup->setVisible(false);
        m_fadeGroup->setVisible(false);
        m_loading = false;
        return;
    }

    m_typeLabel->setText(m_cue->typeName());
    m_number->setValue(m_cue->number());
    m_name->setText(m_cue->name());
    m_preWait->setValue(m_cue->preWait());
    m_postWait->setValue(m_cue->postWait());
    if (m_notes->toPlainText() != m_cue->notes())
        m_notes->setPlainText(m_cue->notes());

    auto *oscCue   = qobject_cast<osc::OscCue *>(m_cue.data());
    auto *audioCue = qobject_cast<audio::AudioCue *>(m_cue.data());
    auto *fadeCue  = qobject_cast<cues::FadeCue *>(m_cue.data());

    m_oscGroup->setVisible(oscCue != nullptr);
    m_audioGroup->setVisible(audioCue != nullptr);
    m_fadeGroup->setVisible(fadeCue != nullptr);

    if (oscCue) {
        m_oscAddress->setText(oscCue->field(QStringLiteral("address")).toString());
        m_oscHost->setText(   oscCue->field(QStringLiteral("host")).toString());
        m_oscPort->setValue(  oscCue->field(QStringLiteral("port")).toInt());
        m_oscArgs->setText(   oscCue->field(QStringLiteral("rawArgs")).toString());
    }

    if (audioCue) {
        m_audioPath->setText(audioCue->filePath());
        m_audioGain->setValue(audioCue->gainDb());
        m_audioFadeIn->setValue(audioCue->fadeInSeconds());
        m_audioFadeOut->setValue(audioCue->fadeOutSeconds());
        m_audioLoop->setChecked(audioCue->loop());
        m_audioWaveform->setAudioFile(audioCue->audioFile());
        if (auto file = audioCue->audioFile(); file && file->state() == audio::AudioFile::State::Loaded) {
            m_audioMeta->setText(tr("%1 Hz · %2 ch · %3 s")
                .arg(file->sampleRate())
                .arg(file->channelCount())
                .arg(QString::number(file->durationSeconds(), 'f', 2)));
        } else {
            m_audioMeta->setText(QStringLiteral(" "));
        }
    }

    if (fadeCue) {
        rebuildFadeTargets();
        const auto targetId = fadeCue->targetId();
        for (int i = 0; i < m_fadeTarget->count(); ++i) {
            if (m_fadeTarget->itemData(i).toUuid() == targetId) {
                m_fadeTarget->setCurrentIndex(i);
                break;
            }
        }
        for (int i = 0; i < m_fadeParam->count(); ++i) {
            if (m_fadeParam->itemData(i).toString() == fadeCue->parameter()) {
                m_fadeParam->setCurrentIndex(i);
                break;
            }
        }
        m_fadeValue->setValue(fadeCue->targetValue());
        m_fadeDuration->setValue(fadeCue->durationSeconds());
    }

    m_loading = false;
}

void Inspector::rebuildFadeTargets()
{
    m_fadeTarget->clear();
    m_fadeTarget->addItem(tr("(no target)"), QVariant::fromValue(QUuid()));
    if (!m_workspace) return;
    auto *list = m_workspace->activeCueList();
    if (!list) return;
    for (int row = 0; row < list->cueCount(); ++row) {
        auto *c = list->cueAt(row);
        if (!c || c == m_cue) continue;
        // For Phase 3 the only valid target is an audio cue.
        if (qobject_cast<audio::AudioCue *>(c) == nullptr) continue;
        m_fadeTarget->addItem(
            QStringLiteral("%1  %2")
                .arg(QString::number(c->number(), 'f', 2),
                     c->name().isEmpty() ? c->typeName() : c->name()),
            QVariant::fromValue(c->id()));
    }
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

void Inspector::browseAudioFile()
{
    if (!m_cue) return;
    auto *audioCue = qobject_cast<audio::AudioCue *>(m_cue.data());
    if (!audioCue) return;

    const auto path = QFileDialog::getOpenFileName(this, tr("Pick audio file"),
        QFileInfo(audioCue->filePath()).absolutePath(),
        tr("Audio files (*.wav *.mp3 *.flac *.aiff *.aif *.ogg *.m4a);;All files (*.*)"));
    if (path.isEmpty()) return;

    m_audioPath->setText(path);
    pushFieldEdit(QStringLiteral("filePath"), path);
    audioCue->prepare();
}

void Inspector::commitAudioGain()    { if (!m_loading) pushFieldEdit(QStringLiteral("gainDb"),         m_audioGain->value()); }
void Inspector::commitAudioFadeIn()  { if (!m_loading) pushFieldEdit(QStringLiteral("fadeInSeconds"),  m_audioFadeIn->value()); }
void Inspector::commitAudioFadeOut() { if (!m_loading) pushFieldEdit(QStringLiteral("fadeOutSeconds"), m_audioFadeOut->value()); }
void Inspector::commitAudioLoop()    { if (!m_loading) pushFieldEdit(QStringLiteral("loop"),           m_audioLoop->isChecked()); }

void Inspector::commitFadeTarget()
{
    if (m_loading) return;
    const auto id = m_fadeTarget->currentData().toUuid();
    pushFieldEdit(QStringLiteral("targetId"), id);
}

void Inspector::commitFadeParameter()
{
    if (m_loading) return;
    pushFieldEdit(QStringLiteral("parameter"), m_fadeParam->currentData().toString());
}

void Inspector::commitFadeTargetValue()
{
    if (!m_loading) pushFieldEdit(QStringLiteral("targetValue"), m_fadeValue->value());
}

void Inspector::commitFadeDuration()
{
    if (!m_loading) pushFieldEdit(QStringLiteral("durationSeconds"), m_fadeDuration->value());
}

} // namespace quewi::ui
