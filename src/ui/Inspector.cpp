#include "ui/Inspector.h"

#include "audio/AudioCue.h"
#include "audio/AudioEngine.h"
#include "audio/AudioFile.h"
#include "audio/AudioTrajectory.h"
#include "core/CueList.h"
#include "core/UndoCommands.h"
#include "core/Workspace.h"
#include "cues/Cue.h"
#include "cues/FadeCue.h"
#include "cues/GroupCue.h"
#include "cues/TargetingCue.h"
#include "cues/WaitCue.h"
#include "midi/MidiCue.h"
#include "midi/MidiEngine.h"
#include "lighting/LightCue.h"
#include "osc/OscCue.h"
#include "ui/SpeakerPatchDialog.h"
#include "ui/StageView.h"
#include "ui/WaveformWidget.h"
#include "audio/SpeakerPatch.h"
#include "core/PatchManager.h"
#include "video/VideoCue.h"

#include <QAudioDevice>
#include <QButtonGroup>
#include <QColorDialog>
#include <QGuiApplication>
#include <QMediaDevices>
#include <QScreen>
#include <QHeaderView>
#include <QListWidget>
#include <QSlider>
#include <QTableWidget>
#include <QTableWidgetItem>

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
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QUndoStack>
#include <QVBoxLayout>

namespace quewi::ui {

Inspector::Inspector(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("inspectorPane"));

    // Top-level Inspector is a thin shell holding a scroll area so the
    // pane can shrink to any height — important on laptops, in split
    // monitor setups, and when the user resizes the main window down.
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    auto *scroll = new QScrollArea(this);
    scroll->setObjectName(QStringLiteral("inspectorScroll"));
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *content = new QWidget(scroll);
    content->setObjectName(QStringLiteral("inspectorContent"));
    scroll->setWidget(content);
    root->addWidget(scroll);

    auto *outer = new QVBoxLayout(content);
    outer->setContentsMargins(20, 16, 20, 16);
    outer->setSpacing(12);

    // Allow the pane to be squeezed below the natural sizeHint of its
    // children — the scroll area takes over from there.
    setMinimumWidth(280);

    auto *headerRow = new QHBoxLayout();
    headerRow->setContentsMargins(0, 0, 0, 0);
    m_typeLabel = new QLabel(tr("No cue selected"), this);
    m_typeLabel->setObjectName(QStringLiteral("typeLabel"));
    headerRow->addWidget(m_typeLabel, 1);
    m_colorChip = new QPushButton(tr("Color…"), this);
    m_colorChip->setObjectName(QStringLiteral("colorChip"));
    m_colorChip->setFixedWidth(96);
    m_colorClear = new QPushButton(tr("✕"), this);
    m_colorClear->setObjectName(QStringLiteral("colorClear"));
    m_colorClear->setFixedSize(28, 28);
    m_colorClear->setToolTip(tr("Clear color"));
    headerRow->addWidget(m_colorChip);
    headerRow->addWidget(m_colorClear);
    outer->addLayout(headerRow);

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

    m_continueMode = new QComboBox(this);
    m_continueMode->addItem(tr("Don't continue"), 0);
    m_continueMode->addItem(tr("Auto-continue"),  1);
    m_continueMode->addItem(tr("Auto-follow"),    2);
    m_continueMode->setToolTip(tr(
        "Don't continue: stop here.\n"
        "Auto-continue: fire next cue after post-wait.\n"
        "Auto-follow: fire next cue immediately on start."));
    form->addRow(tr("Continue"), m_continueMode);

    m_notes = new QPlainTextEdit(this);
    m_notes->setMaximumBlockCount(0);
    form->addRow(tr("Notes"), m_notes);

    outer->addLayout(form);

    // ---------------- Wait group ----------------
    m_waitGroup = new QGroupBox(tr("Wait"), this);
    {
        auto *f = new QFormLayout(m_waitGroup);
        m_waitDuration = new QDoubleSpinBox(m_waitGroup);
        m_waitDuration->setDecimals(2);
        m_waitDuration->setRange(0.0, 86400.0);
        m_waitDuration->setSuffix(QStringLiteral(" s"));
        f->addRow(tr("Duration"), m_waitDuration);
    }
    outer->addWidget(m_waitGroup);

    // ---------------- Target picker (Start/Stop/Goto) ----------------
    m_targetGroup = new QGroupBox(tr("Target"), this);
    {
        auto *f = new QFormLayout(m_targetGroup);
        m_targetCombo = new QComboBox(m_targetGroup);
        f->addRow(tr("Cue"), m_targetCombo);
    }
    outer->addWidget(m_targetGroup);

    // ---------------- MIDI ----------------
    m_midiGroup = new QGroupBox(tr("MIDI"), this);
    {
        auto *f = new QFormLayout(m_midiGroup);
        m_midiPort = new QComboBox(m_midiGroup);
        m_midiPort->setEditable(true);
        m_midiPort->setInsertPolicy(QComboBox::NoInsert);
        f->addRow(tr("Port"), m_midiPort);
        m_midiBytes = new QLineEdit(m_midiGroup);
        m_midiBytes->setPlaceholderText(QStringLiteral("90 3C 7F  (hex bytes)"));
        f->addRow(tr("Bytes"), m_midiBytes);

        // Builder row: a few common presets that overwrite the bytes
        // field with a working hex sequence. Saves operators from looking
        // up status-byte tables. Channel defaults to 1 — that's almost
        // always what you want, and the user can hand-edit the nibble.
        auto *presetsRow = new QHBoxLayout();
        presetsRow->setContentsMargins(0, 0, 0, 0);
        auto setBytes = [this](const QString &hex) {
            m_midiBytes->setText(hex);
            m_midiBytes->setFocus();
        };
        auto *bNote = new QPushButton(tr("Note On"), m_midiGroup);
        bNote->setToolTip(tr("9N nn vv  · channel 1, middle C, vel 100"));
        connect(bNote, &QPushButton::clicked, this, [setBytes]{
            setBytes(QStringLiteral("90 3C 64"));
        });
        auto *bOff = new QPushButton(tr("Note Off"), m_midiGroup);
        bOff->setToolTip(tr("8N nn vv  · channel 1, middle C, vel 0"));
        connect(bOff, &QPushButton::clicked, this, [setBytes]{
            setBytes(QStringLiteral("80 3C 00"));
        });
        auto *bCc = new QPushButton(tr("CC"), m_midiGroup);
        bCc->setToolTip(tr("BN cc vv  · channel 1, controller 7 (volume), value 64"));
        connect(bCc, &QPushButton::clicked, this, [setBytes]{
            setBytes(QStringLiteral("B0 07 40"));
        });
        auto *bPc = new QPushButton(tr("PC"), m_midiGroup);
        bPc->setToolTip(tr("CN pp  · channel 1, program 0"));
        connect(bPc, &QPushButton::clicked, this, [setBytes]{
            setBytes(QStringLiteral("C0 00"));
        });
        for (auto *b : { bNote, bOff, bCc, bPc }) {
            b->setFlat(true);
            presetsRow->addWidget(b);
        }
        presetsRow->addStretch(1);
        f->addRow(tr("Presets"), presetsRow);
    }
    outer->addWidget(m_midiGroup);

    // ---------------- MSC ----------------
    m_mscGroup = new QGroupBox(tr("MSC"), this);
    {
        auto *f = new QFormLayout(m_mscGroup);
        m_mscPort = new QComboBox(m_mscGroup);
        m_mscPort->setEditable(true);
        m_mscPort->setInsertPolicy(QComboBox::NoInsert);
        f->addRow(tr("Port"), m_mscPort);
        m_mscDeviceId = new QSpinBox(m_mscGroup); m_mscDeviceId->setRange(0, 127);
        m_mscDeviceId->setToolTip(tr("0x7F (127) = all-call"));
        f->addRow(tr("Device ID"), m_mscDeviceId);
        m_mscFormat = new QSpinBox(m_mscGroup); m_mscFormat->setRange(0, 127);
        m_mscFormat->setToolTip(tr("0x10 Lighting · 0x01 Sound · 0x40 Sound general"));
        f->addRow(tr("Format"), m_mscFormat);
        m_mscCommand = new QSpinBox(m_mscGroup); m_mscCommand->setRange(0, 127);
        m_mscCommand->setToolTip(tr("0x01 GO · 0x02 STOP · 0x03 RESUME · 0x05 LOAD"));
        f->addRow(tr("Command"), m_mscCommand);
        m_mscQNumber = new QLineEdit(m_mscGroup);
        m_mscQNumber->setPlaceholderText(QStringLiteral("Q_number e.g. 12"));
        f->addRow(tr("Q number"), m_mscQNumber);
        m_mscQList = new QLineEdit(m_mscGroup);
        m_mscQList->setPlaceholderText(tr("Q_list (optional)"));
        f->addRow(tr("Q list"), m_mscQList);
        m_mscQPath = new QLineEdit(m_mscGroup);
        m_mscQPath->setPlaceholderText(tr("Q_path (optional)"));
        f->addRow(tr("Q path"), m_mscQPath);
    }
    outer->addWidget(m_mscGroup);

    // ---------------- Group cue ----------------
    m_groupGroup = new QGroupBox(tr("Group"), this);
    {
        auto *gv = new QVBoxLayout(m_groupGroup);
        auto *f = new QFormLayout();
        m_groupMode = new QComboBox(m_groupGroup);
        m_groupMode->addItem(tr("Parallel"),     0);
        m_groupMode->addItem(tr("Sequential"),   1);
        m_groupMode->addItem(tr("Start First"),  2);
        m_groupMode->addItem(tr("Start Random"), 3);
        m_groupMode->addItem(tr("Timeline"),     4);
        f->addRow(tr("Mode"), m_groupMode);
        m_groupStepInterval = new QDoubleSpinBox(m_groupGroup);
        m_groupStepInterval->setDecimals(2);
        m_groupStepInterval->setRange(0.0, 600.0);
        m_groupStepInterval->setSuffix(QStringLiteral(" s"));
        f->addRow(tr("Step (sequential)"), m_groupStepInterval);
        gv->addLayout(f);
        m_groupChildren = new QListWidget(m_groupGroup);
        m_groupChildren->setMinimumHeight(120);
        gv->addWidget(m_groupChildren);
        auto *picker = new QHBoxLayout();
        m_groupChildPicker = new QComboBox(m_groupGroup);
        m_groupChildAdd    = new QPushButton(tr("+ Add"), m_groupGroup);
        m_groupChildRemove = new QPushButton(tr("Remove"), m_groupGroup);
        picker->addWidget(m_groupChildPicker, 1);
        picker->addWidget(m_groupChildAdd);
        picker->addWidget(m_groupChildRemove);
        gv->addLayout(picker);
    }
    outer->addWidget(m_groupGroup);

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

