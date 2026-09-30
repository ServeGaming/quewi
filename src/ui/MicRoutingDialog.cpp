#include "ui/MicRoutingDialog.h"

#include "ui/MicRouting.h"
#include "ui/Theme.h"

#include <QAudioDevice>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMediaDevices>
#include <QSlider>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace quewi::ui {

namespace {
QString dbText(double db)
{
    return db > 0.0 ? QStringLiteral("+%1 dB").arg(db, 0, 'f', 0)
                    : QStringLiteral("%1 dB").arg(db, 0, 'f', 0);
}
}

QString MicRoutingDialog::buttonText()
{
    auto *mr = MicRouting::instance();
    if (!mr->enabled()) return tr("Mic: Off");
    for (const auto &dev : QMediaDevices::audioOutputs())
        if (dev.id() == mr->deviceId()) {
            QString name = dev.description();
            if (name.size() > 22) name = name.left(21) + QChar(0x2026);
            return tr("Mic: %1").arg(name);
        }
    return tr("Mic: (device missing)");
}

QSlider *MicRoutingDialog::dbSlider(double value)
{
    auto *s = new QSlider(Qt::Horizontal, this);
    s->setRange(-24, 12);
    s->setValue(int(std::lround(value)));
    s->setMinimumWidth(180);
    return s;
}

MicRoutingDialog::MicRoutingDialog(const QByteArray &boardOutputId, QWidget *parent)
    : QDialog(parent), m_boardOutput(boardOutputId)
{
    setWindowTitle(tr("Send sounds to your mic"));
    auto *mr = MicRouting::instance();
    const auto &tk = Theme::tokens();

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(18, 16, 18, 14);
    root->setSpacing(12);

    auto *intro = new QLabel(tr(
        "Plays pad sounds into a <b>virtual cable</b>, so Discord, OBS or a game "
        "hears them through your microphone."), this);
    intro->setWordWrap(true);
    root->addWidget(intro);

    auto *form = new QFormLayout();
    form->setSpacing(10);

    // ── Where the sounds go ───────────────────────────────────────────
    m_device = new QComboBox(this);
    m_device->addItem(tr("Off"), QByteArray());
    QList<QAudioDevice> outs = QMediaDevices::audioOutputs();
    std::stable_sort(outs.begin(), outs.end(), [](const QAudioDevice &a, const QAudioDevice &b) {
        return MicRouting::looksVirtual(a.description()) && !MicRouting::looksVirtual(b.description());
    });
    bool anyVirtual = false;
    for (const auto &dev : outs) {
        const bool v = MicRouting::looksVirtual(dev.description());
        anyVirtual |= v;
        m_device->addItem(v ? tr("%1  (virtual cable)").arg(dev.description())
                            : dev.description(), dev.id());
    }
    m_device->setCurrentIndex(std::max(0, m_device->findData(mr->deviceId())));
    form->addRow(tr("Send sounds to"), m_device);

    m_noCable = new QLabel(tr(
        "No virtual cable found. Install one, such as the free "
        "<a href='https://vb-audio.com/Cable/'>VB-Audio Virtual Cable</a>, "
        "then reopen this window."), this);
    m_noCable->setWordWrap(true);
    m_noCable->setOpenExternalLinks(true);
    m_noCable->setStyleSheet(QStringLiteral("color:%1;").arg(tk.warn.name()));
    m_noCable->setVisible(!anyVirtual);
    form->addRow(QString(), m_noCable);

    m_warning = new QLabel(this);
    m_warning->setWordWrap(true);
    m_warning->setStyleSheet(QStringLiteral("color:%1;").arg(tk.warn.name()));
    form->addRow(QString(), m_warning);

    m_monitor = new QCheckBox(tr("Also play them on the soundboard's output, so I hear them"), this);
    m_monitor->setChecked(mr->monitor());
    form->addRow(QString(), m_monitor);

    auto *sfxRow = new QHBoxLayout();
    m_sfxGain = dbSlider(mr->sfxGainDb());
    m_sfxGainVal = new QLabel(dbText(mr->sfxGainDb()), this);
    m_sfxGainVal->setMinimumWidth(48);
    sfxRow->addWidget(m_sfxGain, 1);
    sfxRow->addWidget(m_sfxGainVal);
    form->addRow(tr("Sound level in mic"), sfxRow);

    // ── Your own voice ────────────────────────────────────────────────
    m_passthrough = new QCheckBox(tr("Mix in my microphone"), this);
    m_passthrough->setChecked(mr->passthrough());
    m_passthrough->setToolTip(tr(
        "quewi listens to your real microphone and sends it into the cable\n"
        "with the sounds, so the app hears your voice AND the soundboard."));
    form->addRow(QString(), m_passthrough);

    m_input = new QComboBox(this);
    const QAudioDevice defIn = QMediaDevices::defaultAudioInput();
    for (const auto &dev : QMediaDevices::audioInputs()) {
        if (MicRouting::looksVirtual(dev.description())) continue;   // not the cable itself
        m_input->addItem(dev.id() == defIn.id() ? tr("%1  (default)").arg(dev.description())
                                                : dev.description(), dev.id());
    }
    int inIdx = m_input->findData(mr->inputDeviceId());
    if (inIdx < 0) inIdx = std::max(0, m_input->findData(defIn.id()));
    m_input->setCurrentIndex(inIdx);
    form->addRow(tr("Microphone"), m_input);

    auto *inRow = new QHBoxLayout();
    m_inputGain = dbSlider(mr->inputGainDb());
    m_inputGainVal = new QLabel(dbText(mr->inputGainDb()), this);
    m_inputGainVal->setMinimumWidth(48);
    inRow->addWidget(m_inputGain, 1);
    inRow->addWidget(m_inputGainVal);
    form->addRow(tr("Voice level"), inRow);
    root->addLayout(form);

    m_howTo = new QLabel(this);
    m_howTo->setWordWrap(true);
    m_howTo->setStyleSheet(QStringLiteral("color:%1; font-size:12px;").arg(tk.ink60.name()));
    root->addWidget(m_howTo);

    auto *bb = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::accept);
    root->addWidget(bb);

    // ── Live edits ────────────────────────────────────────────────────
    connect(m_device, &QComboBox::currentIndexChanged, this, [this, mr] {
        mr->setDeviceId(m_device->currentData().toByteArray());
        refresh();
    });
    connect(m_monitor, &QCheckBox::toggled, mr, &MicRouting::setMonitor);
    connect(m_sfxGain, &QSlider::valueChanged, this, [this, mr](int v) {
        m_sfxGainVal->setText(dbText(v));
        mr->setSfxGainDb(v);
    });
    connect(m_passthrough, &QCheckBox::toggled, this, [this, mr](bool on) {
        // First switch-on: remember the microphone shown in the picker.
        if (on && mr->inputDeviceId().isEmpty())
            mr->setInputDeviceId(m_input->currentData().toByteArray());
        mr->setPassthrough(on);
        refresh();
    });
    connect(m_input, &QComboBox::currentIndexChanged, this, [this, mr] {
        mr->setInputDeviceId(m_input->currentData().toByteArray());
    });
    connect(m_inputGain, &QSlider::valueChanged, this, [this, mr](int v) {
        m_inputGainVal->setText(dbText(v));
        mr->setInputGainDb(v);
    });
    refresh();
    resize(520, sizeHint().height());
}

