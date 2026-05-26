#include "ui/PreferencesDialog.h"

#include "ui/Theme.h"
#include "audio/AudioEngine.h"
#include "core/CueListModel.h"
#include "midi/MidiInputEngine.h"

#include <QAudioDevice>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMediaDevices>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace quewi::ui {

namespace {

QWidget *makeAudioPage(audio::AudioEngine *engine, QWidget *parent)
{
    auto *page = new QWidget(parent);
    auto *outer = new QVBoxLayout(page);
    outer->setContentsMargins(16, 16, 16, 16);
    outer->setSpacing(12);

    auto *deviceGroup = new QGroupBox(QObject::tr("Output device"), page);
    auto *form = new QFormLayout(deviceGroup);

    auto *deviceCombo = new QComboBox(deviceGroup);
    const auto outputs = QMediaDevices::audioOutputs();
    for (const auto &dev : outputs) {
        deviceCombo->addItem(dev.description(), QVariant::fromValue(dev.id()));
    }
    if (engine) {
        const auto current = engine->defaultOutputDevice().id();
        for (int i = 0; i < deviceCombo->count(); ++i) {
            if (deviceCombo->itemData(i).toByteArray() == current) {
                deviceCombo->setCurrentIndex(i);
                break;
            }
        }
    }
    form->addRow(QObject::tr("Device"), deviceCombo);

    auto *infoLabel = new QLabel(QObject::tr(
        "Format: 48 kHz · stereo · float32. Falls back to the device's "
        "preferred format when 48k/stereo isn't supported."), deviceGroup);
    infoLabel->setWordWrap(true);
    form->addRow(QString(), infoLabel);

    QObject::connect(deviceCombo, &QComboBox::currentIndexChanged, deviceGroup,
        [deviceCombo, engine](int idx) {
            if (!engine) return;
            const auto id = deviceCombo->itemData(idx).toByteArray();
            for (const auto &dev : QMediaDevices::audioOutputs()) {
                if (dev.id() == id) { engine->setDefaultOutputDevice(dev); break; }
            }
            // Persist so the choice survives across launches.
            QSettings s(QStringLiteral("ServeGaming"), QStringLiteral("quewi"));
            s.setValue(QStringLiteral("audio/defaultOutputDeviceId"), id);
        });

    outer->addWidget(deviceGroup);

    // ── Memory budget ─────────────────────────────────────────────
    // Pre-decoded audio lives in RAM until the cue is removed. A 4-min
    // stereo 48 kHz track is ~88 MB; a 90-min show recording is ~2 GB.
    // The budget caps total decoded residency: cues that would push
    // past it stay un-prewarmed (lazy decode on first GO) and the
    // pre-flight check warns. v0.9.2 will make over-budget files
    // stream from disk instead of warning.
    auto *memGroup = new QGroupBox(QObject::tr("Memory budget"), page);
    auto *memForm  = new QFormLayout(memGroup);

    QSettings memSettings(QStringLiteral("ServeGaming"), QStringLiteral("quewi"));
    const int currentBudgetMB = memSettings.value(
        QStringLiteral("audio/memoryBudgetMB"), 512).toInt();

    auto *budgetSpin = new QSpinBox(memGroup);
    budgetSpin->setRange(64, 16'384);
    budgetSpin->setSingleStep(64);
    budgetSpin->setSuffix(QObject::tr(" MB"));
    budgetSpin->setValue(currentBudgetMB);
    QObject::connect(budgetSpin, &QSpinBox::valueChanged, memGroup, [](int v) {
        QSettings s(QStringLiteral("ServeGaming"), QStringLiteral("quewi"));
        s.setValue(QStringLiteral("audio/memoryBudgetMB"), v);
    });
    memForm->addRow(QObject::tr("Decoded audio cap"), budgetSpin);

    auto *memHint = new QLabel(QObject::tr(
        "Total RAM used by pre-decoded audio cues. Going over the cap "
        "doesn't break the show — files that don't fit stay un-prewarmed "
        "and decode lazily on GO. Streaming decode in a later release "
        "will let huge files play without ever residing fully in RAM."),
        memGroup);
    memHint->setWordWrap(true);
    memHint->setStyleSheet(QStringLiteral("color:%1;").arg(Theme::tokens().ink60.name()));
    memForm->addRow(QString(), memHint);

    outer->addWidget(memGroup);
    outer->addStretch(1);
    return page;
}

QWidget *makeCueListPage(PreferencesDialog *dlg, QWidget *parent)
{
    auto *page = new QWidget(parent);
    auto *outer = new QVBoxLayout(page);
    outer->setContentsMargins(16, 16, 16, 16);
    outer->setSpacing(12);

    auto *box = new QGroupBox(QObject::tr("Visible columns"), page);
    auto *boxLayout = new QVBoxLayout(box);
    boxLayout->setSpacing(4);

    auto *hint = new QLabel(QObject::tr(
        "Choose which optional details show in the cue list. Number, type, "
        "name and notes are always visible."), box);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color:%1;").arg(Theme::tokens().ink60.name()));
    boxLayout->addWidget(hint);

    QSettings s(QStringLiteral("ServeGaming"), QStringLiteral("quewi"));
    s.beginGroup(QStringLiteral("ui/cueColumns"));

    using core::CueListModel;
    for (int c = 0; c < CueListModel::ColumnCount; ++c) {
        if (!CueListModel::columnIsOptional(c)) continue;
        const auto key   = CueListModel::columnSettingsKey(c);
        const auto label = CueListModel::columnLabel(c);

        auto *cb = new QCheckBox(label, box);
        cb->setChecked(s.value(key, false).toBool());
        QObject::connect(cb, &QCheckBox::toggled, box, [key, dlg](bool on) {
            QSettings s2(QStringLiteral("ServeGaming"), QStringLiteral("quewi"));
            s2.setValue(QStringLiteral("ui/cueColumns/") + key, on);
            if (dlg) emit dlg->cueListColumnsChanged();
        });
        boxLayout->addWidget(cb);
    }
    s.endGroup();
    boxLayout->addStretch(1);

    auto *applyHint = new QLabel(QObject::tr(
        "Changes apply to new cue lists immediately; existing tabs refresh "
        "when reopened or after the list is reloaded."), page);
    applyHint->setWordWrap(true);
    applyHint->setStyleSheet(QStringLiteral("color:%1; font-size:11px;")
                                  .arg(Theme::tokens().ink40.name()));

    outer->addWidget(box);
    outer->addWidget(applyHint);
    outer->addStretch(1);
    return page;
}

// Friendly description for a stored binding, e.g. "0x90:60" → "Note 60 (ch 1)"
QString describeBinding(const QString &key)
{
    if (key.isEmpty()) return QObject::tr("(unbound)");
    const auto parts = key.split(QChar(':'));
    if (parts.size() != 2) return key;
    bool ok = false;
    const auto status = parts[0].toUInt(&ok, 16);
    const int  data   = parts[1].toInt();
    if (!ok) return key;
    const int channel = int(status & 0x0F) + 1;
    const int high    = int(status & 0xF0);
    QString kind;
    switch (high) {
    case 0x90: kind = QObject::tr("Note %1").arg(data); break;
    case 0xB0: kind = QObject::tr("CC %1").arg(data); break;
    case 0xC0: kind = QObject::tr("Program %1").arg(data); break;
    default:   kind = QStringLiteral("0x%1:%2").arg(status, 2, 16, QChar('0')).arg(data);
    }
    return QObject::tr("%1 (ch %2)").arg(kind).arg(channel);
}

QWidget *makeMidiPage(midi::MidiInputEngine *input, QWidget *parent)
{
    auto *page = new QWidget(parent);
    auto *outer = new QVBoxLayout(page);
    outer->setContentsMargins(16, 16, 16, 16);
    outer->setSpacing(12);

    // ── Input device picker ────────────────────────────────────────
    auto *deviceGroup = new QGroupBox(QObject::tr("Input device"), page);
    auto *form = new QFormLayout(deviceGroup);

    auto *devices = new QComboBox(deviceGroup);
    devices->addItem(QObject::tr("(none)"), QString());
    if (input) {
        for (const auto &name : input->inputPortNames()) devices->addItem(name, name);
        const auto current = input->currentPortName();
        for (int i = 0; i < devices->count(); ++i) {
            if (devices->itemData(i).toString() == current) {
                devices->setCurrentIndex(i); break;
            }
        }
    }
    QObject::connect(devices, &QComboBox::currentIndexChanged, deviceGroup,
        [devices, input](int idx) {
            if (!input) return;
            const auto name = devices->itemData(idx).toString();
            input->openPort(name);
            QSettings s(QStringLiteral("ServeGaming"), QStringLiteral("quewi"));
            s.setValue(QStringLiteral("midi/inputPort"), name);
        });
    form->addRow(QObject::tr("Port"), devices);
    outer->addWidget(deviceGroup);

    // ── Bindings ──────────────────────────────────────────────────
    auto *bindGroup = new QGroupBox(QObject::tr("Triggers"), page);
    auto *bindForm = new QFormLayout(bindGroup);

    struct Action { const char *key; QString label; };
    const Action actions[] = {
        { "go",      QObject::tr("GO") },
        { "pause",   QObject::tr("Pause / resume") },
        { "fadeAll", QObject::tr("Fade all") },
        { "panic",   QObject::tr("Panic") },
    };

    for (const auto &a : actions) {
        const QString settingsKey =
            QStringLiteral("midi/binding/%1").arg(QString::fromLatin1(a.key));

        auto *row = new QWidget(bindGroup);
        auto *rowLay = new QHBoxLayout(row);
        rowLay->setContentsMargins(0, 0, 0, 0);

        auto *valueLabel = new QLabel(row);
        QSettings s0(QStringLiteral("ServeGaming"), QStringLiteral("quewi"));
        valueLabel->setText(describeBinding(s0.value(settingsKey).toString()));
        valueLabel->setStyleSheet(QStringLiteral("font-family:Consolas,monospace;"));

        auto *learn = new QPushButton(QObject::tr("Learn…"), row);
        learn->setCheckable(true);
        auto *clear = new QPushButton(QObject::tr("Clear"), row);

        rowLay->addWidget(valueLabel, 1);
        rowLay->addWidget(learn);
        rowLay->addWidget(clear);

        QObject::connect(clear, &QPushButton::clicked, row, [settingsKey, valueLabel] {
            QSettings s(QStringLiteral("ServeGaming"), QStringLiteral("quewi"));
            s.remove(settingsKey);
            valueLabel->setText(describeBinding(QString()));
        });

        QObject::connect(learn, &QPushButton::toggled, row,
            [learn, valueLabel, settingsKey, input](bool on) {
                if (!input) {
                    learn->setChecked(false);
                    return;
                }
                static QMetaObject::Connection s_conn;
                if (s_conn) QObject::disconnect(s_conn);
                if (!on) return;
                learn->setText(QObject::tr("Press a key…"));
                s_conn = QObject::connect(input,
                    &midi::MidiInputEngine::messageReceived, learn,
                    [learn, valueLabel, settingsKey](quint8 status, const QByteArray &bytes) {
                        if (bytes.size() < 2) return;
                        // Skip note-off and zero-vel note-on so the
                        // captured trigger is the press, not the
                        // release.
                        const quint8 high = status & 0xF0;
                        if (high == 0x80) return;
                        if (high == 0x90 && bytes.size() >= 3
                            && quint8(bytes[2]) == 0) return;
                        const QString key = QStringLiteral("%1:%2")
                            .arg(int(status), 2, 16, QChar('0'))
                            .arg(int(quint8(bytes[1])));
                        QSettings s(QStringLiteral("ServeGaming"),
                                    QStringLiteral("quewi"));
                        s.setValue(settingsKey, key);
                        valueLabel->setText(describeBinding(key));
                        learn->setChecked(false);
                        learn->setText(QObject::tr("Learn…"));
                    });
            });

        bindForm->addRow(a.label, row);
    }

    auto *hint = new QLabel(QObject::tr(
        "Click <b>Learn…</b> then press a key/pad/CC on your controller. "
        "Note-off and zero-velocity messages are filtered so the captured "
        "trigger is the press, not the release."), bindGroup);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color:%1; font-size:11px;")
                                  .arg(Theme::tokens().ink40.name()));
    bindForm->addRow(QString(), hint);

    outer->addWidget(bindGroup);
    outer->addStretch(1);
    return page;
}

// Unified settings handle. Every page reads/writes through this one
// QSettings instance so keys stay consistent across the codebase.
QSettings prefSettings()
{
    return QSettings(QStringLiteral("ServeGaming"),
                     QStringLiteral("quewi"));
}

// Small helper for the "applies on next launch" hint that appears on a
// few pages whose settings are read at startup, not live.
QLabel *makeHint(const QString &text, QWidget *parent)
{
    auto *l = new QLabel(text, parent);
    l->setWordWrap(true);
    l->setStyleSheet(QStringLiteral("color:%1; font-size:11px;")
                         .arg(Theme::tokens().ink60.name()));
    return l;
}

// ── General ───────────────────────────────────────────────────────
// Catches the cross-cutting workflow defaults that don't belong to a
// specific subsystem. All settings are honoured at next read; the
// auto-save timer + confirm-delete switch take effect immediately the
// next time their respective code paths fire.
QWidget *makeGeneralPage(QWidget *parent)
{
    auto *page  = new QWidget(parent);
    auto *outer = new QVBoxLayout(page);
    outer->setContentsMargins(16, 16, 16, 16);
    outer->setSpacing(12);
    auto s = prefSettings();

    // Auto-save group
    auto *autosaveGroup = new QGroupBox(QObject::tr("Auto-save"), page);
    auto *autoForm = new QFormLayout(autosaveGroup);
    auto *autoEnable = new QCheckBox(
        QObject::tr("Save journal entries while editing"), autosaveGroup);
    autoEnable->setChecked(s.value(QStringLiteral("general/autosaveEnabled"),
                                   true).toBool());
    QObject::connect(autoEnable, &QCheckBox::toggled, autosaveGroup, [](bool v) {
        prefSettings().setValue(QStringLiteral("general/autosaveEnabled"), v);
    });
    autoForm->addRow(QString(), autoEnable);

    auto *autoIv = new QSpinBox(autosaveGroup);
    autoIv->setRange(10, 600);
    autoIv->setSuffix(QObject::tr(" s"));
    autoIv->setValue(s.value(QStringLiteral("general/autosaveSeconds"),
                             60).toInt());
    QObject::connect(autoIv, &QSpinBox::valueChanged, autosaveGroup, [](int v) {
        prefSettings().setValue(QStringLiteral("general/autosaveSeconds"), v);
    });
    autoForm->addRow(QObject::tr("Interval"), autoIv);
    autoForm->addRow(QString(), makeHint(QObject::tr(
        "The crash-recovery journal flushes every interval. Lower "
        "values mean less work lost on a crash; higher values mean "
        "fewer disk writes during heavy edits."), autosaveGroup));
    outer->addWidget(autosaveGroup);

    // Cue defaults group
    auto *cueGroup = new QGroupBox(QObject::tr("Cue defaults"), page);
    auto *cueForm = new QFormLayout(cueGroup);

    auto *stepSpin = new QDoubleSpinBox(cueGroup);
    stepSpin->setRange(0.01, 100.0);
    stepSpin->setSingleStep(0.5);
    stepSpin->setDecimals(2);
    stepSpin->setValue(s.value(QStringLiteral("general/cueNumberStep"),
                               1.0).toDouble());
    QObject::connect(stepSpin, &QDoubleSpinBox::valueChanged, cueGroup,
        [](double v) {
            prefSettings().setValue(QStringLiteral("general/cueNumberStep"), v);
        });
    cueForm->addRow(QObject::tr("Numbering step"), stepSpin);

    auto *preWait = new QDoubleSpinBox(cueGroup);
    preWait->setRange(0.0, 86400.0);
    preWait->setDecimals(2);
    preWait->setSuffix(QObject::tr(" s"));
    preWait->setValue(s.value(QStringLiteral("general/defaultPreWait"),
                              0.0).toDouble());
    QObject::connect(preWait, &QDoubleSpinBox::valueChanged, cueGroup,
        [](double v) {
            prefSettings().setValue(QStringLiteral("general/defaultPreWait"), v);
        });
    cueForm->addRow(QObject::tr("Default pre-wait"), preWait);

    auto *postWait = new QDoubleSpinBox(cueGroup);
    postWait->setRange(0.0, 86400.0);
    postWait->setDecimals(2);
    postWait->setSuffix(QObject::tr(" s"));
    postWait->setValue(s.value(QStringLiteral("general/defaultPostWait"),
                               0.0).toDouble());
    QObject::connect(postWait, &QDoubleSpinBox::valueChanged, cueGroup,
        [](double v) {
            prefSettings().setValue(QStringLiteral("general/defaultPostWait"), v);
        });
    cueForm->addRow(QObject::tr("Default post-wait"), postWait);

    auto *confirmDel = new QCheckBox(
        QObject::tr("Confirm before deleting a cue"), cueGroup);
    confirmDel->setChecked(s.value(QStringLiteral("general/confirmDelete"),
                                   true).toBool());
    QObject::connect(confirmDel, &QCheckBox::toggled, cueGroup, [](bool v) {
        prefSettings().setValue(QStringLiteral("general/confirmDelete"), v);
    });
    cueForm->addRow(QString(), confirmDel);
    outer->addWidget(cueGroup);

    // Display group
    auto *fmtGroup = new QGroupBox(QObject::tr("Display"), page);
    auto *fmtForm = new QFormLayout(fmtGroup);
    auto *timeFmt = new QComboBox(fmtGroup);
    timeFmt->addItem(QObject::tr("Seconds (1.45)"),  QStringLiteral("seconds"));
    timeFmt->addItem(QObject::tr("HH:MM:SS.ss"),     QStringLiteral("hms"));
    const QString curFmt = s.value(QStringLiteral("general/timeFormat"),
                                   QStringLiteral("seconds")).toString();
    for (int i = 0; i < timeFmt->count(); ++i) {
        if (timeFmt->itemData(i).toString() == curFmt) {
            timeFmt->setCurrentIndex(i); break;
        }
    }
    QObject::connect(timeFmt, &QComboBox::currentIndexChanged, fmtGroup,
        [timeFmt](int) {
            prefSettings().setValue(QStringLiteral("general/timeFormat"),
                                    timeFmt->currentData().toString());
        });
    fmtForm->addRow(QObject::tr("Time format"), timeFmt);
    outer->addWidget(fmtGroup);

    outer->addStretch(1);
    return page;
}

// ── Theme ─────────────────────────────────────────────────────────
// Theme picker is canonical here (the View → Theme menu still works
// but writes into the same settings keys). The dialog emits signals
// so the main window can re-apply without restart.
QWidget *makeThemePage(class PreferencesDialog *dlg, QWidget *parent)
{
    auto *page  = new QWidget(parent);
    auto *outer = new QVBoxLayout(page);
    outer->setContentsMargins(16, 16, 16, 16);
    outer->setSpacing(12);
    auto s = prefSettings();

    auto *themeGroup = new QGroupBox(QObject::tr("Appearance"), page);
    auto *themeForm = new QFormLayout(themeGroup);

    auto *themeCombo = new QComboBox(themeGroup);
    themeCombo->addItem(QObject::tr("Dark"),
                        QStringLiteral("quewi-dark"));
    themeCombo->addItem(QObject::tr("Light"),
                        QStringLiteral("quewi-light"));
    themeCombo->addItem(QObject::tr("High contrast"),
                        QStringLiteral("quewi-highcontrast"));
    const QString curTheme = s.value(QStringLiteral("theme/name"),
                                     QStringLiteral("quewi-dark")).toString();
    for (int i = 0; i < themeCombo->count(); ++i) {
        if (themeCombo->itemData(i).toString() == curTheme) {
            themeCombo->setCurrentIndex(i); break;
        }
    }
    themeForm->addRow(QObject::tr("Theme"), themeCombo);

    auto *densityCombo = new QComboBox(themeGroup);
    densityCombo->addItem(QObject::tr("Compact"),     QStringLiteral("compact"));
    densityCombo->addItem(QObject::tr("Cozy"),        QStringLiteral("cozy"));
    densityCombo->addItem(QObject::tr("Comfortable"), QStringLiteral("comfortable"));
    const QString curDensity = s.value(QStringLiteral("theme/rowDensity"),
                                       QStringLiteral("cozy")).toString();
    for (int i = 0; i < densityCombo->count(); ++i) {
        if (densityCombo->itemData(i).toString() == curDensity) {
            densityCombo->setCurrentIndex(i); break;
        }
    }
    themeForm->addRow(QObject::tr("Cue row density"), densityCombo);

    auto *reducedMotion = new QCheckBox(
        QObject::tr("Reduce motion (disable scroll easing)"), themeGroup);
    reducedMotion->setChecked(s.value(QStringLiteral("theme/reducedMotion"),
                                      false).toBool());
    QObject::connect(reducedMotion, &QCheckBox::toggled, themeGroup, [](bool v) {
        prefSettings().setValue(QStringLiteral("theme/reducedMotion"), v);
    });
    themeForm->addRow(QString(), reducedMotion);

    outer->addWidget(themeGroup);
    outer->addWidget(makeHint(QObject::tr(
        "Density and accent changes apply on next window open. "
        "Theme switches are live."), page));
    outer->addStretch(1);

    // Wire signals back to the dialog (which proxies them to MainWindow).
    QObject::connect(themeCombo, &QComboBox::currentIndexChanged, dlg,
        [dlg, themeCombo](int) {
            const auto name = themeCombo->currentData().toString();
            prefSettings().setValue(QStringLiteral("theme/name"), name);
            emit dlg->themeChanged(name);
        });
    QObject::connect(densityCombo, &QComboBox::currentIndexChanged, dlg,
        [dlg, densityCombo](int) {
            const auto v = densityCombo->currentData().toString();
            prefSettings().setValue(QStringLiteral("theme/rowDensity"), v);
            emit dlg->rowDensityChanged(v);
        });
    return page;
}

// ── OSC ───────────────────────────────────────────────────────────
// Simplified: lead with the one knob that matters (UDP port for the
// remote control API), park TCP/WebSocket/logging under an Advanced
// group, drop the listen-address and pattern-mode fields entirely
// (their defaults are right; the QSettings keys osc/listenAddress
// and osc/patternMode still exist for power users editing config).
QWidget *makeOscPage(QWidget *parent)
{
    auto *page  = new QWidget(parent);
    auto *outer = new QVBoxLayout(page);
    outer->setContentsMargins(16, 16, 16, 16);
    outer->setSpacing(12);
    auto s = prefSettings();

    // Header — the one sentence a user needs to read.
    auto *header = new QLabel(QObject::tr(
        "Quewi listens for OSC remote control on UDP. Any external "
        "app — a lighting console, an iPad remote, your own script "
        "— can send commands, query the cue list, and subscribe to "
        "live updates. See docs/osc-remote-api.md for the full "
        "message vocabulary."), page);
    header->setWordWrap(true);
    outer->addWidget(header);

    // Main group — UDP port only.
    auto *mainGroup = new QGroupBox(QObject::tr("Remote control"), page);
    auto *mainForm = new QFormLayout(mainGroup);
    auto *udpPort = new QSpinBox(mainGroup);
    udpPort->setRange(1, 65535);
    udpPort->setValue(s.value(QStringLiteral("osc/udpPort"), 53535).toInt());
    QObject::connect(udpPort, &QSpinBox::valueChanged, mainGroup, [](int v) {
        prefSettings().setValue(QStringLiteral("osc/udpPort"), v);
    });
    mainForm->addRow(QObject::tr("UDP port"), udpPort);
    mainForm->addRow(QString(), makeHint(QObject::tr(
        "Default 53535. Tell your remote app this port + the IP "
        "address of this machine. Bound to all network interfaces "
        "(0.0.0.0). Takes effect on next launch."), mainGroup));
    outer->addWidget(mainGroup);

    // Advanced group — extra transports + diagnostics.
    auto *advGroup = new QGroupBox(QObject::tr("Advanced"), page);
    auto *advForm = new QFormLayout(advGroup);

    auto *tcpRow = new QHBoxLayout();
    auto *tcpEnable = new QCheckBox(QObject::tr("Enable"), advGroup);
    tcpEnable->setChecked(s.value(QStringLiteral("osc/tcpEnabled"),
                                  false).toBool());
    auto *tcpPort = new QSpinBox(advGroup);
    tcpPort->setRange(1, 65535);
    tcpPort->setValue(s.value(QStringLiteral("osc/tcpPort"), 53536).toInt());
    tcpPort->setEnabled(tcpEnable->isChecked());
    QObject::connect(tcpEnable, &QCheckBox::toggled, advGroup,
        [tcpPort](bool v) {
            prefSettings().setValue(QStringLiteral("osc/tcpEnabled"), v);
            tcpPort->setEnabled(v);
        });
    QObject::connect(tcpPort, &QSpinBox::valueChanged, advGroup, [](int v) {
        prefSettings().setValue(QStringLiteral("osc/tcpPort"), v);
    });
    tcpRow->addWidget(tcpEnable);
    tcpRow->addWidget(tcpPort, 1);
    advForm->addRow(QObject::tr("TCP / SLIP"), tcpRow);

    auto *wsRow = new QHBoxLayout();
    auto *wsEnable = new QCheckBox(QObject::tr("Enable"), advGroup);
    wsEnable->setChecked(s.value(QStringLiteral("osc/wsEnabled"),
                                 false).toBool());
    auto *wsPort = new QSpinBox(advGroup);
    wsPort->setRange(1, 65535);
    wsPort->setValue(s.value(QStringLiteral("osc/wsPort"), 8080).toInt());
    wsPort->setEnabled(wsEnable->isChecked());
    QObject::connect(wsEnable, &QCheckBox::toggled, advGroup,
        [wsPort](bool v) {
            prefSettings().setValue(QStringLiteral("osc/wsEnabled"), v);
            wsPort->setEnabled(v);
        });
    QObject::connect(wsPort, &QSpinBox::valueChanged, advGroup, [](int v) {
        prefSettings().setValue(QStringLiteral("osc/wsPort"), v);
    });
    wsRow->addWidget(wsEnable);
    wsRow->addWidget(wsPort, 1);
    advForm->addRow(QObject::tr("WebSocket"), wsRow);

    auto *logIncoming = new QCheckBox(
        QObject::tr("Log incoming messages to file"), advGroup);
    logIncoming->setChecked(s.value(QStringLiteral("osc/logIncoming"),
                                    false).toBool());
    QObject::connect(logIncoming, &QCheckBox::toggled, advGroup, [](bool v) {
        prefSettings().setValue(QStringLiteral("osc/logIncoming"), v);
    });
    advForm->addRow(QString(), logIncoming);

    advForm->addRow(QString(), makeHint(QObject::tr(
        "UDP is enough for almost every remote. Turn on TCP or "
        "WebSocket only if your specific remote needs them. The "
        "live OSC monitor (Tools → OSC Monitor) shows traffic in "
        "real time without writing the log file."), advGroup));

    outer->addWidget(advGroup);
    outer->addStretch(1);
    return page;
}

// ── Show Mode ─────────────────────────────────────────────────────
// Operator-safety surface. PIN is stored in plain QSettings — good
// enough to deter idle clicks during a show, not a security boundary.
QWidget *makeShowModePage(QWidget *parent)
{
    auto *page  = new QWidget(parent);
    auto *outer = new QVBoxLayout(page);
    outer->setContentsMargins(16, 16, 16, 16);
    outer->setSpacing(12);
    auto s = prefSettings();

    auto *lockGroup = new QGroupBox(QObject::tr("Lock"), page);
    auto *lockForm = new QFormLayout(lockGroup);

    auto *pin = new QLineEdit(lockGroup);
    pin->setEchoMode(QLineEdit::Password);
    pin->setPlaceholderText(QObject::tr("Optional"));
    pin->setText(s.value(QStringLiteral("showmode/pin"), QString()).toString());
    QObject::connect(pin, &QLineEdit::editingFinished, lockGroup, [pin] {
        prefSettings().setValue(QStringLiteral("showmode/pin"),
                                pin->text());
    });
    lockForm->addRow(QObject::tr("Unlock PIN"), pin);

    auto *autoEnter = new QCheckBox(
        QObject::tr("Enter Show Mode automatically when opening a show"),
        lockGroup);
    autoEnter->setChecked(s.value(QStringLiteral("showmode/autoEnterOnOpen"),
                                  false).toBool());
    QObject::connect(autoEnter, &QCheckBox::toggled, lockGroup, [](bool v) {
        prefSettings().setValue(QStringLiteral("showmode/autoEnterOnOpen"), v);
    });
    lockForm->addRow(QString(), autoEnter);

    outer->addWidget(lockGroup);

    auto *allowGroup = new QGroupBox(
        QObject::tr("Allowed during Show Mode"), page);
    auto *allowForm = new QFormLayout(allowGroup);

    auto *allowPause = new QCheckBox(
        QObject::tr("Pause / resume"), allowGroup);
    allowPause->setChecked(s.value(QStringLiteral("showmode/allowPause"),
                                   true).toBool());
    QObject::connect(allowPause, &QCheckBox::toggled, allowGroup, [](bool v) {
        prefSettings().setValue(QStringLiteral("showmode/allowPause"), v);
    });
    allowForm->addRow(QString(), allowPause);

    auto *hideMenu = new QCheckBox(
        QObject::tr("Hide the menu bar"), allowGroup);
    hideMenu->setChecked(s.value(QStringLiteral("showmode/hideMenuBar"),
                                 false).toBool());
    QObject::connect(hideMenu, &QCheckBox::toggled, allowGroup, [](bool v) {
        prefSettings().setValue(QStringLiteral("showmode/hideMenuBar"), v);
    });
    allowForm->addRow(QString(), hideMenu);

    allowForm->addRow(QString(), makeHint(QObject::tr(
        "Panic and GO are always available in Show Mode regardless "
        "of the toggles above — that's a hard rule, not a setting."),
        allowGroup));

    outer->addWidget(allowGroup);
    outer->addStretch(1);
    return page;
}

// ── Lighting ──────────────────────────────────────────────────────
// Universe table lives in Patch Editor; this page handles the
// engine-wide knobs. Refresh rate balances DMX timing strictness vs
// CPU; blackout-on-Panic is the consensus default for theatre.
QWidget *makeLightingPage(QWidget *parent)
{
    auto *page  = new QWidget(parent);
    auto *outer = new QVBoxLayout(page);
    outer->setContentsMargins(16, 16, 16, 16);
    outer->setSpacing(12);
    auto s = prefSettings();

    auto *engGroup = new QGroupBox(QObject::tr("Output"), page);
    auto *engForm = new QFormLayout(engGroup);

    auto *refresh = new QSpinBox(engGroup);
    refresh->setRange(10, 60);
    refresh->setSuffix(QObject::tr(" Hz"));
    refresh->setValue(s.value(QStringLiteral("lighting/refreshRateHz"),
                              30).toInt());
    QObject::connect(refresh, &QSpinBox::valueChanged, engGroup, [](int v) {
        prefSettings().setValue(QStringLiteral("lighting/refreshRateHz"), v);
    });
    engForm->addRow(QObject::tr("Refresh rate"), refresh);

    auto *blackout = new QCheckBox(
        QObject::tr("Blackout all lighting universes on Panic"), engGroup);
    blackout->setChecked(s.value(QStringLiteral("lighting/blackoutOnPanic"),
                                 true).toBool());
    QObject::connect(blackout, &QCheckBox::toggled, engGroup, [](bool v) {
        prefSettings().setValue(QStringLiteral("lighting/blackoutOnPanic"), v);
    });
    engForm->addRow(QString(), blackout);

    outer->addWidget(engGroup);

    auto *patchGroup = new QGroupBox(QObject::tr("Universes"), page);
    auto *patchLayout = new QVBoxLayout(patchGroup);
    patchLayout->addWidget(makeHint(QObject::tr(
        "Universes (sACN, Art-Net, DMX-USB) are configured in the "
        "Patch Editor — Tools → Patch Editor."), patchGroup));
    outer->addWidget(patchGroup);

    outer->addWidget(makeHint(QObject::tr(
        "Refresh rate applies on next launch."), page));
    outer->addStretch(1);
    return page;
}

} // namespace

