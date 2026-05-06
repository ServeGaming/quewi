#include "ui/SpeakerPatchDialog.h"

#include "audio/SpeakerPatch.h"
#include "core/PatchManager.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QVBoxLayout>

namespace quewi::ui {

using core::PatchManager;
using Category = PatchManager::Category;

namespace {
QString defaultLabelFor(int channel)
{
    return SpeakerPatchDialog::tr("Speaker %1").arg(channel + 1);
}
} // namespace

SpeakerPatchDialog::SpeakerPatchDialog(PatchManager *patches, QWidget *parent)
    : QDialog(parent)
    , m_patches(patches)
{
    setWindowTitle(tr("Speaker Patch"));
    resize(820, 480);

    auto *root = new QVBoxLayout(this);

    auto *splitter = new QSplitter(Qt::Horizontal, this);

    // ── Left: list of patches ─────────────────────────────────────────
    auto *left = new QWidget(splitter);
    auto *leftV = new QVBoxLayout(left);
    leftV->setContentsMargins(0, 0, 0, 0);
    leftV->addWidget(new QLabel(tr("Patches"), left));
    m_patchList = new QListWidget(left);
    leftV->addWidget(m_patchList, 1);
    auto *patchBtnRow = new QHBoxLayout();
    m_addPatchBtn    = new QPushButton(tr("Add"),    left);
    m_removePatchBtn = new QPushButton(tr("Remove"), left);
    m_renameBtn      = new QPushButton(tr("Rename"), left);
    patchBtnRow->addWidget(m_addPatchBtn);
    patchBtnRow->addWidget(m_removePatchBtn);
    patchBtnRow->addWidget(m_renameBtn);
    leftV->addLayout(patchBtnRow);
    splitter->addWidget(left);

    // ── Right: editor for the selected patch ──────────────────────────
    auto *right = new QWidget(splitter);
    auto *rightV = new QVBoxLayout(right);
    rightV->setContentsMargins(0, 0, 0, 0);

    auto *templateForm = new QFormLayout();
    m_templateCombo = new QComboBox(right);
    for (const auto &k : audio::templateKeys())
        m_templateCombo->addItem(audio::templateLabel(k), k);
    m_templateCombo->addItem(tr("Custom"), QStringLiteral("custom"));
    templateForm->addRow(tr("Template"), m_templateCombo);
    rightV->addLayout(templateForm);

    auto *speakerSplit = new QSplitter(Qt::Horizontal, right);

    // Speakers list.
    auto *spkPanel = new QWidget(speakerSplit);
    auto *spkV = new QVBoxLayout(spkPanel);
    spkV->setContentsMargins(0, 0, 0, 0);
    spkV->addWidget(new QLabel(tr("Speakers"), spkPanel));
    m_speakerList = new QListWidget(spkPanel);
    spkV->addWidget(m_speakerList, 1);
    auto *spkBtnRow = new QHBoxLayout();
    m_addSpeakerBtn    = new QPushButton(tr("Add"),    spkPanel);
    m_removeSpeakerBtn = new QPushButton(tr("Remove"), spkPanel);
    spkBtnRow->addWidget(m_addSpeakerBtn);
    spkBtnRow->addWidget(m_removeSpeakerBtn);
    spkBtnRow->addStretch(1);
    spkV->addLayout(spkBtnRow);
    speakerSplit->addWidget(spkPanel);

    // Per-speaker editor.
    auto *editPanel = new QGroupBox(tr("Selected speaker"), speakerSplit);
    auto *form = new QFormLayout(editPanel);
    m_spkLabel     = new QLineEdit(editPanel);
    m_spkChannel   = new QSpinBox(editPanel);
    m_spkChannel->setRange(0, 63);
    m_spkAzimuth   = new QDoubleSpinBox(editPanel);
    m_spkAzimuth->setRange(-180.0, 180.0);
    m_spkAzimuth->setSuffix(QStringLiteral("°"));
    m_spkAzimuth->setDecimals(1);
    m_spkAzimuth->setSingleStep(5.0);
    m_spkAzimuth->setToolTip(tr("0° = front · ±90° = sides · 180° = behind"));
    m_spkElevation = new QDoubleSpinBox(editPanel);
    m_spkElevation->setRange(-90.0, 90.0);
    m_spkElevation->setSuffix(QStringLiteral("°"));
    m_spkElevation->setDecimals(1);
    m_spkElevation->setSingleStep(5.0);
    m_spkElevation->setToolTip(tr("0° = ear-level · 45°+ = overhead"));
    m_spkDistance  = new QDoubleSpinBox(editPanel);
    m_spkDistance->setRange(0.1, 50.0);
    m_spkDistance->setDecimals(2);
    m_spkDistance->setSingleStep(0.1);
    m_spkDistance->setSuffix(QStringLiteral(" m"));
    form->addRow(tr("Label"),     m_spkLabel);
    form->addRow(tr("Channel"),   m_spkChannel);
    form->addRow(tr("Azimuth"),   m_spkAzimuth);
    form->addRow(tr("Elevation"), m_spkElevation);
    form->addRow(tr("Distance"),  m_spkDistance);
    speakerSplit->addWidget(editPanel);
    speakerSplit->setStretchFactor(0, 1);
    speakerSplit->setStretchFactor(1, 1);

    rightV->addWidget(speakerSplit, 1);
    splitter->addWidget(right);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 3);