    // Waveform + gain fader side-by-side.
    auto *waveRow = new QHBoxLayout();
    waveRow->setSpacing(12);

    auto *waveColumn = new QVBoxLayout();
    waveColumn->setSpacing(6);
    m_audioWaveform = new WaveformWidget(m_audioGroup);
    m_audioWaveform->setMinimumHeight(140);
    waveColumn->addWidget(m_audioWaveform);

    // Mode toggle row: Trim | Fade
    auto *modeRow = new QHBoxLayout();
    m_audioModeTrim = new QPushButton(tr("Trim"), m_audioGroup);
    m_audioModeFade = new QPushButton(tr("Fade"), m_audioGroup);
    m_audioModeTrim->setObjectName(QStringLiteral("modeButton"));
    m_audioModeFade->setObjectName(QStringLiteral("modeButton"));
    m_audioModeTrim->setCheckable(true);
    m_audioModeFade->setCheckable(true);
    auto *modeGroup = new QButtonGroup(this);
    modeGroup->setExclusive(false); // we want "deselect to disable mode"
    modeGroup->addButton(m_audioModeTrim);
    modeGroup->addButton(m_audioModeFade);
    modeRow->addWidget(m_audioModeTrim);
    modeRow->addWidget(m_audioModeFade);
    modeRow->addStretch(1);
    waveColumn->addLayout(modeRow);

    m_audioMeta = new QLabel(QStringLiteral(" "), m_audioGroup);
    m_audioMeta->setObjectName(QStringLiteral("audioMeta"));
    waveColumn->addWidget(m_audioMeta);

    waveRow->addLayout(waveColumn, 1);

    // Vertical gain fader column.
    auto *gainColumn = new QVBoxLayout();
    gainColumn->setSpacing(4);
    auto *gainHeader = new QLabel(tr("Gain"), m_audioGroup);
    gainHeader->setStyleSheet(QStringLiteral(
        "color:#A8AEBA; font-size:10px; font-weight:600; "
        "letter-spacing:0.04em;"));
    gainHeader->setAlignment(Qt::AlignCenter);
    gainColumn->addWidget(gainHeader);

    m_audioGainSlider = new QSlider(Qt::Vertical, m_audioGroup);
    m_audioGainSlider->setRange(-9000, 1200); // centi-dB → -90.00 .. +12.00 dB
    m_audioGainSlider->setTickPosition(QSlider::TicksLeft);
    m_audioGainSlider->setTickInterval(600);  // every 6 dB
    m_audioGainSlider->setMinimumHeight(160);
    gainColumn->addWidget(m_audioGainSlider, 1, Qt::AlignHCenter);

    m_audioGainLabel = new QLabel(QStringLiteral("0.0 dB"), m_audioGroup);
    m_audioGainLabel->setAlignment(Qt::AlignCenter);
    m_audioGainLabel->setStyleSheet(QStringLiteral("font-weight:600;"));
    gainColumn->addWidget(m_audioGainLabel);

    waveRow->addLayout(gainColumn);
    audioOuter->addLayout(waveRow);

    // Numeric trim & fade spinboxes — synced live with the waveform handles.
    auto *audioForm = new QFormLayout();
    audioForm->setHorizontalSpacing(12);
    audioForm->setVerticalSpacing(8);
    audioForm->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

    // Paired in/out fields go on a single row each — same field count
    // as before, half the vertical space, and the "in → out"
    // relationship is visually reinforced.
    auto makeArrowLabel = [&](QWidget *parent) {
        auto *l = new QLabel(QStringLiteral("→"), parent);
        l->setStyleSheet(QStringLiteral("color:#A8AEBA;"));
        l->setAlignment(Qt::AlignCenter);
        l->setFixedWidth(16);
        return l;
    };

    m_audioFadeIn = new QDoubleSpinBox(m_audioGroup);
    m_audioFadeIn->setDecimals(2);
    m_audioFadeIn->setRange(0.0, 600.0);
    m_audioFadeIn->setSuffix(QStringLiteral(" s"));
    m_audioFadeOut = new QDoubleSpinBox(m_audioGroup);
    m_audioFadeOut->setDecimals(2);
    m_audioFadeOut->setRange(0.0, 600.0);
    m_audioFadeOut->setSuffix(QStringLiteral(" s"));
    {
        auto *fadeRow = new QHBoxLayout();
        fadeRow->setContentsMargins(0, 0, 0, 0);
        fadeRow->addWidget(m_audioFadeIn, 1);
        fadeRow->addWidget(makeArrowLabel(m_audioGroup));
        fadeRow->addWidget(m_audioFadeOut, 1);
        audioForm->addRow(tr("Fade"), fadeRow);
    }

    m_audioTrimIn = new QDoubleSpinBox(m_audioGroup);
    m_audioTrimIn->setDecimals(3);
    m_audioTrimIn->setRange(0.0, 86400.0);
    m_audioTrimIn->setSuffix(QStringLiteral(" s"));
    m_audioTrimOut = new QDoubleSpinBox(m_audioGroup);
    m_audioTrimOut->setDecimals(3);
    m_audioTrimOut->setRange(0.0, 86400.0);
    m_audioTrimOut->setSuffix(QStringLiteral(" s"));
    m_audioTrimOut->setSpecialValueText(tr("(end)"));
    {
        auto *trimRow = new QHBoxLayout();
        trimRow->setContentsMargins(0, 0, 0, 0);
        trimRow->addWidget(m_audioTrimIn, 1);
        trimRow->addWidget(makeArrowLabel(m_audioGroup));
        trimRow->addWidget(m_audioTrimOut, 1);
        audioForm->addRow(tr("Trim"), trimRow);
    }

    // Pan: readout sits to the LEFT of the slider so it never reads
    // like a fourth endpoint past R. The slider's tick at the centre
    // (interval=50) carries the L/R orientation visually; explicit L/R
    // bracket labels were redundant and pushed the readout off the
    // logical axis. Readout text stays "Centre"/"L 25%"/"R 50%".
    auto *panRow = new QHBoxLayout();
    m_audioPanLabel = new QLabel(tr("Centre"), m_audioGroup);
    m_audioPanLabel->setMinimumWidth(64);
    m_audioPanLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_audioPanLabel->setStyleSheet(QStringLiteral("color:#A8AEBA;"));
    m_audioPanSlider = new QSlider(Qt::Horizontal, m_audioGroup);
    m_audioPanSlider->setRange(-100, 100); // hundredths
    m_audioPanSlider->setTickPosition(QSlider::TicksBelow);
    m_audioPanSlider->setTickInterval(50);
    panRow->addWidget(m_audioPanLabel);
    panRow->addWidget(m_audioPanSlider, 1);
    audioForm->addRow(tr("Pan"), panRow);

    m_audioLoop = new QCheckBox(tr("Loop"), m_audioGroup);
    audioForm->addRow(QString(), m_audioLoop);

    m_audioOutputDevice = new QComboBox(m_audioGroup);
    m_audioOutputDevice->addItem(tr("(default)"), QByteArray());
    for (const auto &dev : QMediaDevices::audioOutputs()) {
        m_audioOutputDevice->addItem(dev.description(), dev.id());
    }
    audioForm->addRow(tr("Output"), m_audioOutputDevice);

    auto *quickRow = new QHBoxLayout();
    m_audioNormalize = new QPushButton(tr("Normalize"), m_audioGroup);
    m_audioReverse   = new QPushButton(tr("Reverse"),   m_audioGroup);
    quickRow->addWidget(m_audioNormalize);
    quickRow->addWidget(m_audioReverse);
    quickRow->addStretch(1);

    audioOuter->addLayout(audioForm);
    audioOuter->addLayout(quickRow);

    // ---------------- Output matrix (per-output send levels) ------
    // A row of dB sliders, one per output channel of the cue's
    // chosen device. Empty list = passthrough; the engine pads with
    // unity for any channel beyond the matrix length.
    m_outputMatrixGroup = new QGroupBox(tr("Output matrix (post-pan sends)"),
                                         m_audioGroup);
    m_outputMatrixGroup->setCheckable(true);
    m_outputMatrixGroup->setChecked(false);
    m_outputMatrixLayout = new QGridLayout(m_outputMatrixGroup);
    m_outputMatrixLayout->setHorizontalSpacing(8);
    m_outputMatrixLayout->setVerticalSpacing(4);
    audioOuter->addWidget(m_outputMatrixGroup);

    // ---------------- Object Audio (nested inside Audio group) -----
    m_objAudioGroup = new QGroupBox(tr("Object Audio"), m_audioGroup);
    m_objAudioGroup->setCheckable(true);
    m_objAudioGroup->setChecked(false);
    auto *objVBox = new QVBoxLayout(m_objAudioGroup);

    auto *objTopRow = new QHBoxLayout();
    m_objSpeakerPatch = new QComboBox(m_objAudioGroup);
    m_objSpeakerPatch->setMinimumWidth(160);
    m_objSpeakerEdit  = new QPushButton(tr("Edit…"), m_objAudioGroup);
    m_objSpeakerEdit->setToolTip(tr("Open the Speaker Patch editor"));
    objTopRow->addWidget(new QLabel(tr("Speakers:"), m_objAudioGroup));
    objTopRow->addWidget(m_objSpeakerPatch, 1);
    objTopRow->addWidget(m_objSpeakerEdit);
    objVBox->addLayout(objTopRow);

