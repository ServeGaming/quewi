#include "ui/EffectsRackWidget.h"
#include "audio/AudioEffect.h"
#include "audio/effects/EqEffect.h"
#include "audio/effects/CompressorEffect.h"
#include "ui/ParametricEqDialog.h"
#include "ui/CompressorDialog.h"
#include "ui/Theme.h"

#include <QApplication>
#include <QCheckBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSlider>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

namespace quewi::ui {

namespace {

// Distinct accent per effect type — the same hues the visual editors use,
// so a card reads as the same "instrument" as its editor.
QColor fxAccent(audio::AudioEffect::Type t) {
    using T = audio::AudioEffect::Type;
    switch (t) {
    case T::Eq:         return QColor(0x5b, 0x9d, 0xff); // blue
    case T::Compressor: return QColor(0x4f, 0xd1, 0xc5); // teal
    case T::Reverb:     return QColor(0xb3, 0x8b, 0xff); // violet
    case T::Delay:      return QColor(0xff, 0xb0, 0x5a); // amber
    }
    return QColor(0x8a, 0x91, 0x9e);
}

bool fxHasVisualEditor(audio::AudioEffect::Type t) {
    return t == audio::AudioEffect::Type::Eq
        || t == audio::AudioEffect::Type::Compressor;
}

QString fxTagline(audio::AudioEffect::Type t) {
    using T = audio::AudioEffect::Type;
    switch (t) {
    case T::Eq:         return EffectsRackWidget::tr("6-band parametric frequency shaping");
    case T::Compressor: return EffectsRackWidget::tr("Dynamics — tame peaks, even out levels");
    case T::Reverb:     return EffectsRackWidget::tr("Spatial ambience and room tone");
    case T::Delay:      return EffectsRackWidget::tr("Echo and rhythmic repeats");
    }
    return QString();
}

} // namespace

EffectsRackWidget::EffectsRackWidget(QWidget *parent) : QWidget(parent) {
    setObjectName(QStringLiteral("effectsRack"));
    const auto &tk = Theme::tokens();

    setStyleSheet(QStringLiteral(
        // Add button — accent-filled pill.
        "QPushButton#fxAddButton {"
        "  background:%1; color:%2; border:none; border-radius:6px;"
        "  padding:7px 16px; font-size:12px; font-weight:600; min-height:18px; }"
        "QPushButton#fxAddButton:hover  { background:%3; }"
        "QPushButton#fxAddButton:pressed{ background:%4; }"
        // Effect card surface.
        "QFrame#fxCard { background:%5; border:1px solid %6; border-radius:8px; }"
        // Remove (×) button on a card.
        "QToolButton#fxClose { background:transparent; border:none; color:%7;"
        "  min-width:22px; max-width:22px; min-height:22px; max-height:22px; }"
        "QToolButton#fxClose:hover { color:%2; }"
        // Slider — slim track, round handle.
        "QFrame#fxCard QSlider::groove:horizontal { height:4px; background:%6;"
        "  border-radius:2px; }"
        "QFrame#fxCard QSlider::sub-page:horizontal { background:%8;"
        "  border-radius:2px; }"
        "QFrame#fxCard QSlider::handle:horizontal { background:%2; width:13px;"
        "  height:13px; margin:-5px 0; border-radius:6px; }"
        "QFrame#fxCard QSlider::handle:horizontal:hover { background:%8; }")
        .arg(tk.bgInteractive.name(),  // %1 add bg
             tk.ink100.name(),         // %2 text / handle
             tk.bgRowHover.name(),     // %3 add hover
             tk.bgPanel.name(),        // %4 add pressed
             tk.bgPanel.name(),        // %5 card bg
             tk.outline.name(),        // %6 borders / groove
             tk.ink40.name(),          // %7 close idle
             tk.accent.name()));       // %8 slider fill

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(16, 14, 16, 16);
    outer->setSpacing(12);

    // ── Header strip: track context + Add ──────────────────────────────
    auto *header = new QHBoxLayout();
    header->setSpacing(10);
    m_trackLabel = new QLabel(tr("No track selected"), this);
    m_trackLabel->setStyleSheet(QStringLiteral(
        "color:%1; font-size:12px; font-weight:700; letter-spacing:0.12em;")
        .arg(tk.ink60.name()));
    header->addWidget(m_trackLabel, 1);

    auto *addBtn = new QPushButton(tr("+   Add Effect"), this);
    addBtn->setObjectName(QStringLiteral("fxAddButton"));
    addBtn->setCursor(Qt::PointingHandCursor);
    connect(addBtn, &QPushButton::clicked, this, &EffectsRackWidget::addEffect);
    header->addWidget(addBtn, 0);
    outer->addLayout(header);

    // ── Horizontal strip of cards ──────────────────────────────────────
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setStyleSheet(QStringLiteral("QScrollArea{background:transparent;}"
                                         "QScrollArea > QWidget > QWidget{background:transparent;}"));
    auto *content = new QWidget(scroll);
    m_cardsLayout = new QHBoxLayout(content);
    m_cardsLayout->setContentsMargins(0, 0, 0, 0);
    m_cardsLayout->setSpacing(12);
    m_cardsLayout->addStretch(1);
    scroll->setWidget(content);
    outer->addWidget(scroll, 1);
}

void EffectsRackWidget::setTrack(audio::AudioEditorTrack *track) {
    if (m_track) disconnect(m_track, &audio::AudioEditorTrack::changed,
                            this, &EffectsRackWidget::rebuild);
    m_track = track;
    if (m_track) connect(m_track, &audio::AudioEditorTrack::changed,
                         this, &EffectsRackWidget::rebuild);
    rebuild();
}

void EffectsRackWidget::rebuild() {
    // Drop every card (keep the trailing stretch at the end).
    while (m_cardsLayout->count() > 1) {
        auto *item = m_cardsLayout->takeAt(0);
        if (auto *w = item->widget()) w->deleteLater();
        delete item;
    }

    if (m_trackLabel)
        m_trackLabel->setText(m_track ? tr("TRACK · %1").arg(m_track->name())
                                      : tr("NO TRACK SELECTED"));

    if (!m_track) {
        m_cardsLayout->insertWidget(0, buildPlaceholder(
            tr("No track selected"),
            tr("Pick a track in the timeline to edit its effects.")));
        return;
    }

    const auto &fxList = m_track->effects();
    if (fxList.empty()) {
        m_cardsLayout->insertWidget(0, buildPlaceholder(
            tr("No effects on this track"),
            tr("Add EQ, compression, reverb or delay to shape this audio.")));
        return;
    }
    for (int i = 0; i < int(fxList.size()); ++i)
        m_cardsLayout->insertWidget(i, buildEffectCard(fxList[i].get(), i));
}

QWidget *EffectsRackWidget::buildPlaceholder(const QString &title,
                                             const QString &subtitle) {
    const auto &tk = Theme::tokens();
    auto *box = new QFrame(this);
    box->setMinimumHeight(150);
    box->setMinimumWidth(480);
    box->setStyleSheet(QStringLiteral(
        "QFrame{border:1px dashed %1; border-radius:8px; background:transparent;}")
        .arg(tk.outline.name()));
    auto *v = new QVBoxLayout(box);
    v->setContentsMargins(28, 20, 28, 20);
    v->setSpacing(6);
    v->addStretch(1);
    auto *t = new QLabel(title, box);
    t->setAlignment(Qt::AlignCenter);
    t->setStyleSheet(QStringLiteral("border:none; color:%1; font-size:14px; font-weight:600;")
                         .arg(tk.ink100.name()));
    auto *s = new QLabel(subtitle, box);
    s->setAlignment(Qt::AlignCenter);
    s->setWordWrap(true);
    s->setStyleSheet(QStringLiteral("border:none; color:%1; font-size:12px;")
                         .arg(tk.ink40.name()));
    v->addWidget(t);
    v->addWidget(s);
    v->addStretch(1);
    return box;
}

QWidget *EffectsRackWidget::buildEffectCard(audio::AudioEffect *fx, int index) {
    const auto &tk = Theme::tokens();
    const QColor accent = fxAccent(fx->type());

    auto *card = new QFrame(this);
    card->setObjectName(QStringLiteral("fxCard"));
    card->setFixedWidth(258);

    auto *cv = new QVBoxLayout(card);
    cv->setContentsMargins(0, 0, 0, 0);
    cv->setSpacing(0);

    // Accent stripe across the top of the card.
    auto *stripe = new QFrame(card);
    stripe->setFixedHeight(3);
    stripe->setStyleSheet(QStringLiteral(
        "background:%1; border-top-left-radius:7px; border-top-right-radius:7px;")
        .arg(accent.name()));
    cv->addWidget(stripe);

    auto *inner = new QWidget(card);
    auto *iv = new QVBoxLayout(inner);
    iv->setContentsMargins(16, 14, 16, 16);
    iv->setSpacing(10);

    // ── Header: name + bypass + remove ─────────────────────────────────
    auto *hdr = new QHBoxLayout();
    hdr->setSpacing(8);
    auto *name = new QLabel(fx->name(), inner);
    name->setStyleSheet(QStringLiteral("color:%1; font-size:14px; font-weight:700;")
                            .arg(tk.ink100.name()));
    hdr->addWidget(name, 1);

    auto *power = new QCheckBox(inner);
    power->setChecked(fx->isEnabled());
    power->setCursor(Qt::PointingHandCursor);
    power->setToolTip(tr("Bypass this effect"));
    connect(power, &QCheckBox::toggled, fx, &audio::AudioEffect::setEnabled);
    hdr->addWidget(power, 0);

    auto *close = new QToolButton(inner);
    close->setObjectName(QStringLiteral("fxClose"));
    close->setIcon(qApp->style()->standardIcon(QStyle::SP_TitleBarCloseButton));
    close->setCursor(Qt::PointingHandCursor);
    close->setToolTip(tr("Remove effect"));
    connect(close, &QToolButton::clicked, this, [this, index] {
        if (m_track) m_track->removeEffect(index);
    });
    hdr->addWidget(close, 0);
    iv->addLayout(hdr);

    // ── One-line description ───────────────────────────────────────────
    auto *tag = new QLabel(fxTagline(fx->type()), inner);
    tag->setWordWrap(true);
    tag->setStyleSheet(QStringLiteral("color:%1; font-size:11px;").arg(tk.ink40.name()));
    iv->addWidget(tag);

    // ── Body: visual-editor launcher, or labelled sliders ──────────────
    if (fxHasVisualEditor(fx->type())) {
        iv->addStretch(1);
        auto *open = new QPushButton(tr("Open Editor"), inner);
        open->setCursor(Qt::PointingHandCursor);
        open->setMinimumHeight(36);
        open->setStyleSheet(QStringLiteral(
            "QPushButton { background:transparent; color:%1; border:1px solid %1;"
            "  border-radius:6px; font-size:12px; font-weight:600; }"
            "QPushButton:hover  { background:%1; color:%2; }"
            "QPushButton:pressed{ background:%3; }")
            .arg(accent.name(), Theme::tokens().bgDeep.name(),
                 accent.darker(120).name()));
        connect(open, &QPushButton::clicked, this, [this, fx] { openEditor(fx); });
        iv->addWidget(open);
    } else {
        for (const QString &id : fx->parameterIds())
            iv->addWidget(buildParamRow(fx, id, inner));
        iv->addStretch(1);
    }

    cv->addWidget(inner, 1);
    return card;
}

QWidget *EffectsRackWidget::buildParamRow(audio::AudioEffect *fx,
                                          const QString &id, QWidget *parent) {
    const auto &tk = Theme::tokens();
    auto [lo, hi] = fx->parameterRange(id);
    const float cur = fx->parameterValue(id);
    const int decimals = fx->parameterDecimals(id);
    const int kRes = 1000;
    auto toSlider = [lo, hi, kRes](float v) {
        return (hi > lo) ? int((v - lo) / (hi - lo) * kRes) : 0;
    };

    auto *row = new QWidget(parent);
    auto *v = new QVBoxLayout(row);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(3);

    auto *top = new QHBoxLayout();
    top->setContentsMargins(0, 0, 0, 0);
    auto *lab = new QLabel(fx->parameterLabel(id), row);
    lab->setStyleSheet(QStringLiteral("color:%1; font-size:11px;").arg(tk.ink60.name()));
    auto *val = new QLabel(QString::number(double(cur), 'f', decimals), row);
    val->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    val->setStyleSheet(QStringLiteral(
        "color:%1; font-size:11px; font-family:'JetBrains Mono',monospace;")
        .arg(tk.ink100.name()));
    top->addWidget(lab, 1);
    top->addWidget(val, 0);
    v->addLayout(top);

    auto *slider = new QSlider(Qt::Horizontal, row);
    slider->setRange(0, kRes);
    slider->setValue(toSlider(cur));
    v->addWidget(slider);

    connect(slider, &QSlider::valueChanged, this,
            [fx, id, lo, hi, kRes, val, decimals](int sv) {
        const float value = lo + float(sv) / float(kRes) * (hi - lo);
        val->setText(QString::number(double(value), 'f', decimals));
        fx->setParameterValue(id, value);
    });
    connect(fx, &audio::AudioEffect::parameterChanged, row,
            [id, toSlider, slider, val, decimals](const QString &pid, float value) {
        if (pid != id) return;
        QSignalBlocker b(slider);
        slider->setValue(toSlider(value));
        val->setText(QString::number(double(value), 'f', decimals));
    });
    return row;
}

void EffectsRackWidget::openEditor(audio::AudioEffect *fx) {
    QDialog *dlg = nullptr;
    if (fx->type() == audio::AudioEffect::Type::Eq)
        dlg = new ParametricEqDialog(static_cast<audio::EqEffect *>(fx), window());
    else if (fx->type() == audio::AudioEffect::Type::Compressor)
        dlg = new CompressorDialog(static_cast<audio::CompressorEffect *>(fx), window());
    if (dlg) dlg->show();
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