    root->addWidget(splitter, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    root->addWidget(buttons);

    // ── Wiring ────────────────────────────────────────────────────────
    connect(m_patchList,      &QListWidget::currentItemChanged,
            this,             &SpeakerPatchDialog::onPatchSelected);
    connect(m_addPatchBtn,    &QPushButton::clicked, this, &SpeakerPatchDialog::onAddPatchClicked);
    connect(m_removePatchBtn, &QPushButton::clicked, this, &SpeakerPatchDialog::onRemovePatchClicked);
    connect(m_renameBtn,      &QPushButton::clicked, this, &SpeakerPatchDialog::onRenameClicked);
    connect(m_templateCombo,  qOverload<int>(&QComboBox::activated),
            this,             &SpeakerPatchDialog::onTemplateChanged);

    connect(m_speakerList,      &QListWidget::currentItemChanged,
            this,               &SpeakerPatchDialog::onSpeakerSelected);
    connect(m_addSpeakerBtn,    &QPushButton::clicked, this, &SpeakerPatchDialog::onAddSpeakerClicked);
    connect(m_removeSpeakerBtn, &QPushButton::clicked, this, &SpeakerPatchDialog::onRemoveSpeakerClicked);

    connect(m_spkLabel,     &QLineEdit::textEdited,        this, &SpeakerPatchDialog::onSpeakerEdited);
    connect(m_spkChannel,   qOverload<int>(&QSpinBox::valueChanged),
            this,           &SpeakerPatchDialog::onSpeakerEdited);
    connect(m_spkAzimuth,   qOverload<double>(&QDoubleSpinBox::valueChanged),
            this,           &SpeakerPatchDialog::onSpeakerEdited);
    connect(m_spkElevation, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this,           &SpeakerPatchDialog::onSpeakerEdited);
    connect(m_spkDistance,  qOverload<double>(&QDoubleSpinBox::valueChanged),
            this,           &SpeakerPatchDialog::onSpeakerEdited);

    rebuildPatchList();
    setEditableEnabled(false);
}

void SpeakerPatchDialog::rebuildPatchList()
{
    m_suppressSignals = true;
    m_patchList->clear();
    if (!m_patches) { m_suppressSignals = false; return; }

    int selectRow = -1;
    int row = 0;
    for (const auto &p : m_patches->patchesIn(Category::SpeakerArray)) {
        auto *item = new QListWidgetItem(p.name, m_patchList);
        item->setData(Qt::UserRole, p.id);
        if (p.id == m_currentPatchId) selectRow = row;
        ++row;
    }
    if (selectRow >= 0) m_patchList->setCurrentRow(selectRow);
    else if (m_patchList->count() > 0) m_patchList->setCurrentRow(0);

    m_suppressSignals = false;
    onPatchSelected();
}

void SpeakerPatchDialog::onPatchSelected()
{
    if (m_suppressSignals) return;
    auto *item = m_patchList->currentItem();
    if (!item || !m_patches) {
        m_currentPatchId = QUuid();
        m_workingSpeakers.clear();
        rebuildSpeakerList();
        setEditableEnabled(false);
        return;
    }
    m_currentPatchId = item->data(Qt::UserRole).toUuid();
    m_workingSpeakers = audio::readSpeakers(m_patches, m_currentPatchId);

    const auto patch = m_patches->patch(m_currentPatchId);
    const auto tmpl  = patch.fields.value(QStringLiteral("templateKey"),
                                          QStringLiteral("custom")).toString();
    m_suppressSignals = true;
    int idx = m_templateCombo->findData(tmpl);
    if (idx < 0) idx = m_templateCombo->findData(QStringLiteral("custom"));
    if (idx >= 0) m_templateCombo->setCurrentIndex(idx);
    m_suppressSignals = false;

    rebuildSpeakerList();
    setEditableEnabled(true);
}

void SpeakerPatchDialog::onAddPatchClicked()
{
    if (!m_patches) return;
    bool ok = false;
    const auto name = QInputDialog::getText(this, tr("New speaker patch"),
        tr("Name:"), QLineEdit::Normal, tr("Stereo rig"), &ok);
    if (!ok || name.isEmpty()) return;

    auto fields = audio::toPatchFields(QStringLiteral("stereo"),
                                       audio::templateSpeakers(QStringLiteral("stereo")));
    m_currentPatchId = m_patches->add(Category::SpeakerArray, name, fields);
    rebuildPatchList();
}

void SpeakerPatchDialog::onRemovePatchClicked()
{
    if (!m_patches || m_currentPatchId.isNull()) return;
    if (QMessageBox::question(this, tr("Remove patch"),
            tr("Delete this speaker patch? Cues that reference it will "
               "fall back to legacy stereo pan."))
        != QMessageBox::Yes) return;
    m_patches->remove(m_currentPatchId);
    m_currentPatchId = QUuid();
    rebuildPatchList();
}

void SpeakerPatchDialog::onRenameClicked()
{
    if (!m_patches || m_currentPatchId.isNull()) return;
    const auto cur = m_patches->nameOf(m_currentPatchId);
    bool ok = false;
    const auto name = QInputDialog::getText(this, tr("Rename patch"),
        tr("Name:"), QLineEdit::Normal, cur, &ok);
    if (!ok || name.isEmpty()) return;
    m_patches->rename(m_currentPatchId, name);
    rebuildPatchList();
}

void SpeakerPatchDialog::onTemplateChanged(int index)
{
    if (m_suppressSignals || m_currentPatchId.isNull()) return;
    const auto key = m_templateCombo->itemData(index).toString();
    if (key == QLatin1String("custom")) {
        // Custom = keep current speakers, just update the templateKey
        // tag so the patch displays as Custom.
        auto fields = m_patches->patch(m_currentPatchId).fields;
        fields[QStringLiteral("templateKey")] = QStringLiteral("custom");
        m_patches->setFields(m_currentPatchId, fields);
        return;
    }
    if (QMessageBox::question(this, tr("Apply template"),
            tr("Replace the current speaker list with the %1 layout?")
                .arg(audio::templateLabel(key)))
        != QMessageBox::Yes) {
        // Revert combo selection.
        m_suppressSignals = true;
        const auto cur = m_patches->patch(m_currentPatchId).fields
                            .value(QStringLiteral("templateKey")).toString();
        int idx = m_templateCombo->findData(cur);
        if (idx >= 0) m_templateCombo->setCurrentIndex(idx);
        m_suppressSignals = false;
        return;
    }
    m_workingSpeakers = audio::templateSpeakers(key);
    auto fields = audio::toPatchFields(key, m_workingSpeakers);
    m_patches->setFields(m_currentPatchId, fields);
    rebuildSpeakerList();
}

void SpeakerPatchDialog::rebuildSpeakerList()
{
    m_suppressSignals = true;
    m_speakerList->clear();
    for (int i = 0; i < m_workingSpeakers.size(); ++i) {
        const auto &s = m_workingSpeakers[i];
        const auto label = tr("Ch %1 — %2°, %3°")
            .arg(s.channel)
            .arg(QString::number(double(s.azimuthDeg), 'f', 0))
            .arg(QString::number(double(s.elevationDeg), 'f', 0));
        auto *item = new QListWidgetItem(label, m_speakerList);
        item->setData(Qt::UserRole, i);
    }
    if (m_speakerList->count() > 0) m_speakerList->setCurrentRow(0);
    m_suppressSignals = false;
    onSpeakerSelected();
}

void SpeakerPatchDialog::onSpeakerSelected()
{
    if (m_suppressSignals) return;
    auto *item = m_speakerList->currentItem();
    const bool valid = item != nullptr;
    m_spkLabel->setEnabled(valid);
    m_spkChannel->setEnabled(valid);
    m_spkAzimuth->setEnabled(valid);
    m_spkElevation->setEnabled(valid);
    m_spkDistance->setEnabled(valid);
    if (!valid) {
        m_suppressSignals = true;
        m_spkLabel->clear();
        m_spkChannel->setValue(0);
        m_spkAzimuth->setValue(0);
        m_spkElevation->setValue(0);
        m_spkDistance->setValue(1.0);
        m_suppressSignals = false;
        return;
    }
    const int idx = item->data(Qt::UserRole).toInt();
    if (idx < 0 || idx >= m_workingSpeakers.size()) return;
    const auto &s = m_workingSpeakers[idx];
    m_suppressSignals = true;
    m_spkLabel->setText(defaultLabelFor(s.channel));   // labels aren't stored yet
    m_spkChannel->setValue(s.channel);
    m_spkAzimuth->setValue(s.azimuthDeg);
    m_spkElevation->setValue(s.elevationDeg);
    m_spkDistance->setValue(s.distance);
    m_suppressSignals = false;
}

void SpeakerPatchDialog::onAddSpeakerClicked()
{
    audio::Speaker s;
    s.channel = static_cast<int>(m_workingSpeakers.size());
    m_workingSpeakers.append(s);
    writeBackSpeakers();
    rebuildSpeakerList();
    m_speakerList->setCurrentRow(m_workingSpeakers.size() - 1);
}

void SpeakerPatchDialog::onRemoveSpeakerClicked()
{
    auto *item = m_speakerList->currentItem();
    if (!item) return;
    const int idx = item->data(Qt::UserRole).toInt();
    if (idx < 0 || idx >= m_workingSpeakers.size()) return;
    m_workingSpeakers.removeAt(idx);
    writeBackSpeakers();
    rebuildSpeakerList();
}

void SpeakerPatchDialog::onSpeakerEdited()
{
    if (m_suppressSignals) return;
    auto *item = m_speakerList->currentItem();
    if (!item) return;
    const int idx = item->data(Qt::UserRole).toInt();
    if (idx < 0 || idx >= m_workingSpeakers.size()) return;

    auto &s = m_workingSpeakers[idx];
    s.channel      = m_spkChannel->value();
    s.azimuthDeg   = static_cast<float>(m_spkAzimuth->value());
    s.elevationDeg = static_cast<float>(m_spkElevation->value());
    s.distance     = static_cast<float>(m_spkDistance->value());

    // Editing a speaker drops the patch to "custom" — the user has
    // diverged from the template, and saying it's still 5.1 would be
    // a lie that confuses the inspector summary.
    auto fields = m_patches->patch(m_currentPatchId).fields;
    fields[QStringLiteral("templateKey")] = QStringLiteral("custom");
    m_patches->setFields(m_currentPatchId, fields);
    writeBackSpeakers();

    // Refresh just the row label.
    const auto label = tr("Ch %1 — %2°, %3°")
        .arg(s.channel)
        .arg(QString::number(double(s.azimuthDeg), 'f', 0))
        .arg(QString::number(double(s.elevationDeg), 'f', 0));
    item->setText(label);

    m_suppressSignals = true;
    int tIdx = m_templateCombo->findData(QStringLiteral("custom"));
    if (tIdx >= 0) m_templateCombo->setCurrentIndex(tIdx);
    m_suppressSignals = false;
}

void SpeakerPatchDialog::writeBackSpeakers()
{
    if (!m_patches || m_currentPatchId.isNull()) return;
    auto fields = m_patches->patch(m_currentPatchId).fields;
    auto repacked = audio::toPatchFields(
        fields.value(QStringLiteral("templateKey")).toString(),
        m_workingSpeakers);
    // Preserve templateKey already in fields (toPatchFields would overwrite it).
    repacked[QStringLiteral("templateKey")] =
        fields.value(QStringLiteral("templateKey")).toString();
    m_patches->setFields(m_currentPatchId, repacked);
}

void SpeakerPatchDialog::setEditableEnabled(bool enabled)
{
    m_templateCombo->setEnabled(enabled);
    m_speakerList->setEnabled(enabled);
    m_addSpeakerBtn->setEnabled(enabled);
    m_removeSpeakerBtn->setEnabled(enabled);
    m_renameBtn->setEnabled(enabled);
    m_removePatchBtn->setEnabled(enabled);
}

QList<audio::Speaker> SpeakerPatchDialog::currentSpeakers() const
{
    return m_workingSpeakers;
}

} // namespace quewi::ui