void MicRoutingDialog::refresh()
{
    auto *mr = MicRouting::instance();
    const bool on = mr->enabled();
    m_monitor->setEnabled(on);
    m_sfxGain->setEnabled(on);
    m_passthrough->setEnabled(on);
    m_input->setEnabled(on && mr->passthrough());
    m_inputGain->setEnabled(on && mr->passthrough());

    const bool sameAsBoard = on && mr->deviceId() == m_boardOutput && !m_boardOutput.isEmpty();
    m_warning->setText(sameAsBoard
        ? tr("That's the soundboard's own output. Pick the virtual cable here, "
             "and keep the soundboard's Output on your speakers or headphones.")
        : QString());
    m_warning->setVisible(sameAsBoard);

    if (!on) {
        m_howTo->setText(tr("Pick the virtual cable above to start."));
    } else if (mr->passthrough()) {
        m_howTo->setText(tr(
            "In Discord, OBS or your game, set the microphone to the cable's "
            "recording side (for VB-Audio: “CABLE Output”). Your voice now "
            "goes through quewi, so don't pick your real mic there as well, "
            "or you'll be heard twice. Keep quewi open while you talk."));
    } else {
        m_howTo->setText(tr(
            "In Discord, OBS or your game, pick the cable's recording side "
            "(for VB-Audio: “CABLE Output”) wherever you want the sounds heard. "
            "To have your voice come through that same mic, tick "
            "“Mix in my microphone”."));
    }
}

} // namespace quewi::ui