    m_objStageView = new StageView(m_objAudioGroup);
    objVBox->addWidget(m_objStageView, 1);

    auto *objForm = new QFormLayout();
    m_objElevation = new QSlider(Qt::Horizontal, m_objAudioGroup);
    m_objElevation->setRange(-900, 900);     // tenths of a degree
    m_objElevation->setSingleStep(10);
    m_objElevationLabel = new QLabel(QStringLiteral("0°"), m_objAudioGroup);
    auto *elRow = new QHBoxLayout();
    elRow->addWidget(m_objElevation, 1);
    elRow->addWidget(m_objElevationLabel);
    objForm->addRow(tr("Elevation"), elRow);

    m_objSpread = new QSlider(Qt::Horizontal, m_objAudioGroup);
    m_objSpread->setRange(0, 100);
    m_objSpreadLabel = new QLabel(QStringLiteral("0%"), m_objAudioGroup);
    auto *spRow = new QHBoxLayout();
    spRow->addWidget(m_objSpread, 1);
    spRow->addWidget(m_objSpreadLabel);
    objForm->addRow(tr("Spread"), spRow);

    objVBox->addLayout(objForm);

    // ── Trajectory ────────────────────────────────────────────────
    m_trajGroup = new QGroupBox(tr("Trajectory"), m_objAudioGroup);
    auto *trajVBox = new QVBoxLayout(m_trajGroup);

    auto *trajTopRow = new QHBoxLayout();
    trajTopRow->addWidget(new QLabel(tr("Mode:"), m_trajGroup));
    m_trajMode = new QComboBox(m_trajGroup);
    m_trajMode->addItem(tr("One-shot"), QStringLiteral("oneshot"));
    m_trajMode->addItem(tr("Loop"),     QStringLiteral("loop"));
    trajTopRow->addWidget(m_trajMode, 1);
    m_trajAdd    = new QPushButton(tr("Add point"),    m_trajGroup);
    m_trajRemove = new QPushButton(tr("Remove point"), m_trajGroup);
    trajTopRow->addWidget(m_trajAdd);
    trajTopRow->addWidget(m_trajRemove);
    trajVBox->addLayout(trajTopRow);

    m_trajTable = new QTableWidget(0, 4, m_trajGroup);
    m_trajTable->setHorizontalHeaderLabels(
        { tr("Time (s)"), tr("Az °"), tr("El °"), tr("Spread") });
    m_trajTable->horizontalHeader()->setStretchLastSection(true);
    m_trajTable->verticalHeader()->setVisible(false);
    m_trajTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_trajTable->setMinimumHeight(120);
    trajVBox->addWidget(m_trajTable);

    objVBox->addWidget(m_trajGroup);

    audioOuter->addWidget(m_objAudioGroup);

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

    // ---------------- Light group ----------------
    m_lightGroup = new QGroupBox(tr("Light"), this);
    auto *lightOuter = new QVBoxLayout(m_lightGroup);

    auto *uniRow = new QHBoxLayout();
    uniRow->addWidget(new QLabel(tr("Universe"), m_lightGroup));
    m_lightUniverse = new QSpinBox(m_lightGroup);
    m_lightUniverse->setRange(1, 63999);
    uniRow->addWidget(m_lightUniverse);
    uniRow->addStretch(1);
    lightOuter->addLayout(uniRow);

    m_lightTable = new QTableWidget(0, 2, m_lightGroup);
    m_lightTable->setHorizontalHeaderLabels({tr("Channel"), tr("Value")});
    m_lightTable->horizontalHeader()->setStretchLastSection(true);
    m_lightTable->verticalHeader()->setVisible(false);
    m_lightTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_lightTable->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_lightTable->setMinimumHeight(160);
    lightOuter->addWidget(m_lightTable);

    auto *lightBtns = new QHBoxLayout();
    m_lightAdd    = new QPushButton(tr("+ Channel"), m_lightGroup);
    m_lightRemove = new QPushButton(tr("Remove"),    m_lightGroup);
    lightBtns->addWidget(m_lightAdd);
    lightBtns->addWidget(m_lightRemove);
    lightBtns->addStretch(1);
    lightOuter->addLayout(lightBtns);

    outer->addWidget(m_lightGroup);

    // ---------------- Light Fade group ----------------
    m_lightFadeGroup = new QGroupBox(tr("Light Fade"), this);
    auto *lfForm = new QFormLayout(m_lightFadeGroup);
    m_lightFadeTarget = new QComboBox(m_lightFadeGroup);
    lfForm->addRow(tr("Target light cue"), m_lightFadeTarget);
    m_lightFadeDuration = new QDoubleSpinBox(m_lightFadeGroup);
    m_lightFadeDuration->setDecimals(2);
    m_lightFadeDuration->setRange(0.0, 600.0);
    m_lightFadeDuration->setValue(3.0);
    m_lightFadeDuration->setSuffix(QStringLiteral(" s"));
    lfForm->addRow(tr("Duration"), m_lightFadeDuration);
    outer->addWidget(m_lightFadeGroup);

    // ---------------- Visual (video/image/text) group ----------------
    m_visualGroup = new QGroupBox(tr("Visual"), this);
    auto *visualOuter = new QVBoxLayout(m_visualGroup);

    auto *visualFileRow = new QHBoxLayout();
    m_visualPath   = new QLineEdit(m_visualGroup);
    m_visualPath->setReadOnly(true);
    m_visualPath->setPlaceholderText(tr("(no file)"));
    m_visualBrowse = new QPushButton(tr("Browse…"), m_visualGroup);
    visualFileRow->addWidget(m_visualPath, 1);
    visualFileRow->addWidget(m_visualBrowse);
    visualOuter->addLayout(visualFileRow);

    m_textString = new QLineEdit(m_visualGroup);
    m_textString->setPlaceholderText(tr("Text to display"));
    visualOuter->addWidget(m_textString);

    auto *visualForm = new QFormLayout();
    visualForm->setHorizontalSpacing(12);
    visualForm->setVerticalSpacing(8);
    visualForm->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

    m_visualScreen = new QComboBox(m_visualGroup);
    {
        const auto screens = QGuiApplication::screens();
        for (int i = 0; i < screens.size(); ++i) {
            auto *s = screens[i];
            const auto geom = s->geometry();
            m_visualScreen->addItem(
                QStringLiteral("%1 — %2 (%3×%4)")
                    .arg(i)
                    .arg(s->name().isEmpty() ? tr("Display") : s->name())
                    .arg(geom.width()).arg(geom.height()),
                i);
        }
        if (m_visualScreen->count() == 0) {
            m_visualScreen->addItem(tr("0 — (no displays detected)"), 0);
        }
    }
    visualForm->addRow(tr("Screen"), m_visualScreen);

    auto makePctSpinner = [&](QWidget *p) {
        auto *s = new QDoubleSpinBox(p);
        s->setDecimals(3);
        s->setRange(0.0, 1.0);
        s->setSingleStep(0.05);
        return s;
    };
    m_visualX = makePctSpinner(m_visualGroup);
    m_visualY = makePctSpinner(m_visualGroup);
    m_visualW = makePctSpinner(m_visualGroup);
    m_visualH = makePctSpinner(m_visualGroup);
    m_visualW->setValue(1.0);
    m_visualH->setValue(1.0);
    auto *posRow = new QHBoxLayout();
    posRow->addWidget(new QLabel(QStringLiteral("x"), m_visualGroup));
    posRow->addWidget(m_visualX);
    posRow->addWidget(new QLabel(QStringLiteral("y"), m_visualGroup));
    posRow->addWidget(m_visualY);
    auto *posWrap = new QWidget(m_visualGroup);
    posWrap->setLayout(posRow);
    posRow->setContentsMargins(0, 0, 0, 0);
    visualForm->addRow(tr("Position"), posWrap);

    auto *sizeRow = new QHBoxLayout();
    sizeRow->addWidget(new QLabel(QStringLiteral("w"), m_visualGroup));
    sizeRow->addWidget(m_visualW);
    sizeRow->addWidget(new QLabel(QStringLiteral("h"), m_visualGroup));
    sizeRow->addWidget(m_visualH);
    auto *sizeWrap = new QWidget(m_visualGroup);
    sizeWrap->setLayout(sizeRow);
    sizeRow->setContentsMargins(0, 0, 0, 0);
    visualForm->addRow(tr("Size"), sizeWrap);

    m_visualOpacity = new QDoubleSpinBox(m_visualGroup);
    m_visualOpacity->setDecimals(2);
    m_visualOpacity->setRange(0.0, 1.0);
    m_visualOpacity->setSingleStep(0.05);
    m_visualOpacity->setValue(1.0);
    visualForm->addRow(tr("Opacity"), m_visualOpacity);

    m_videoLoop = new QCheckBox(tr("Loop"), m_visualGroup);
    visualForm->addRow(QString(), m_videoLoop);

    m_textSize = new QSpinBox(m_visualGroup);
    m_textSize->setRange(8, 600);
    m_textSize->setSuffix(QStringLiteral(" px"));
    visualForm->addRow(tr("Text size"), m_textSize);

    m_textColorBtn = new QPushButton(tr("Pick text color…"), m_visualGroup);
    visualForm->addRow(QString(), m_textColorBtn);

    visualOuter->addLayout(visualForm);
    outer->addWidget(m_visualGroup);

    outer->addStretch(1);

    // ---------------- Connect ----------------
    connect(m_number,   &QDoubleSpinBox::editingFinished, this, &Inspector::commitNumber);
    connect(m_name,     &QLineEdit::editingFinished,      this, &Inspector::commitName);
    connect(m_preWait,  &QDoubleSpinBox::editingFinished, this, &Inspector::commitPreWait);
    connect(m_postWait, &QDoubleSpinBox::editingFinished, this, &Inspector::commitPostWait);
    connect(m_notes,    &QPlainTextEdit::textChanged,     this, &Inspector::commitNotes);
    connect(m_continueMode, &QComboBox::currentIndexChanged,
            this, [this](int){ commitContinueMode(); });
    connect(m_waitDuration, &QDoubleSpinBox::editingFinished,
            this, &Inspector::commitWaitDuration);
    connect(m_targetCombo, &QComboBox::currentIndexChanged,
            this, [this](int){ commitTargetCue(); });
    connect(m_groupMode, &QComboBox::currentIndexChanged,
            this, [this](int){ commitGroupMode(); });
    connect(m_groupStepInterval, &QDoubleSpinBox::editingFinished,
            this, &Inspector::commitGroupStepInterval);
    connect(m_groupChildAdd,    &QPushButton::clicked, this, &Inspector::addGroupChild);
    connect(m_groupChildRemove, &QPushButton::clicked, this, &Inspector::removeGroupChild);

