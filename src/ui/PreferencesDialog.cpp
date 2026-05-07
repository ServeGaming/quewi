#include "ui/PreferencesDialog.h"

#include "audio/AudioEngine.h"
#include "core/CueListModel.h"
#include "midi/MidiInputEngine.h"

#include <QAudioDevice>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMediaDevices>
#include <QPushButton>
#include <QSettings>
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
    hint->setStyleSheet(QStringLiteral("color:#A8AEBA;"));
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
    applyHint->setStyleSheet(QStringLiteral("color:#7A828F; font-size:11px;"));

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
    hint->setStyleSheet(QStringLiteral("color:#7A828F; font-size:11px;"));
    bindForm->addRow(QString(), hint);

    outer->addWidget(bindGroup);
    outer->addStretch(1);
    return page;
}

QWidget *makePlaceholderPage(const QString &name, QWidget *parent)
{
    auto *page = new QWidget(parent);
    auto *layout = new QVBoxLayout(page);
    auto *label = new QLabel(QObject::tr(
        "%1 settings will appear here as the subsystem lands.").arg(name));
    label->setAlignment(Qt::AlignCenter);
    label->setWordWrap(true);
    layout->addWidget(label);
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
        { tr("General"),  makePlaceholderPage(tr("General"),  pages) },
        { tr("Audio"),    makeAudioPage(audioEngine, pages) },
        { tr("Cue List"), makeCueListPage(this, pages) },
        { tr("OSC"),      makePlaceholderPage(tr("OSC"),      pages) },
        { tr("MIDI"),     makeMidiPage(midiInput, pages) },
        { tr("Lighting"), makePlaceholderPage(tr("Lighting"), pages) },
        { tr("Theme"),    makePlaceholderPage(tr("Theme"),    pages) },
        { tr("Show Mode"),makePlaceholderPage(tr("Show Mode"),pages) },
    };
    for (const auto &p : items) {
        categories->addItem(p.name);
        pages->addWidget(p.widget);
    }
    categories->setCurrentRow(1); // start on Audio — the only working page

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