PreferencesDialog::PreferencesDialog(audio::AudioEngine *audioEngine,
                                     midi::MidiInputEngine *midiInput,
                                     QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Preferences"));
    resize(720, 500);

    auto *root = new QVBoxLayout(this);

    auto *splitter = new QSplitter(Qt::Horizontal, this);
    auto *categories = new QListWidget(splitter);
    auto *pages = new QStackedWidget(splitter);

    struct Page { QString name; QWidget *widget; };
    const Page items[] = {
        { tr("General"),   makeGeneralPage(pages) },
        { tr("Audio"),     makeAudioPage(audioEngine, pages) },
        { tr("Cue List"),  makeCueListPage(this, pages) },
        { tr("OSC"),       makeOscPage(pages) },
        { tr("MIDI"),      makeMidiPage(midiInput, pages) },
        { tr("Lighting"),  makeLightingPage(pages) },
        { tr("Theme"),     makeThemePage(this, pages) },
        { tr("Show Mode"), makeShowModePage(pages) },
    };
    for (const auto &p : items) {
        categories->addItem(p.name);
        pages->addWidget(p.widget);
    }
    categories->setCurrentRow(0);   // open on General — most-edited page

    splitter->addWidget(categories);
    splitter->addWidget(pages);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({160, 560});
    root->addWidget(splitter, 1);

    QObject::connect(categories, &QListWidget::currentRowChanged,
                     pages, &QStackedWidget::setCurrentIndex);

    auto *bb = new QDialogButtonBox(QDialogButtonBox::Close, this);
    QObject::connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(bb);
}

PreferencesDialog::~PreferencesDialog() = default;

} // namespace quewi::ui