    connect(m_midiPort,  &QComboBox::editTextChanged, this, [this](const QString &){ commitMidiPort(); });
    connect(m_midiBytes, &QLineEdit::editingFinished, this, &Inspector::commitMidiBytes);
    connect(m_mscPort,   &QComboBox::editTextChanged, this, [this](const QString &){ commitMscPort(); });
    connect(m_mscDeviceId, &QSpinBox::editingFinished, this, &Inspector::commitMscField);
    connect(m_mscFormat,   &QSpinBox::editingFinished, this, &Inspector::commitMscField);
    connect(m_mscCommand,  &QSpinBox::editingFinished, this, &Inspector::commitMscField);
    connect(m_mscQNumber, &QLineEdit::editingFinished, this, &Inspector::commitMscField);
    connect(m_mscQList,   &QLineEdit::editingFinished, this, &Inspector::commitMscField);
    connect(m_mscQPath,   &QLineEdit::editingFinished, this, &Inspector::commitMscField);

    connect(m_oscAddress, &QLineEdit::editingFinished,    this, &Inspector::commitOscAddress);
    connect(m_oscHost,    &QLineEdit::editingFinished,    this, &Inspector::commitOscHost);
    connect(m_oscPort,    &QSpinBox::editingFinished,     this, &Inspector::commitOscPort);
    connect(m_oscArgs,    &QLineEdit::editingFinished,    this, &Inspector::commitOscArgs);

    connect(m_audioBrowse,     &QPushButton::clicked,            this, &Inspector::browseAudioFile);
    connect(m_audioGainSlider, &QSlider::valueChanged,           this, &Inspector::onGainSliderChanged);
    connect(m_audioPanSlider,  &QSlider::valueChanged,           this, &Inspector::onPanSliderChanged);
    connect(m_audioFadeIn,     &QDoubleSpinBox::editingFinished, this, &Inspector::commitAudioFadeIn);
    connect(m_audioFadeOut,    &QDoubleSpinBox::editingFinished, this, &Inspector::commitAudioFadeOut);
    connect(m_audioTrimIn,     &QDoubleSpinBox::editingFinished, this, &Inspector::commitAudioTrimIn);
    connect(m_audioTrimOut,    &QDoubleSpinBox::editingFinished, this, &Inspector::commitAudioTrimOut);
    connect(m_audioLoop,       &QCheckBox::toggled,              this, &Inspector::commitAudioLoop);
    connect(m_audioNormalize,  &QPushButton::clicked,            this, &Inspector::normalizeAudio);
    connect(m_audioReverse,    &QPushButton::clicked,            this, &Inspector::reverseAudio);
    connect(m_audioModeTrim,   &QPushButton::clicked,            this, &Inspector::setAudioModeTrim);
    connect(m_audioModeFade,   &QPushButton::clicked,            this, &Inspector::setAudioModeFade);
    connect(m_audioOutputDevice, &QComboBox::currentIndexChanged,
            this, [this](int){ commitAudioOutputDevice(); rebuildOutputMatrix(); });
    connect(m_outputMatrixGroup, &QGroupBox::toggled,
            this, &Inspector::onOutputMatrixToggled);

    // Object Audio
    connect(m_objAudioGroup, &QGroupBox::toggled,
            this, &Inspector::commitObjectAudioEnabled);
    connect(m_objSpeakerPatch, &QComboBox::currentIndexChanged,
            this, [this](int){ commitSpeakerPatch(); });
    connect(m_objSpeakerEdit, &QPushButton::clicked,
            this, &Inspector::openSpeakerPatchDialog);
    connect(m_objStageView, &StageView::positionChanged,
            this, &Inspector::onStagePositionChanged);
    connect(m_objElevation, &QSlider::valueChanged,
            this, &Inspector::onElevationSliderChanged);
    connect(m_objSpread, &QSlider::valueChanged,
            this, &Inspector::onSpreadSliderChanged);

    connect(m_trajAdd,    &QPushButton::clicked, this, &Inspector::onTrajectoryAdd);
    connect(m_trajRemove, &QPushButton::clicked, this, &Inspector::onTrajectoryRemove);
    connect(m_trajTable,  &QTableWidget::cellChanged,
            this, &Inspector::onTrajectoryCellChanged);
    connect(m_trajMode, &QComboBox::currentIndexChanged,
            this, [this](int){ onTrajectoryModeChanged(); });

    // Waveform handles: drag updates the spinbox value live; release pushes
    // a single undo step via the spinbox's editingFinished commit handlers.
    connect(m_audioWaveform, &WaveformWidget::trimInChanged,  this, [this](double s) {
        if (m_loading) return;
        m_loading = true; m_audioTrimIn->setValue(s); m_loading = false;
    });
    connect(m_audioWaveform, &WaveformWidget::trimOutChanged, this, [this](double s) {
        if (m_loading) return;
        m_loading = true; m_audioTrimOut->setValue(s); m_loading = false;
    });
    connect(m_audioWaveform, &WaveformWidget::fadeInChanged,  this, [this](double s) {
        if (m_loading) return;
        m_loading = true; m_audioFadeIn->setValue(s); m_loading = false;
    });
    connect(m_audioWaveform, &WaveformWidget::fadeOutChanged, this, [this](double s) {
        if (m_loading) return;
        m_loading = true; m_audioFadeOut->setValue(s); m_loading = false;
    });
    connect(m_audioWaveform, &WaveformWidget::editingFinished, this, [this] {
        commitAudioTrimIn();
        commitAudioTrimOut();
        commitAudioFadeIn();
        commitAudioFadeOut();
    });

    connect(m_colorChip,  &QPushButton::clicked, this, &Inspector::pickCueColor);
    connect(m_colorClear, &QPushButton::clicked, this, &Inspector::clearCueColor);

    connect(m_fadeTarget, &QComboBox::currentIndexChanged,  this, [this](int){ commitFadeTarget(); });
    connect(m_fadeParam,  &QComboBox::currentIndexChanged,  this, [this](int){ commitFadeParameter(); });
    connect(m_fadeValue,  &QDoubleSpinBox::editingFinished, this, &Inspector::commitFadeTargetValue);
    connect(m_fadeDuration, &QDoubleSpinBox::editingFinished, this, &Inspector::commitFadeDuration);

    connect(m_lightUniverse, &QSpinBox::editingFinished, this, &Inspector::commitLightUniverse);
    connect(m_lightAdd,      &QPushButton::clicked,      this, &Inspector::addLightChannel);
    connect(m_lightRemove,   &QPushButton::clicked,      this, &Inspector::removeLightChannel);
    connect(m_lightTable,    &QTableWidget::itemChanged, this, [this](QTableWidgetItem *){ commitLightChannels(); });

    connect(m_lightFadeTarget,   &QComboBox::currentIndexChanged,  this, [this](int){ commitLightFadeTarget(); });
    connect(m_lightFadeDuration, &QDoubleSpinBox::editingFinished, this, &Inspector::commitLightFadeDuration);

    connect(m_visualBrowse,  &QPushButton::clicked,            this, &Inspector::browseVisualFile);
    connect(m_visualScreen,  &QComboBox::currentIndexChanged,
            this, [this](int){ commitVisualScreen(); });
    for (auto *s : {m_visualX, m_visualY, m_visualW, m_visualH, m_visualOpacity}) {
        connect(s, &QDoubleSpinBox::editingFinished, this, &Inspector::commitVisualGeometry);
    }
    connect(m_videoLoop,     &QCheckBox::toggled,              this, &Inspector::commitVideoLoop);
    connect(m_textString,    &QLineEdit::editingFinished,      this, &Inspector::commitTextString);
    connect(m_textSize,      &QSpinBox::editingFinished,       this, &Inspector::commitTextSize);
    connect(m_textColorBtn,  &QPushButton::clicked,            this, &Inspector::pickTextColor);

    setCue(nullptr);
}

Inspector::~Inspector() = default;

void Inspector::setWorkspace(core::Workspace *workspace) { m_workspace = workspace; }

void Inspector::setAudioEngine(audio::AudioEngine *engine) { m_audioEngine = engine; }

void Inspector::setMidiEngine(midi::MidiEngine *engine) { m_midiEngine = engine; }

