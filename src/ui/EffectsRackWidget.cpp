#include "ui/EffectsRackWidget.h"
#include "audio/AudioEffect.h"
#include "audio/effects/EqEffect.h"
#include "audio/effects/CompressorEffect.h"
#include "ui/ParametricEqDialog.h"
#include "ui/CompressorDialog.h"
#include "ui/Theme.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QStyle>
#include <QToolButton>

namespace quewi::ui {

EffectsRackWidget::EffectsRackWidget(QWidget *parent) : QWidget(parent) {
    setObjectName(QStringLiteral("effectsRack"));
    // Internal styling — kept self-contained so this widget looks
    // consistent whether hosted inline in AudioEditorWindow or torn
    // off into a stand-alone panel.
    const auto &tk = Theme::tokens();
    setStyleSheet(QStringLiteral(
        // Add button — small accent pill on the right of the toolbar.
        "QPushButton#fxAddButton {"
        "    background: %1; color: %2;"
        "    border: 1px solid %3; border-radius: 4px;"
        "    padding: 4px 12px; font-size: 12px; min-height: 22px;"
        "}"
        "QPushButton#fxAddButton:hover { background: %4; }"
        "QPushButton#fxAddButton:pressed { background: %5; }"
        // Effect row container — flat surface with subtle border,
        // replaces the ugly QGroupBox frame that hung around an
        // empty title.
        "QFrame#fxRow {"
        "    background: %5; border: 1px solid %3;"
        "    border-radius: 6px;"
        "}"
        // Edit / close — fixed height so they line up with the
        // enable checkbox baseline.
        "QPushButton#fxEditBtn, QToolButton#fxCloseBtn {"
        "    min-height: 24px; max-height: 24px;"
        "}"
        "QPushButton#fxEditBtn {"
        "    background: transparent; color: %6;"
        "    border: 1px solid %3; border-radius: 4px;"
        "    padding: 0 10px;"
        "}"
        "QPushButton#fxEditBtn:hover { color: %2; border-color: %7; }"
        "QToolButton#fxCloseBtn {"
        "    background: transparent; border: none;"
        "    color: %8; min-width: 24px; max-width: 24px;"
        "}"
        "QToolButton#fxCloseBtn:hover { color: %2; }"
        // Empty-state hint.
        "QLabel#fxEmptyHint {"
        "    color: %8; font-size: 12px; font-style: italic;"
        "}")
        .arg(tk.bgInteractive.name(),  // %1 add button bg
             tk.ink100.name(),         // %2 primary text
             tk.outline.name(),        // %3 borders
             tk.bgRowHover.name(),     // %4 add button hover
             tk.bgPanel.name(),        // %5 row bg / pressed
             tk.ink60.name(),          // %6 edit button text
             tk.outlineFocus.name(),   // %7 edit hover border (amber)
             tk.ink40.name()));        // %8 close button + empty hint

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(12, 12, 12, 12);
    outer->setSpacing(8);

    // Compact toolbar — track name on the left (filled in by the
    // host), Add button on the right. No more redundant "Effects Rack"
    // header that just repeated the tab title.
    auto *header = new QWidget(this);
    auto *hl = new QHBoxLayout(header);
    hl->setContentsMargins(0, 0, 0, 0);
    hl->setSpacing(8);
    m_trackLabel = new QLabel(tr("No track selected"), this);
    m_trackLabel->setStyleSheet(QStringLiteral(
        "color: %1; font-size: 11px; "
        "font-weight: 600; letter-spacing: 1px;")
        .arg(tk.ink40.name()));
    hl->addWidget(m_trackLabel, 1);
    auto *addBtn = new QPushButton(tr("Add effect"), this);
    addBtn->setObjectName(QStringLiteral("fxAddButton"));
    addBtn->setCursor(Qt::PointingHandCursor);
    hl->addWidget(addBtn);
    connect(addBtn, &QPushButton::clicked, this, &EffectsRackWidget::addEffect);
    outer->addWidget(header);

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *content = new QWidget(scroll);
    m_effectsLayout = new QVBoxLayout(content);
    m_effectsLayout->setContentsMargins(0, 0, 0, 0);
    m_effectsLayout->setSpacing(8);
    m_effectsLayout->addStretch(1);
    scroll->setWidget(content);
    outer->addWidget(scroll, 1);

    // Empty hint — sits above the stretch so it appears centered-ish
    // when the rack is empty. rebuild() toggles its visibility.
    m_emptyHint = new QLabel(
        tr("No effects yet — click \"Add effect\" to insert one."), this);
    m_emptyHint->setObjectName(QStringLiteral("fxEmptyHint"));
    m_emptyHint->setAlignment(Qt::AlignCenter);
    m_emptyHint->setVisible(false);
    outer->addWidget(m_emptyHint, 0);
}

void EffectsRackWidget::setTrack(audio::AudioEditorTrack *track) {
    if (m_track) disconnect(m_track, &audio::AudioEditorTrack::changed, this, &EffectsRackWidget::rebuild);
    m_track = track;
    if (m_track) connect(m_track, &audio::AudioEditorTrack::changed, this, &EffectsRackWidget::rebuild);
    rebuild();
}

void EffectsRackWidget::rebuild() {
    // Remove all items except the stretch at the end
    while (m_effectsLayout->count() > 1) {
        auto *item = m_effectsLayout->takeAt(0);
        delete item->widget();
        delete item;
    }
    if (m_trackLabel) {
        m_trackLabel->setText(m_track
            ? tr("TRACK · %1").arg(m_track->name())
            : tr("No track selected"));
    }
    const bool hasFx = m_track && !m_track->effects().empty();
    if (m_emptyHint) m_emptyHint->setVisible(m_track && !hasFx);
    if (!m_track) return;
    const auto &fxList = m_track->effects();
    for (int i = 0; i < int(fxList.size()); ++i) {
        m_effectsLayout->insertWidget(i, buildEffectRow(fxList[i].get(), i));
    }
}

QWidget *EffectsRackWidget::buildEffectRow(audio::AudioEffect *fx, int index) {
    // QFrame replaces the old empty-title QGroupBox. The frame border
    // comes from QSS (#fxRow) so the row reads as a single card rather
    // than the chrome-around-nothing the QGroupBox produced.
    auto *box = new QFrame(this);
    box->setObjectName(QStringLiteral("fxRow"));
    auto *vl = new QVBoxLayout(box);
    vl->setContentsMargins(10, 10, 10, 10);
    vl->setSpacing(6);

    // Header row: enable checkbox + name + edit + close. Heights are
    // unified via QSS (#fxEditBtn / #fxCloseBtn) at 24 px so they
    // line up with the checkbox baseline.
    auto *hdr = new QWidget(box);
    auto *hl = new QHBoxLayout(hdr);
    hl->setContentsMargins(0, 0, 0, 0);
    hl->setSpacing(6);

    auto *enableCheck = new QCheckBox(fx->name(), hdr);
    enableCheck->setChecked(fx->isEnabled());
    enableCheck->setStyleSheet(QStringLiteral(
        "QCheckBox { font-weight: 600; }"));
    connect(enableCheck, &QCheckBox::toggled, fx, &audio::AudioEffect::setEnabled);
    hl->addWidget(enableCheck, 1);

    // EQ and Compressor get a visual editor button; the others keep the
    // generic slider rows below as their only UI.
    if (fx->type() == audio::AudioEffect::Type::Eq ||
        fx->type() == audio::AudioEffect::Type::Compressor) {
        auto *editBtn = new QPushButton(tr("Edit…"), hdr);
        editBtn->setObjectName(QStringLiteral("fxEditBtn"));
        editBtn->setCursor(Qt::PointingHandCursor);
        connect(editBtn, &QPushButton::clicked, this, [this, fx]{
            QDialog *dlg = nullptr;
            if (fx->type() == audio::AudioEffect::Type::Eq)
                dlg = new ParametricEqDialog(static_cast<audio::EqEffect*>(fx), this->window());
            else
                dlg = new CompressorDialog(static_cast<audio::CompressorEffect*>(fx), this->window());
            dlg->show();
        });
        hl->addWidget(editBtn);
    }

    auto *removeBtn = new QToolButton(hdr);
    removeBtn->setObjectName(QStringLiteral("fxCloseBtn"));
    // QStyle::SP_TitleBarCloseButton renders consistently across
    // platforms — the raw "✕" glyph the old code used didn't exist
    // in some default Windows fonts and fell back to a placeholder
    // box.
    removeBtn->setIcon(qApp->style()->standardIcon(
        QStyle::SP_TitleBarCloseButton));
    removeBtn->setToolTip(tr("Remove effect"));
    removeBtn->setCursor(Qt::PointingHandCursor);
    connect(removeBtn, &QToolButton::clicked, this, [this, index]{
        if (m_track) { m_track->removeEffect(index); }
    });
    hl->addWidget(removeBtn);
    vl->addWidget(hdr);

    // Parameter sliders
    auto *form = new QWidget(box);
    auto *fl = new QFormLayout(form);
    fl->setContentsMargins(4, 0, 4, 4);
    fl->setSpacing(4);

    for (const QString &id : fx->parameterIds()) {
        auto [lo, hi] = fx->parameterRange(id);
        float cur     = fx->parameterValue(id);
        int decimals  = fx->parameterDecimals(id);

        auto *row = new QWidget(form);
        auto *rowL = new QHBoxLayout(row);
        rowL->setContentsMargins(0,0,0,0);
        rowL->setSpacing(4);

        auto *slider = new QSlider(Qt::Horizontal, row);
        const int resolution = 1000;
        slider->setRange(0, resolution);
        slider->setValue(int((cur - lo) / (hi - lo) * resolution));

        auto *spin = new QDoubleSpinBox(row);
        spin->setRange(double(lo), double(hi));
        spin->setDecimals(decimals);
        spin->setValue(double(cur));
        spin->setFixedWidth(72);

        rowL->addWidget(slider, 1);
        rowL->addWidget(spin);

        connect(slider, &QSlider::valueChanged, this, [fx, id, lo, hi, spin, resolution](int v){
            float val = lo + float(v) / float(resolution) * (hi - lo);
            QSignalBlocker sb(spin);
            spin->setValue(double(val));
            fx->setParameterValue(id, val);
        });
        connect(spin, &QDoubleSpinBox::valueChanged, this, [fx, id, slider, lo, hi, resolution](double v){
            QSignalBlocker sb(slider);
            slider->setValue(int((float(v) - lo) / (hi - lo) * resolution));
            fx->setParameterValue(id, float(v));
        });
        connect(fx, &audio::AudioEffect::parameterChanged, row,
                [fx, id, slider, spin, lo, hi, resolution](const QString &pid, float val){
            if (pid != id) return;
            QSignalBlocker s1(slider), s2(spin);
            slider->setValue(int((val - lo) / (hi - lo) * resolution));
            spin->setValue(double(val));
        });

        fl->addRow(fx->parameterLabel(id), row);
    }
    vl->addWidget(form);
    return box;
}

void EffectsRackWidget::addEffect() {
    if (!m_track) return;
    QMenu menu(this);
    menu.addAction(tr("Parametric EQ"),  this, [this]{ m_track->addEffect(audio::AudioEffect::Type::Eq); });
    menu.addAction(tr("Compressor"),     this, [this]{ m_track->addEffect(audio::AudioEffect::Type::Compressor); });
    menu.addAction(tr("Reverb"),         this, [this]{ m_track->addEffect(audio::AudioEffect::Type::Reverb); });
    menu.addAction(tr("Delay"),          this, [this]{ m_track->addEffect(audio::AudioEffect::Type::Delay); });
    menu.exec(QCursor::pos());
}

} // namespace quewi::ui
