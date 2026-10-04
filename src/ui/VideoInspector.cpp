#include "ui/VideoInspector.h"

#include "ui/Theme.h"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFontMetrics>
#include <QFormLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QVBoxLayout>

namespace quewi::ui {

namespace {

constexpr int kHeaderH = 28;
constexpr int kLabelW  = 62;    // the form's label column, the same in every section

QDoubleSpinBox *secondsSpin(QWidget *parent)
{
    auto *s = new QDoubleSpinBox(parent);
    s->setDecimals(2);
    s->setSingleStep(0.1);
    s->setRange(0.0, 24 * 3600.0);
    s->setSuffix(QStringLiteral(" s"));
    s->setKeyboardTracking(false);
    s->setAccelerated(true);
    return s;
}

QLabel *readout(QWidget *parent, bool primary)
{
    const auto &tk = Theme::tokens();
    auto *l = new QLabel(parent);
    l->setStyleSheet(QStringLiteral(
        "color:%1; font-family:'Space Grotesk','JetBrains Mono',monospace; font-size:%2px;"
        " font-weight:%3; padding:0 2px;")
        .arg((primary ? tk.ink100 : tk.ink60).name()).arg(primary ? 13 : 11).arg(primary ? 600 : 400));
    l->setMinimumHeight(26);
    return l;
}

} // namespace

// ── InspectorSection ───────────────────────────────────────────────────────

class InspectorSection::Header : public QAbstractButton {
public:
    explicit Header(const QString &title, QWidget *parent) : QAbstractButton(parent), m_title(title)
    {
        setCheckable(true);
        setChecked(true);
        setFixedHeight(kHeaderH);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }
    void setNote(const QString &n) { m_note = n; update(); }

protected:
    void paintEvent(QPaintEvent *) override
    {
        const auto &tk = Theme::tokens();
        QPainter p(this);
        p.fillRect(rect(), underMouse() ? tk.bgRowHover : tk.bgPanel);
        p.setPen(tk.divider);
        p.drawLine(0, height() - 1, width(), height() - 1);

        // Disclosure arrow: right when folded, down when open.
        p.setRenderHint(QPainter::Antialiasing, true);
        QPainterPath tri;
        const QPointF c(16, height() / 2.0);
        if (isChecked()) {
            tri.moveTo(c.x() - 4, c.y() - 2); tri.lineTo(c.x() + 4, c.y() - 2); tri.lineTo(c.x(), c.y() + 3);
        } else {
            tri.moveTo(c.x() - 2, c.y() - 4); tri.lineTo(c.x() + 3, c.y()); tri.lineTo(c.x() - 2, c.y() + 4);
        }
        tri.closeSubpath();
        p.fillPath(tri, tk.ink40);
        p.setRenderHint(QPainter::Antialiasing, false);

        QFont f = font();
        f.setPixelSize(10);
        f.setBold(true);
        f.setLetterSpacing(QFont::PercentageSpacing, 118);
        p.setFont(f);
        p.setPen(tk.ink60);
        p.drawText(QRect(28, 0, width() - 40, height()), Qt::AlignLeft | Qt::AlignVCenter, m_title);

        if (!m_note.isEmpty()) {
            QFont mono(QStringLiteral("Space Grotesk"));
            mono.setPixelSize(10);
            p.setFont(mono);
            p.setPen(tk.ink40);
            p.drawText(QRect(0, 0, width() - 14, height()), Qt::AlignRight | Qt::AlignVCenter, m_note);
        }
    }
    void enterEvent(QEnterEvent *) override { update(); }
    void leaveEvent(QEvent *) override { update(); }

private:
    QString m_title;
    QString m_note;
};

InspectorSection::InspectorSection(const QString &title, QWidget *parent) : QWidget(parent)
{
    const auto &tk = Theme::tokens();
    auto *vl = new QVBoxLayout(this);
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(0);

    m_header = new Header(title, this);
    vl->addWidget(m_header);

    m_body = new QWidget(this);
    m_body->setObjectName(QStringLiteral("inspectorSectionBody"));
    m_body->setStyleSheet(QStringLiteral(
        "QWidget#inspectorSectionBody { background:%1; border-bottom:1px solid %2; }")
        .arg(tk.bgDeep.name(), tk.divider.name()));
    m_bodyLayout = new QVBoxLayout(m_body);
    m_bodyLayout->setContentsMargins(14, 8, 14, 10);
    m_bodyLayout->setSpacing(6);
    m_form = new QFormLayout();
    m_form->setContentsMargins(0, 0, 0, 0);
    m_form->setHorizontalSpacing(10);
    m_form->setVerticalSpacing(4);
    m_form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_form->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
    m_form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    m_form->setRowWrapPolicy(QFormLayout::DontWrapRows);
    m_bodyLayout->addLayout(m_form);
    vl->addWidget(m_body);

    connect(m_header, &QAbstractButton::toggled, m_body, &QWidget::setVisible);
}

void InspectorSection::setNote(const QString &note) { m_header->setNote(note); }
void InspectorSection::setExpanded(bool on) { m_header->setChecked(on); }
bool InspectorSection::isExpanded() const { return m_header->isChecked(); }

QLabel *InspectorSection::makeLabel(const QString &text)
{
    const auto &tk = Theme::tokens();
    auto *l = new QLabel(text, m_body);
    l->setStyleSheet(QStringLiteral("color:%1; font-size:11px;").arg(tk.ink60.name()));
    l->setMinimumWidth(kLabelW);
    l->setMinimumHeight(26);
    return l;
}

void InspectorSection::addRow(const QString &label, QWidget *field)
{
    m_form->addRow(makeLabel(label), field);
}

// ── VideoInspector ─────────────────────────────────────────────────────────

VideoInspector::VideoInspector(QWidget *parent) : QWidget(parent)
{
    const auto &tk = Theme::tokens();
    setObjectName(QStringLiteral("videoInspector"));
    setStyleSheet(QStringLiteral("QWidget#videoInspector { background:%1; border-left:1px solid %2; }")
                      .arg(tk.bgDeep.name(), tk.divider.name()));
    setFixedWidth(300);

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(1, 0, 0, 0);
    outer->setSpacing(0);

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->viewport()->setStyleSheet(QStringLiteral("background:%1;").arg(tk.bgDeep.name()));
    outer->addWidget(scroll);

    auto *column = new QWidget(scroll);
    column->setStyleSheet(QStringLiteral("background:%1;").arg(tk.bgDeep.name()));
    auto *vl = new QVBoxLayout(column);
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(0);

    // ── Clip ───────────────────────────────────────────────────────────
    clipSection = new InspectorSection(tr("CLIP"), column);
    inSpin = secondsSpin(clipSection->body());
    outSpin = secondsSpin(clipSection->body());
    outSpin->setSpecialValueText(tr("End"));   // 0 = the end of the file
    playsForLabel = readout(clipSection->body(), true);
    cutsLabel = readout(clipSection->body(), false);
    loopCheck = new QCheckBox(tr("Loop between In and Out"), clipSection->body());
    clipSection->addRow(tr("In"), inSpin);
    clipSection->addRow(tr("Out"), outSpin);
    clipSection->addRow(tr("Plays for"), playsForLabel);
    clipSection->addRow(tr("Cuts"), cutsLabel);
    clipSection->form()->setRowVisible(cutsLabel, false);   // until there are cuts
    clipSection->form()->addRow(clipSection->makeLabel(QString()), loopCheck);
    vl->addWidget(clipSection);

    // ── Video ──────────────────────────────────────────────────────────
    videoSection = new InspectorSection(tr("VIDEO"), column);
    opacitySpin = new QDoubleSpinBox(videoSection->body());
    opacitySpin->setRange(0.0, 1.0);
    opacitySpin->setSingleStep(0.05);
    opacitySpin->setDecimals(2);
    opacitySpin->setKeyboardTracking(false);
    picFadeInSpin = secondsSpin(videoSection->body());
    picFadeOutSpin = secondsSpin(videoSection->body());
    videoSection->addRow(tr("Opacity"), opacitySpin);
    videoSection->addRow(tr("Fade in"), picFadeInSpin);
    videoSection->addRow(tr("Fade out"), picFadeOutSpin);
    vl->addWidget(videoSection);

    // ── Audio ──────────────────────────────────────────────────────────
    audioSection = new InspectorSection(tr("AUDIO"), column);
    soundCheck = new QCheckBox(tr("Play the video's sound"), audioSection->body());
    levelSpin = new QDoubleSpinBox(audioSection->body());
    levelSpin->setRange(-60.0, 12.0);
    levelSpin->setDecimals(1);
    levelSpin->setSingleStep(0.5);
    levelSpin->setSuffix(QStringLiteral(" dB"));
    levelSpin->setKeyboardTracking(false);
    sndFadeInSpin = secondsSpin(audioSection->body());
    sndFadeOutSpin = secondsSpin(audioSection->body());
    editSoundBtn = new QPushButton(tr("Edit sound…"), audioSection->body());
    editSoundBtn->setToolTip(tr("Open the soundtrack in the audio editor: effects, EQ, "
                                "compressor, presets"));
    audioSection->form()->addRow(audioSection->makeLabel(QString()), soundCheck);
    audioSection->addRow(tr("Level"), levelSpin);
    audioSection->addRow(tr("Fade in"), sndFadeInSpin);
    audioSection->addRow(tr("Fade out"), sndFadeOutSpin);
    audioSection->bodyLayout()->addWidget(editSoundBtn);
    vl->addWidget(audioSection);

    // ── Lighting ───────────────────────────────────────────────────────
    lightingSection = new InspectorSection(tr("LIGHTING"), column);
    triggersLabel = readout(lightingSection->body(), false);
    lightingBtn = new QPushButton(tr("Lighting triggers…"), lightingSection->body());
    lightingBtn->setToolTip(tr("Lighting triggers on this video's soundtrack"));
    lightingSection->addRow(tr("Triggers"), triggersLabel);
    lightingSection->bodyLayout()->addWidget(lightingBtn);
    vl->addWidget(lightingSection);

    vl->addStretch(1);
    scroll->setWidget(column);
}

} // namespace quewi::ui