void Inspector::setCue(cues::Cue *cue)
{
    if (m_cue) disconnect(m_cue, nullptr, this, nullptr);
    m_cue = cue;
    if (m_cue) connect(m_cue, &cues::Cue::changed, this, &Inspector::onCueChanged);
    rebuild();
    // Reset scroll on every selection so the cue header is always
    // visible from the top — without this the scroll position is
    // sticky across selections, which read as a "clipped at top"
    // layout glitch when the view was scrolled to expose the audio
    // group on a previous cue.
    if (auto *sa = findChild<QScrollArea *>(QStringLiteral("inspectorScroll"))) {
        if (auto *bar = sa->verticalScrollBar()) bar->setValue(0);
    }
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
        m_continueMode->setCurrentIndex(0);
        m_notes->clear();
        m_oscGroup->setVisible(false);
        m_audioGroup->setVisible(false);
        m_fadeGroup->setVisible(false);
        m_lightGroup->setVisible(false);
        m_lightFadeGroup->setVisible(false);
        m_visualGroup->setVisible(false);
        m_waitGroup->setVisible(false);
        m_targetGroup->setVisible(false);
        m_groupGroup->setVisible(false);
        m_midiGroup->setVisible(false);
        m_mscGroup->setVisible(false);
        m_loading = false;
        return;
    }

    m_typeLabel->setText(m_cue->typeName());
    m_number->setValue(m_cue->number());
    m_name->setText(m_cue->name());
    m_preWait->setValue(m_cue->preWait());
    m_postWait->setValue(m_cue->postWait());
    m_continueMode->setCurrentIndex(static_cast<int>(m_cue->continueMode()));
    if (m_notes->toPlainText() != m_cue->notes())
        m_notes->setPlainText(m_cue->notes());

    auto *waitCue   = qobject_cast<cues::WaitCue *>(m_cue.data());
    auto *targetCue = qobject_cast<cues::TargetingCue *>(m_cue.data());
    auto *groupCue  = qobject_cast<cues::GroupCue *>(m_cue.data());
    auto *midiCue   = qobject_cast<midi::MidiCue *>(m_cue.data());
    auto *mscCue    = qobject_cast<midi::MscCue *>(m_cue.data());
    auto *oscCue   = qobject_cast<osc::OscCue *>(m_cue.data());
    auto *audioCue = qobject_cast<audio::AudioCue *>(m_cue.data());
    auto *fadeCue  = qobject_cast<cues::FadeCue *>(m_cue.data());
    auto *lightCue = qobject_cast<lighting::LightCue *>(m_cue.data());
    auto *lfadeCue = qobject_cast<lighting::LightFadeCue *>(m_cue.data());
    auto *visualCue = qobject_cast<video::VisualCue *>(m_cue.data());
    auto *videoCue  = qobject_cast<video::VideoCue *>(m_cue.data());
    auto *imageCue  = qobject_cast<video::ImageCue *>(m_cue.data());
    auto *textCue   = qobject_cast<video::TextCue  *>(m_cue.data());

    m_oscGroup->setVisible(oscCue != nullptr);
    m_audioGroup->setVisible(audioCue != nullptr);
    m_fadeGroup->setVisible(fadeCue != nullptr);
    m_lightGroup->setVisible(lightCue != nullptr);
    m_lightFadeGroup->setVisible(lfadeCue != nullptr);
    m_visualGroup->setVisible(visualCue != nullptr);
    m_waitGroup->setVisible(waitCue != nullptr);
    m_targetGroup->setVisible(targetCue != nullptr);
    m_groupGroup->setVisible(groupCue != nullptr);
    m_midiGroup->setVisible(midiCue != nullptr);
    m_mscGroup->setVisible(mscCue != nullptr);

    auto fillPorts = [this](QComboBox *combo, const QString &current) {
        const auto sigsBlocked = combo->blockSignals(true);
        combo->clear();
        combo->addItem(QStringLiteral("(default / first)"), QString());
        if (m_midiEngine) {
            for (const auto &name : m_midiEngine->outputPortNames())
                combo->addItem(name, name);
        }
        // If current isn't in the enumeration, still show it.
        if (!current.isEmpty()) {
            int idx = combo->findData(current);
            if (idx < 0) {
                combo->addItem(current + QObject::tr(" (offline)"), current);
                idx = combo->count() - 1;
            }
            combo->setCurrentIndex(idx);
        } else {
            combo->setCurrentIndex(0);
        }
        combo->blockSignals(sigsBlocked);
    };

    if (midiCue) {
        fillPorts(m_midiPort, midiCue->portName());
        m_midiBytes->setText(QString::fromLatin1(midiCue->bytes().toHex(' ')));
    }
    if (mscCue) {
        fillPorts(m_mscPort, mscCue->portName());
        m_mscDeviceId->setValue(mscCue->deviceId());
        m_mscFormat->setValue(mscCue->commandFormat());
        m_mscCommand->setValue(mscCue->command());
        m_mscQNumber->setText(mscCue->qNumber());
        m_mscQList->setText(mscCue->qList());
        m_mscQPath->setText(mscCue->qPath());
    }

    if (groupCue) {
        m_groupMode->setCurrentIndex(static_cast<int>(groupCue->mode()));
        m_groupStepInterval->setValue(groupCue->stepInterval());
        m_groupChildren->clear();
        m_groupChildPicker->clear();
        m_groupChildPicker->addItem(tr("(pick a cue)"), QVariant::fromValue(QUuid()));
        if (m_workspace) {
            if (auto *list = m_workspace->activeCueList()) {
                // List children with their cue line for context.
                for (const auto &id : groupCue->childIds()) {
                    QString label = id.toString();
                    for (int row = 0; row < list->cueCount(); ++row) {
                        if (auto *c = list->cueAt(row); c && c->id() == id) {
                            label = QStringLiteral("%1  %2  [%3]")
                                .arg(QString::number(c->number(), 'f', 2),
                                     c->name().isEmpty() ? c->typeName() : c->name(),
                                     c->typeName());
                            break;
                        }
                    }
                    auto *item = new QListWidgetItem(label, m_groupChildren);
                    item->setData(Qt::UserRole, QVariant::fromValue(id));
                }
                // Fill the picker with all cues except self.
                for (int row = 0; row < list->cueCount(); ++row) {
                    auto *c = list->cueAt(row);
                    if (!c || c == m_cue) continue;
                    m_groupChildPicker->addItem(
                        QStringLiteral("%1  %2  [%3]")
                            .arg(QString::number(c->number(), 'f', 2),
                                 c->name().isEmpty() ? c->typeName() : c->name(),
                                 c->typeName()),
                        QVariant::fromValue(c->id()));
                }
            }
        }
    }

    if (waitCue) {
        m_waitDuration->setValue(waitCue->durationSeconds());
    }
    if (targetCue) {
        m_targetCombo->clear();
        m_targetCombo->addItem(tr("(none)"), QVariant::fromValue(QUuid()));
        if (m_workspace) {
            if (auto *list = m_workspace->activeCueList()) {
                for (int row = 0; row < list->cueCount(); ++row) {
                    auto *c = list->cueAt(row);
                    if (!c || c == m_cue) continue;
                    m_targetCombo->addItem(
                        QStringLiteral("%1  %2  [%3]")
                            .arg(QString::number(c->number(), 'f', 2),
                                 c->name().isEmpty() ? c->typeName() : c->name(),
                                 c->typeName()),
                        QVariant::fromValue(c->id()));
                }
            }
        }
        const auto tid = targetCue->targetId();
        for (int i = 0; i < m_targetCombo->count(); ++i) {
            if (m_targetCombo->itemData(i).toUuid() == tid) {
                m_targetCombo->setCurrentIndex(i);
                break;
            }
        }
    }

    if (visualCue) {
        // File path field is for video/image; hidden for text.
        const bool hasFile = (videoCue != nullptr) || (imageCue != nullptr);
        m_visualPath->setVisible(hasFile);
        m_visualBrowse->setVisible(hasFile);
        m_textString->setVisible(textCue != nullptr);
        m_videoLoop->setVisible(videoCue != nullptr);
        m_textSize->setVisible(textCue != nullptr);
        m_textColorBtn->setVisible(textCue != nullptr);

        if (videoCue) {
            m_visualPath->setText(videoCue->filePath());
            m_videoLoop->setChecked(videoCue->loop());
        } else if (imageCue) {
            m_visualPath->setText(imageCue->filePath());
        } else if (textCue) {
            m_textString->setText(textCue->text());
            m_textSize->setValue(textCue->fontPixelSize());
        }
        {
            const int want = visualCue->screenIndex();
            int row = 0;
            for (int i = 0; i < m_visualScreen->count(); ++i) {
                if (m_visualScreen->itemData(i).toInt() == want) { row = i; break; }
            }
            m_visualScreen->setCurrentIndex(row);
        }
        m_visualX->setValue(visualCue->posX());
        m_visualY->setValue(visualCue->posY());
        m_visualW->setValue(visualCue->posW());
        m_visualH->setValue(visualCue->posH());
        m_visualOpacity->setValue(visualCue->opacity());
    }

    if (oscCue) {
        m_oscAddress->setText(oscCue->field(QStringLiteral("address")).toString());
        m_oscHost->setText(   oscCue->field(QStringLiteral("host")).toString());
        m_oscPort->setValue(  oscCue->field(QStringLiteral("port")).toInt());
        m_oscArgs->setText(   oscCue->field(QStringLiteral("rawArgs")).toString());
    }

    if (audioCue) {
        m_audioPath->setText(audioCue->filePath());
        m_audioGainSlider->setValue(static_cast<int>(audioCue->gainDb() * 100.0));
        m_audioGainLabel->setText(QStringLiteral("%1 dB")
            .arg(QString::number(audioCue->gainDb(), 'f', 1)));
        m_audioPanSlider->setValue(static_cast<int>(audioCue->pan() * 100.0));
        const double pv = audioCue->pan();
        m_audioPanLabel->setText(qFuzzyIsNull(pv)
            ? tr("Centre")
            : (pv < 0
               ? tr("L %1%").arg(int(std::round(std::abs(pv) * 100.0)))
               : tr("R %1%").arg(int(std::round(pv * 100.0)))));
        m_audioFadeIn->setValue(audioCue->fadeInSeconds());
        m_audioFadeOut->setValue(audioCue->fadeOutSeconds());
        m_audioTrimIn->setValue(audioCue->trimInSeconds());
        m_audioTrimOut->setValue(audioCue->trimOutSeconds());
        m_audioLoop->setChecked(audioCue->loop());
        const QByteArray devId = audioCue->outputDeviceId();
        int devIdx = 0;
        for (int i = 0; i < m_audioOutputDevice->count(); ++i) {
            if (m_audioOutputDevice->itemData(i).toByteArray() == devId) {
                devIdx = i;
                break;
            }
        }
        m_audioOutputDevice->setCurrentIndex(devIdx);
        m_audioWaveform->setAudioFile(audioCue->audioFile());
        m_audioWaveform->setMarkers(audioCue->trimInSeconds(),
                                    audioCue->trimOutSeconds(),
                                    audioCue->fadeInSeconds(),
                                    audioCue->fadeOutSeconds());
        if (auto file = audioCue->audioFile(); file && file->state() == audio::AudioFile::State::Loaded) {
            m_audioMeta->setText(tr("%1 Hz · %2 ch · %3 s")
                .arg(file->sampleRate())
                .arg(file->channelCount())
                .arg(QString::number(file->durationSeconds(), 'f', 2)));
        } else if (auto file = audioCue->audioFile(); file && file->state() == audio::AudioFile::State::Failed) {
            m_audioMeta->setText(tr("Decode failed: %1").arg(file->errorString()));
            m_audioMeta->setStyleSheet(QStringLiteral("color:#FF5A5A;"));
        } else {
            m_audioMeta->setText(QStringLiteral(" "));
            m_audioMeta->setStyleSheet(QString());
        }

        // ── Output matrix reload ─────────────────────────────────────
        rebuildOutputMatrix();

        // ── Object Audio reload ──────────────────────────────────────
        m_objAudioGroup->setChecked(audioCue->objectAudioEnabled());

        m_objSpeakerPatch->clear();
        m_objSpeakerPatch->addItem(tr("(none)"), QUuid());
        int patchIdx = 0;
        if (m_workspace && m_workspace->patches()) {
            const auto patches = m_workspace->patches()->patchesIn(
                core::PatchManager::Category::SpeakerArray);
            for (int i = 0; i < patches.size(); ++i) {
                m_objSpeakerPatch->addItem(patches[i].name, patches[i].id);
                if (patches[i].id == audioCue->speakerPatchId()) patchIdx = i + 1;
            }
        }
        m_objSpeakerPatch->setCurrentIndex(patchIdx);

        // Push current speakers into the stage view so the user sees
        // the dot relative to their actual rig.
        if (m_workspace && m_workspace->patches()) {
            m_objStageView->setSpeakers(audio::readSpeakers(
                m_workspace->patches(), audioCue->speakerPatchId()));
        } else {
            m_objStageView->setSpeakers({});
        }
        m_objStageView->setAzimuth(static_cast<float>(audioCue->objectAzimuthDeg()));
        m_objStageView->setElevation(static_cast<float>(audioCue->objectElevationDeg()));
        m_objElevation->setValue(static_cast<int>(audioCue->objectElevationDeg() * 10));
        m_objElevationLabel->setText(QStringLiteral("%1°")
            .arg(QString::number(audioCue->objectElevationDeg(), 'f', 0)));
        m_objSpread->setValue(static_cast<int>(audioCue->objectSpread() * 100));
        m_objSpreadLabel->setText(QStringLiteral("%1%")
            .arg(static_cast<int>(audioCue->objectSpread() * 100)));

        // ── Trajectory reload ────────────────────────────────────────
        const auto &traj = audioCue->trajectory();
        QSignalBlocker bMode(m_trajMode);
        m_trajMode->setCurrentIndex(
            traj.mode() == audio::AudioTrajectory::Mode::Loop ? 1 : 0);
        QSignalBlocker bTbl(m_trajTable);
        m_trajTable->setRowCount(traj.keyframeCount());
        const auto &kfs = traj.keyframes();
        for (int i = 0; i < kfs.size(); ++i) {
            const auto &k = kfs[i];
            auto setCell = [&](int col, double v, char fmt = 'f', int prec = 2) {
                auto *it = new QTableWidgetItem(QString::number(v, fmt, prec));
                it->setTextAlignment(Qt::AlignCenter);
                m_trajTable->setItem(i, col, it);
            };
            setCell(0, k.timeSeconds);
            setCell(1, k.azimuthDeg, 'f', 1);
            setCell(2, k.elevationDeg, 'f', 1);
            setCell(3, k.spread, 'f', 2);
        }
    }

    // Color chip preview — fill the chip with the cue's colour, falling
    // back to a neutral background when none is set.
    if (m_cue) {
        const auto col = m_cue->color();
        if (col.isValid()) {
            m_colorChip->setStyleSheet(QStringLiteral(
                "background:%1; color:#0E0F12; border:1px solid #33373F; "
                "border-radius:4px; padding:4px 10px; font-weight:600;").arg(col.name()));
            m_colorChip->setText(col.name(QColor::HexRgb).toUpper());
            m_colorClear->setEnabled(true);
        } else {
            m_colorChip->setStyleSheet(QString());
            m_colorChip->setText(tr("Color…"));
            m_colorClear->setEnabled(false);
        }
    }

    if (lightCue) {
        m_lightUniverse->setValue(lightCue->universe());
        m_lightTable->setRowCount(0);
        const auto &channels = lightCue->channels();
        QList<int> keys = channels.keys();
        std::sort(keys.begin(), keys.end());
        for (int ch : keys) {
            const int row = m_lightTable->rowCount();
            m_lightTable->insertRow(row);
            auto *chItem  = new QTableWidgetItem(QString::number(ch));
            auto *valItem = new QTableWidgetItem(QString::number(channels.value(ch)));
            chItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            valItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            m_lightTable->setItem(row, 0, chItem);
            m_lightTable->setItem(row, 1, valItem);
        }
    }

    if (lfadeCue) {
        // Populate target combo with Light cues only.
        m_lightFadeTarget->clear();
        m_lightFadeTarget->addItem(tr("(no target)"), QVariant::fromValue(QUuid()));
        if (m_workspace) {
            if (auto *list = m_workspace->activeCueList()) {
                for (int row = 0; row < list->cueCount(); ++row) {
                    auto *c = list->cueAt(row);
                    if (!c || c == m_cue) continue;
                    if (qobject_cast<lighting::LightCue *>(c) == nullptr) continue;
                    m_lightFadeTarget->addItem(
                        QStringLiteral("%1  %2")
                            .arg(QString::number(c->number(), 'f', 2),
                                 c->name().isEmpty() ? c->typeName() : c->name()),
                        QVariant::fromValue(c->id()));
                }
            }
        }
        for (int i = 0; i < m_lightFadeTarget->count(); ++i) {
            if (m_lightFadeTarget->itemData(i).toUuid() == lfadeCue->targetId()) {
                m_lightFadeTarget->setCurrentIndex(i);
                break;
            }
        }
        m_lightFadeDuration->setValue(lfadeCue->durationSeconds());
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

void Inspector::commitContinueMode()
{
    if (m_loading) return;
    pushFieldEdit(QStringLiteral("continueMode"), m_continueMode->currentData().toInt());
}

void Inspector::commitWaitDuration()
{
    if (m_loading) return;
    pushFieldEdit(QStringLiteral("durationSeconds"), m_waitDuration->value());
}

void Inspector::commitTargetCue()
{
    if (m_loading) return;
    pushFieldEdit(QStringLiteral("targetId"), m_targetCombo->currentData().toUuid());
}

void Inspector::commitGroupMode()
{
    if (m_loading) return;
    pushFieldEdit(QStringLiteral("mode"), m_groupMode->currentData().toInt());
}

void Inspector::commitGroupStepInterval()
{
    if (m_loading) return;
    pushFieldEdit(QStringLiteral("stepInterval"), m_groupStepInterval->value());
}

void Inspector::addGroupChild()
{
    if (m_loading) return;
    auto *gc = qobject_cast<cues::GroupCue *>(m_cue.data());
    if (!gc) return;
    const auto id = m_groupChildPicker->currentData().toUuid();
    if (id.isNull()) return;
    auto kids = gc->childIds();
    if (kids.contains(id)) return;
    kids.append(id);
    QStringList ids;
    for (const auto &k : kids) ids << k.toString();
    pushFieldEdit(QStringLiteral("childIds"), ids);
}

void Inspector::commitMidiPort()
{
    if (m_loading) return;
    pushFieldEdit(QStringLiteral("portName"),
        m_midiPort->currentData().isValid()
            ? m_midiPort->currentData().toString()
            : m_midiPort->currentText());
}

void Inspector::commitMidiBytes()
{
    if (m_loading) return;
    pushFieldEdit(QStringLiteral("bytes"), m_midiBytes->text());
}

void Inspector::commitMscPort()
{
    if (m_loading) return;
    pushFieldEdit(QStringLiteral("portName"),
        m_mscPort->currentData().isValid()
            ? m_mscPort->currentData().toString()
            : m_mscPort->currentText());
}

void Inspector::commitMscField()
{
    if (m_loading) return;
    pushFieldEdit(QStringLiteral("deviceId"),      m_mscDeviceId->value());
    pushFieldEdit(QStringLiteral("commandFormat"), m_mscFormat->value());
    pushFieldEdit(QStringLiteral("command"),       m_mscCommand->value());
    pushFieldEdit(QStringLiteral("qNumber"),       m_mscQNumber->text());
    pushFieldEdit(QStringLiteral("qList"),         m_mscQList->text());
    pushFieldEdit(QStringLiteral("qPath"),         m_mscQPath->text());
}

void Inspector::removeGroupChild()
{
    if (m_loading) return;
    auto *gc = qobject_cast<cues::GroupCue *>(m_cue.data());
    if (!gc) return;
    auto *item = m_groupChildren->currentItem();
    if (!item) return;
    const auto id = item->data(Qt::UserRole).toUuid();
    auto kids = gc->childIds();
    kids.removeAll(id);
    QStringList ids;
    for (const auto &k : kids) ids << k.toString();
    pushFieldEdit(QStringLiteral("childIds"), ids);
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

void Inspector::onGainSliderChanged(int centiDb)
{
    const double db = centiDb / 100.0;
    m_audioGainLabel->setText(QStringLiteral("%1 dB").arg(QString::number(db, 'f', 1)));
    if (m_loading) return;
    pushFieldEdit(QStringLiteral("gainDb"), db);
    // Live-apply to the playing voice (if any) so the change is audible
    // immediately, not just stored on the cue for next fire.
    if (m_audioEngine) {
        if (auto *ac = qobject_cast<audio::AudioCue *>(m_cue.data())) {
            if (auto vid = ac->currentVoiceId()) m_audioEngine->setVoiceGain(vid, db);
        }
    }
}

void Inspector::onPanSliderChanged(int hundredths)
{
    const double pv = hundredths / 100.0;
    m_audioPanLabel->setText(qFuzzyIsNull(pv)
        ? tr("Centre")
        : (pv < 0
           ? tr("L %1%").arg(int(std::round(std::abs(pv) * 100.0)))
           : tr("R %1%").arg(int(std::round(pv * 100.0)))));
    if (m_loading) return;
    pushFieldEdit(QStringLiteral("pan"), pv);
    if (m_audioEngine) {
        if (auto *ac = qobject_cast<audio::AudioCue *>(m_cue.data())) {
            if (auto vid = ac->currentVoiceId()) m_audioEngine->setVoicePan(vid, pv);
        }
    }
}

void Inspector::commitAudioFadeIn()  { if (!m_loading) pushFieldEdit(QStringLiteral("fadeInSeconds"),  m_audioFadeIn->value()); }
void Inspector::commitAudioFadeOut() { if (!m_loading) pushFieldEdit(QStringLiteral("fadeOutSeconds"), m_audioFadeOut->value()); }
void Inspector::commitAudioTrimIn()  { if (!m_loading) pushFieldEdit(QStringLiteral("trimInSeconds"),  m_audioTrimIn->value()); }
void Inspector::commitAudioTrimOut() { if (!m_loading) pushFieldEdit(QStringLiteral("trimOutSeconds"), m_audioTrimOut->value()); }
void Inspector::commitAudioLoop()    { if (!m_loading) pushFieldEdit(QStringLiteral("loop"),           m_audioLoop->isChecked()); }

void Inspector::commitAudioOutputDevice()
{
    if (m_loading) return;
    pushFieldEdit(QStringLiteral("outputDeviceId"),
                  m_audioOutputDevice->currentData().toByteArray());
}

// ── Object Audio commits ─────────────────────────────────────────────
void Inspector::commitObjectAudioEnabled(bool on)
{
    if (m_loading) return;
    pushFieldEdit(QStringLiteral("objectAudio"), on);

    // Bootstrap: enabling Object Audio with zero speaker patches means
    // the user can do nothing useful — auto-create a Stereo patch and
    // select it. Skip if the cue already references a patch (preserves
    // user intent on toggle-off-then-on).
    if (!on || !m_workspace || !m_workspace->patches()) return;
    auto *audioCue = qobject_cast<audio::AudioCue *>(m_cue.data());
    if (!audioCue || !audioCue->speakerPatchId().isNull()) return;
    auto *patches = m_workspace->patches();
    if (!patches->patchesIn(core::PatchManager::Category::SpeakerArray).isEmpty())
        return;
    const auto fields = audio::toPatchFields(QStringLiteral("stereo"),
        audio::templateSpeakers(QStringLiteral("stereo")));
    const auto id = patches->add(core::PatchManager::Category::SpeakerArray,
                                 tr("Stereo"), fields);
    pushFieldEdit(QStringLiteral("speakerPatchId"), id);
    // Refresh combo to surface the new patch.
    m_loading = true;
    m_objSpeakerPatch->clear();
    m_objSpeakerPatch->addItem(tr("(none)"), QUuid());
    m_objSpeakerPatch->addItem(tr("Stereo"), id);
    m_objSpeakerPatch->setCurrentIndex(1);
    m_objStageView->setSpeakers(audio::readSpeakers(patches, id));
    m_loading = false;
}

void Inspector::commitSpeakerPatch()
{
    if (m_loading) return;
    const QUuid id = m_objSpeakerPatch->currentData().toUuid();
    pushFieldEdit(QStringLiteral("speakerPatchId"), id);
    if (m_workspace && m_workspace->patches()) {
        m_objStageView->setSpeakers(audio::readSpeakers(m_workspace->patches(), id));
    }
}

void Inspector::onStagePositionChanged(float azimuthDeg, float elevationDeg)
{
    if (m_loading) return;
    pushFieldEdit(QStringLiteral("objAzimuth"),   double(azimuthDeg));
    pushFieldEdit(QStringLiteral("objElevation"), double(elevationDeg));

    // Keep the elevation slider in sync without triggering its commit.
    m_loading = true;
    m_objElevation->setValue(static_cast<int>(elevationDeg * 10));
    m_objElevationLabel->setText(QStringLiteral("%1°")
        .arg(QString::number(elevationDeg, 'f', 0)));
    m_loading = false;
}

void Inspector::onElevationSliderChanged(int tenthDeg)
{
    if (m_loading) return;
    const double el = tenthDeg / 10.0;
    m_objElevationLabel->setText(QStringLiteral("%1°")
        .arg(QString::number(el, 'f', 0)));
    pushFieldEdit(QStringLiteral("objElevation"), el);
    m_loading = true;
    m_objStageView->setElevation(static_cast<float>(el));
    m_loading = false;
}

void Inspector::onSpreadSliderChanged(int hundredths)
{
    if (m_loading) return;
    const double s = hundredths / 100.0;
    m_objSpreadLabel->setText(QStringLiteral("%1%").arg(hundredths));
    pushFieldEdit(QStringLiteral("objSpread"), s);
}

void Inspector::rebuildOutputMatrix()
{
    if (!m_outputMatrixLayout) return;
    // Wipe and rebuild — cheap; run on cue-change and device-change.
    while (m_outputMatrixLayout->count() > 0) {
        if (auto *w = m_outputMatrixLayout->itemAt(0)->widget()) {
            m_outputMatrixLayout->removeWidget(w);
            w->deleteLater();
        } else {
            delete m_outputMatrixLayout->takeAt(0);
        }
    }
    m_outputMatrixSliders.clear();
    m_outputMatrixLabels.clear();

    auto *audioCue = qobject_cast<audio::AudioCue *>(m_cue.data());
    if (!audioCue || !m_audioEngine) return;

    // Honour whatever the device reports — defaults to stereo if the
    // device hasn't been opened yet.
    int outChans = m_audioEngine->outputChannelCount(audioCue->outputDeviceId());
    if (outChans <= 0) outChans = 2;

    const auto stored = audioCue->outputGainsDb();
    for (int oc = 0; oc < outChans; ++oc) {
        auto *channelLabel = new QLabel(QStringLiteral("%1").arg(oc + 1),
                                         m_outputMatrixGroup);
        channelLabel->setAlignment(Qt::AlignCenter);
        channelLabel->setMinimumWidth(20);

        auto *slider = new QSlider(Qt::Horizontal, m_outputMatrixGroup);
        slider->setRange(-9000, 1200);    // -90 dB … +12 dB in centi-dB
        const double db = (oc < stored.size()) ? stored[oc] : 0.0;
        slider->setValue(int(db * 100.0));

        auto *valueLabel = new QLabel(QStringLiteral("%1 dB")
            .arg(QString::number(db, 'f', 1)), m_outputMatrixGroup);
        valueLabel->setMinimumWidth(56);
        valueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

        m_outputMatrixLayout->addWidget(channelLabel, oc, 0);
        m_outputMatrixLayout->addWidget(slider,       oc, 1);
        m_outputMatrixLayout->addWidget(valueLabel,   oc, 2);

        m_outputMatrixSliders.append(slider);
        m_outputMatrixLabels.append(valueLabel);

        connect(slider, &QSlider::valueChanged, this,
            [this, valueLabel](int centiDb) {
                valueLabel->setText(QStringLiteral("%1 dB")
                    .arg(QString::number(centiDb / 100.0, 'f', 1)));
                if (!m_loading) onOutputMatrixSliderChanged();
            });
    }

    m_outputMatrixGroup->setChecked(!stored.isEmpty());
}

void Inspector::onOutputMatrixToggled(bool on)
{
    if (m_loading) return;
    auto *audioCue = qobject_cast<audio::AudioCue *>(m_cue.data());
    if (!audioCue) return;
    if (!on) {
        // Empty list = passthrough on every output. Persist that state.
        audioCue->setOutputGainsDb({});
    } else {
        // First enable pulls the current slider values into a gains list.
        onOutputMatrixSliderChanged();
    }
}

void Inspector::onOutputMatrixSliderChanged()
{
    if (m_loading) return;
    auto *audioCue = qobject_cast<audio::AudioCue *>(m_cue.data());
    if (!audioCue) return;
    if (!m_outputMatrixGroup->isChecked()) return;
    QList<double> gains;
    gains.reserve(m_outputMatrixSliders.size());
    for (auto *s : m_outputMatrixSliders) gains.append(s->value() / 100.0);
    audioCue->setOutputGainsDb(gains);
}

void Inspector::openSpeakerPatchDialog()
{
    if (!m_workspace || !m_workspace->patches()) return;
    SpeakerPatchDialog dlg(m_workspace->patches(), this);
    dlg.exec();
    // After the dialog closes the speaker list may have changed —
    // rebuild this row's combo + reload the stage view.
    if (auto *audioCue = qobject_cast<audio::AudioCue *>(m_cue.data())) {
        const auto savedId = audioCue->speakerPatchId();
        m_loading = true;
        m_objSpeakerPatch->clear();
        m_objSpeakerPatch->addItem(tr("(none)"), QUuid());
        int sel = 0;
        const auto patches = m_workspace->patches()->patchesIn(
            core::PatchManager::Category::SpeakerArray);
        for (int i = 0; i < patches.size(); ++i) {
            m_objSpeakerPatch->addItem(patches[i].name, patches[i].id);
            if (patches[i].id == savedId) sel = i + 1;
        }
        m_objSpeakerPatch->setCurrentIndex(sel);
        m_objStageView->setSpeakers(audio::readSpeakers(m_workspace->patches(), savedId));
        m_loading = false;
    }
}

// ── Trajectory editor slots ──────────────────────────────────────────
//
// Trajectory edits write a fresh AudioTrajectory back through pushFieldEdit
// is overkill here (no per-field undo for keyframe table changes in v1);
// instead we mutate the cue directly. Lost: undo on a keyframe move.
// Acceptable trade for shipping object-audio motion in 0.5 — undo for
// trajectories is on the v0.6 list.

namespace {
audio::AudioTrajectory readTrajectoryFromTable(QTableWidget *table,
                                               audio::AudioTrajectory::Mode mode)
{
    audio::AudioTrajectory t;
    t.setMode(mode);
    QList<audio::AudioTrajectory::Keyframe> kfs;
    kfs.reserve(table->rowCount());
    for (int r = 0; r < table->rowCount(); ++r) {
        audio::AudioTrajectory::Keyframe k;
        auto cellD = [&](int col) -> double {
            auto *it = table->item(r, col);
            return it ? it->text().toDouble() : 0.0;
        };
        k.timeSeconds   = cellD(0);
        k.azimuthDeg    = cellD(1);
        k.elevationDeg  = cellD(2);
        k.spread        = qBound(0.0, cellD(3), 1.0);
        kfs.append(k);
    }
    t.setKeyframes(std::move(kfs));
    return t;
}
} // namespace

void Inspector::onTrajectoryAdd()
{
    if (m_loading) return;
    auto *audioCue = qobject_cast<audio::AudioCue *>(m_cue.data());
    if (!audioCue) return;
    auto traj = audioCue->trajectory();
    audio::AudioTrajectory::Keyframe k;
    // New point lands one second after the last keyframe and inherits its
    // pose, so the user can add and then nudge instead of starting from
    // an arbitrary spot.
    if (!traj.keyframes().isEmpty()) {
        const auto &last = traj.keyframes().last();
        k.timeSeconds  = last.timeSeconds + 1.0;
        k.azimuthDeg   = last.azimuthDeg;
        k.elevationDeg = last.elevationDeg;
        k.spread       = last.spread;
    } else {
        k.azimuthDeg   = audioCue->objectAzimuthDeg();
        k.elevationDeg = audioCue->objectElevationDeg();
        k.spread       = audioCue->objectSpread();
    }
    traj.addKeyframe(k);
    audioCue->setTrajectory(std::move(traj));
    onCueChanged();
}

void Inspector::onTrajectoryRemove()
{
    if (m_loading) return;
    auto *audioCue = qobject_cast<audio::AudioCue *>(m_cue.data());
    if (!audioCue) return;
    const int row = m_trajTable->currentRow();
    if (row < 0) return;
    auto traj = audioCue->trajectory();
    traj.removeKeyframe(row);
    audioCue->setTrajectory(std::move(traj));
    onCueChanged();
}

void Inspector::onTrajectoryCellChanged(int /*row*/, int /*column*/)
{
    if (m_loading) return;
    auto *audioCue = qobject_cast<audio::AudioCue *>(m_cue.data());
    if (!audioCue) return;
    const auto mode = (m_trajMode->currentIndex() == 1)
        ? audio::AudioTrajectory::Mode::Loop
        : audio::AudioTrajectory::Mode::OneShot;
    audioCue->setTrajectory(readTrajectoryFromTable(m_trajTable, mode));
}

void Inspector::onTrajectoryModeChanged()
{
    if (m_loading) return;
    auto *audioCue = qobject_cast<audio::AudioCue *>(m_cue.data());
    if (!audioCue) return;
    auto traj = audioCue->trajectory();
    traj.setMode(m_trajMode->currentIndex() == 1
                 ? audio::AudioTrajectory::Mode::Loop
                 : audio::AudioTrajectory::Mode::OneShot);
    audioCue->setTrajectory(std::move(traj));
}

void Inspector::setAudioModeTrim()
{
    const auto wantOn = !m_audioModeTrim->isChecked() ? false : true;
    m_audioModeTrim->setChecked(wantOn);
    if (wantOn) m_audioModeFade->setChecked(false);
    m_audioWaveform->setEditMode(wantOn ? WaveformWidget::EditMode::Trim
                                         : WaveformWidget::EditMode::None);
}

void Inspector::setAudioModeFade()
{
    const auto wantOn = !m_audioModeFade->isChecked() ? false : true;
    m_audioModeFade->setChecked(wantOn);
    if (wantOn) m_audioModeTrim->setChecked(false);
    m_audioWaveform->setEditMode(wantOn ? WaveformWidget::EditMode::Fade
                                         : WaveformWidget::EditMode::None);
}

void Inspector::normalizeAudio()
{
    auto *audioCue = qobject_cast<audio::AudioCue *>(m_cue.data());
    if (!audioCue) return;
    auto file = audioCue->audioFile();
    if (!file || file->state() != audio::AudioFile::State::Loaded) return;
    file->normaliseSamples(0.891f); // ~ -1 dBFS
    m_audioWaveform->update();
}

void Inspector::reverseAudio()
{
    auto *audioCue = qobject_cast<audio::AudioCue *>(m_cue.data());
    if (!audioCue) return;
    auto file = audioCue->audioFile();
    if (!file || file->state() != audio::AudioFile::State::Loaded) return;
    file->reverseSamples();
    m_audioWaveform->update();
}

void Inspector::pickCueColor()
{
    if (!m_cue) return;
    const auto cur = m_cue->color();
    const auto col = QColorDialog::getColor(cur.isValid() ? cur : QColor("#62B4FF"),
        this, tr("Cue color"));
    if (!col.isValid()) return;
    pushFieldEdit(QStringLiteral("color"), col);
}

void Inspector::clearCueColor()
{
    if (!m_cue) return;
    pushFieldEdit(QStringLiteral("color"), QColor()); // invalid = no tint
}

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

void Inspector::commitLightUniverse()
{
    if (!m_loading) pushFieldEdit(QStringLiteral("universe"), m_lightUniverse->value());
}

void Inspector::addLightChannel()
{
    if (m_loading || !qobject_cast<lighting::LightCue *>(m_cue.data())) return;

    // Pick the next free channel after the highest currently in the table.
    int next = 1;
    for (int row = 0; row < m_lightTable->rowCount(); ++row) {
        if (auto *it = m_lightTable->item(row, 0)) {
            const int ch = it->text().toInt();
            if (ch + 1 > next) next = ch + 1;
        }
    }
    if (next > 512) next = 512;

    m_lightTable->blockSignals(true);
    const int row = m_lightTable->rowCount();
    m_lightTable->insertRow(row);
    auto *chItem  = new QTableWidgetItem(QString::number(next));
    auto *valItem = new QTableWidgetItem(QStringLiteral("0"));
    chItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    valItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_lightTable->setItem(row, 0, chItem);
    m_lightTable->setItem(row, 1, valItem);
    m_lightTable->blockSignals(false);
    commitLightChannels();
}

void Inspector::removeLightChannel()
{
    if (m_loading) return;
    const auto sel = m_lightTable->selectionModel()->selectedRows();
    if (sel.isEmpty()) return;
    QList<int> rows;
    for (const auto &idx : sel) rows.append(idx.row());
    std::sort(rows.begin(), rows.end(), std::greater<int>());
    m_lightTable->blockSignals(true);
    for (int r : rows) m_lightTable->removeRow(r);
    m_lightTable->blockSignals(false);
    commitLightChannels();
}

void Inspector::commitLightChannels()
{
    if (m_loading) return;
    QVariantMap map;
    for (int row = 0; row < m_lightTable->rowCount(); ++row) {
        auto *chItem  = m_lightTable->item(row, 0);
        auto *valItem = m_lightTable->item(row, 1);
        if (!chItem || !valItem) continue;
        bool ok1 = false, ok2 = false;
        const int ch  = chItem->text().toInt(&ok1);
        const int val = valItem->text().toInt(&ok2);
        if (!ok1 || !ok2) continue;
        if (ch < 1 || ch > 512) continue;
        map.insert(QString::number(ch), std::clamp(val, 0, 255));
    }
    pushFieldEdit(QStringLiteral("channels"), map);
}

void Inspector::commitLightFadeTarget()
{
    if (m_loading) return;
    pushFieldEdit(QStringLiteral("targetId"), m_lightFadeTarget->currentData().toUuid());
}

void Inspector::commitLightFadeDuration()
{
    if (!m_loading) pushFieldEdit(QStringLiteral("durationSeconds"), m_lightFadeDuration->value());
}

void Inspector::browseVisualFile()
{
    if (!m_cue) return;
    const auto path = QFileDialog::getOpenFileName(this, tr("Pick file"),
        QFileInfo(m_visualPath->text()).absolutePath(),
        tr("Media (*.mp4 *.mov *.mkv *.avi *.webm *.m4v *.png *.jpg *.jpeg *.gif *.bmp *.tiff *.webp);;All files (*.*)"));
    if (path.isEmpty()) return;
    m_visualPath->setText(path);
    pushFieldEdit(QStringLiteral("filePath"), path);
}

void Inspector::commitVisualScreen()
{
    if (!m_loading) pushFieldEdit(QStringLiteral("screenIndex"),
                                  m_visualScreen->currentData().toInt());
}

void Inspector::commitVisualGeometry()
{
    if (m_loading) return;
    pushFieldEdit(QStringLiteral("posX"),    m_visualX->value());
    pushFieldEdit(QStringLiteral("posY"),    m_visualY->value());
    pushFieldEdit(QStringLiteral("posW"),    m_visualW->value());
    pushFieldEdit(QStringLiteral("posH"),    m_visualH->value());
    pushFieldEdit(QStringLiteral("opacity"), m_visualOpacity->value());
}

void Inspector::commitVisualOpacity()
{
    if (!m_loading) pushFieldEdit(QStringLiteral("opacity"), m_visualOpacity->value());
}

void Inspector::commitVideoLoop()
{
    if (!m_loading) pushFieldEdit(QStringLiteral("loop"), m_videoLoop->isChecked());
}

void Inspector::commitTextString()
{
    if (!m_loading) pushFieldEdit(QStringLiteral("text"), m_textString->text());
}

void Inspector::commitTextSize()
{
    if (!m_loading) pushFieldEdit(QStringLiteral("fontPixelSize"), m_textSize->value());
}

void Inspector::pickTextColor()
{
    auto *textCue = qobject_cast<video::TextCue *>(m_cue.data());
    if (!textCue) return;
    const auto col = QColorDialog::getColor(textCue->textColor(), this, tr("Text colour"),
        QColorDialog::ShowAlphaChannel);
    if (!col.isValid()) return;
    pushFieldEdit(QStringLiteral("textColor"), col);
}

} // namespace quewi::ui
