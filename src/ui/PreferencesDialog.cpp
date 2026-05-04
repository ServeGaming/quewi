#include "ui/PreferencesDialog.h"

#include "audio/AudioEngine.h"

#include <QAudioDevice>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QListWidget>
#include <QMediaDevices>
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

PreferencesDialog::PreferencesDialog(audio::AudioEngine *audioEngine, QWidget *parent)
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
        { tr("OSC"),      makePlaceholderPage(tr("OSC"),      pages) },
        { tr("MIDI"),     makePlaceholderPage(tr("MIDI"),     pages) },
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
