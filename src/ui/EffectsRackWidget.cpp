#include "ui/EffectsRackWidget.h"
#include "audio/AudioEffect.h"
#include "audio/effects/EqEffect.h"
#include "ui/ParametricEqDialog.h"

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
#include <QToolButton>

namespace quewi::ui {

EffectsRackWidget::EffectsRackWidget(QWidget *parent) : QWidget(parent) {
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(4, 4, 4, 4);
    outer->setSpacing(4);

    auto *header = new QWidget(this);
    auto *hl = new QHBoxLayout(header);
    hl->setContentsMargins(0,0,0,0);
    hl->addWidget(new QLabel(tr("Effects Rack"), this));
    hl->addStretch();
    auto *addBtn = new QPushButton(tr("+ Add"), this);
    addBtn->setFixedWidth(60);
    hl->addWidget(addBtn);
    connect(addBtn, &QPushButton::clicked, this, &EffectsRackWidget::addEffect);
    outer->addWidget(header);

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *content = new QWidget(scroll);
    m_effectsLayout = new QVBoxLayout(content);
    m_effectsLayout->setContentsMargins(0,0,0,0);
    m_effectsLayout->setSpacing(4);
    m_effectsLayout->addStretch(1);
    scroll->setWidget(content);
    outer->addWidget(scroll, 1);
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
    if (!m_track) return;
    const auto &fxList = m_track->effects();
    for (int i = 0; i < int(fxList.size()); ++i) {
        m_effectsLayout->insertWidget(i, buildEffectRow(fxList[i].get(), i));
    }
}

QWidget *EffectsRackWidget::buildEffectRow(audio::AudioEffect *fx, int index) {
    auto *box = new QGroupBox(this);
    auto *vl = new QVBoxLayout(box);
    vl->setSpacing(4);

    // Header row: enable checkbox + name + remove button
    auto *hdr = new QWidget(box);
    auto *hl = new QHBoxLayout(hdr);
    hl->setContentsMargins(0,0,0,0);

    auto *enableCheck = new QCheckBox(fx->name(), hdr);
    enableCheck->setChecked(fx->isEnabled());
    connect(enableCheck, &QCheckBox::toggled, fx, &audio::AudioEffect::setEnabled);
    hl->addWidget(enableCheck, 1);

    // EQ gets a visual editor button
    if (fx->type() == audio::AudioEffect::Type::Eq) {
        auto *editBtn = new QPushButton(tr("Edit…"), hdr);
        editBtn->setFixedHeight(22);
        connect(editBtn, &QPushButton::clicked, this, [this, fx]{
            auto *dlg = new ParametricEqDialog(static_cast<audio::EqEffect*>(fx), this->window());
            dlg->show();
        });
        hl->addWidget(editBtn);
    }

    auto *removeBtn = new QToolButton(hdr);
    removeBtn->setText(QStringLiteral("✕"));
    removeBtn->setFixedSize(20, 20);
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
