#include "ui/TransportBar.h"

#include "cues/Cue.h"
#include "ui/AnimatedButton.h"
#include "ui/Theme.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>

namespace quewi::ui {

namespace {
// Blend `a` parts of `c` over `base` — used to build the subtle tinted-dark
// fills for the Fade All / Panic chips so they read as filled, intentional
// buttons instead of invisible-body wireframes.
QColor tintOver(const QColor &base, const QColor &c, float a)
{
    return QColor::fromRgbF(base.redF()   * (1 - a) + c.redF()   * a,
                            base.greenF() * (1 - a) + c.greenF() * a,
                            base.blueF()  * (1 - a) + c.blueF()  * a);
}
} // namespace

TransportBar::TransportBar(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("transportBar"));
    // Bumped from 96 to 120: the original min-height + 12-px T/B
    // contentsMargins + Qt's default QPushButton padding/border was
    // squeezing the Pause / Fade All / Panic buttons just enough that
    // the top edge of their rounded corner painted outside the
    // allotted rect on Windows DPI scales — visually clipping the
    // top of the buttons. The extra 24 px gives the AnimatedButton
    // painter (which inset-adjusts by 1 px on the bottom-right for
    // antialiasing) breathing room without floating the bar away
    // from the content above it.
    setMinimumHeight(120);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(20, 16, 20, 16);
    layout->setSpacing(16);

    // Left: NEXT label stack
    auto *nextStack = new QVBoxLayout();
    nextStack->setSpacing(2);
    nextStack->setContentsMargins(0, 0, 0, 0);

    auto *nextHeader = new QLabel(tr("NEXT"), this);
    nextHeader->setObjectName(QStringLiteral("nextHeader"));
    nextStack->addWidget(nextHeader);

    m_nextLabel = new QLabel(tr("—"), this);
    m_nextLabel->setObjectName(QStringLiteral("nextLabel"));
    nextStack->addWidget(m_nextLabel);

    layout->addLayout(nextStack, 1);

    // Right: action buttons, escalating right-to-left.
    // Order (left → right): Pause, Fade All, Panic, GO.
    // The three secondary buttons are AnimatedButtons — the hover and
    // press states animate over ~140 ms which feels more tactile than
    // QSS' instant pseudo-state switch on a 165 Hz monitor.
    auto *pauseBtn = new AnimatedButton(tr("Pause"), this);
    pauseBtn->setMinimumHeight(44);
    pauseBtn->setBorderRadius(6);
    pauseBtn->setColors(QColor(0x34, 0x30, 0x2C),
                        QColor(0x40, 0x3A, 0x34),
                        QColor(0x2A, 0x28, 0x25),
                        QColor(0xE8, 0xE2, 0xD4));
    m_pause = pauseBtn;

    const auto &tk = Theme::tokens();

    // Fade All — amber. A tinted-dark fill at rest (a real "amber chip", not
    // a wireframe), a subtle lift on hover, and bright amber text + amber
    // border carrying the meaning. Theme-token driven so it follows themes.
    auto *fadeBtn = new AnimatedButton(tr("Fade All"), this);
    fadeBtn->setMinimumHeight(44);
    fadeBtn->setBorderRadius(6);
    fadeBtn->setColors(tintOver(tk.bgPanel, tk.warn, 0.16f),   // rest
                       tintOver(tk.bgPanel, tk.warn, 0.32f),   // hover lift
                       tintOver(tk.bgPanel, tk.warn, 0.10f),   // pressed
                       tk.warnBright);                          // text
    fadeBtn->setBorderColor(tk.warn);
    fadeBtn->setToolTip(tr("Fade every running cue out over 2 seconds"));
    m_fadeAll = fadeBtn;

    // Panic — terracotta. Same chip treatment, slightly more present (it's the
    // danger control), with bright terracotta text + border.
    auto *panicBtn = new AnimatedButton(tr("Panic"), this);
    panicBtn->setMinimumHeight(44);
    panicBtn->setBorderRadius(6);
    panicBtn->setColors(tintOver(tk.bgPanel, tk.err, 0.18f),   // rest
                        tintOver(tk.bgPanel, tk.err, 0.36f),   // hover lift
                        tintOver(tk.bgPanel, tk.err, 0.10f),   // pressed
                        tk.errBright);                          // text
    panicBtn->setBorderColor(tk.err);
    panicBtn->setToolTip(tr("Stop everything immediately"));
    m_panic = panicBtn;

    // DCA GO — the second GO. Fires the Mix (DCA) list against the console,
    // separate from the playback GO so the operator can advance the mix scene
    // and the sound cues independently. Deliberately a different colour (dusty
    // "console" blue, not the playback green) and shorter than the hero GO, so
    // the two are never confused under booth pressure. Disabled until a mix
    // list is loaded and a console is connected.
    m_dcaGo = new QPushButton(tr("DCA GO"), this);
    m_dcaGo->setObjectName(QStringLiteral("dcaGoButton"));
    m_dcaGo->setProperty("state", "disabled");
    m_dcaGo->setEnabled(false);
    m_dcaGo->setMinimumHeight(64);
    m_dcaGo->setToolTip(tr("Connect a console on a Mix (DCA) list to fire DCA cues."));

    m_goButton = new QPushButton(tr("GO"), this);
    m_goButton->setObjectName(QStringLiteral("goButton"));
    // Dynamic property drives QSS state colour: standby (blue) until a
    // cue is queued, ready (green) when one is, disabled when none.
    m_goButton->setProperty("state", "standby");
    // Shortcut handled by the rebindable QAction owned by MainWindow.
    m_goButton->setDefault(true);

    // Explicit AlignVCenter — without it QHBoxLayout's default stretch
    // policy makes the 44-px AnimatedButtons grow to fill the full
    // ~88-px content area, which both looks wrong next to the GO
    // button and re-introduces the top-clip when the painter draws
    // a rounded background bigger than the actual sizeHint.
    layout->addWidget(m_pause,    0, Qt::AlignVCenter);
    layout->addWidget(m_fadeAll,  0, Qt::AlignVCenter);
    layout->addWidget(m_panic,    0, Qt::AlignVCenter);
    layout->addWidget(m_dcaGo,    0, Qt::AlignVCenter);
    layout->addWidget(m_goButton, 0, Qt::AlignVCenter);

    connect(m_goButton, &QPushButton::clicked, this, &TransportBar::goPressed);
    connect(m_dcaGo,    &QPushButton::clicked, this, &TransportBar::dcaGoPressed);
    connect(m_panic,    &QPushButton::clicked, this, &TransportBar::panicPressed);
    connect(m_pause,    &QPushButton::clicked, this, &TransportBar::pausePressed);
    connect(m_fadeAll,  &QPushButton::clicked, this, &TransportBar::fadeAllPressed);
}

void TransportBar::setDcaGoState(bool ready, const QString &tooltip)
{
    if (!m_dcaGo) return;
    m_dcaGo->setEnabled(ready);
    const char *state = ready ? "ready" : "disabled";
    if (m_dcaGo->property("state").toString() != QLatin1String(state)) {
        m_dcaGo->setProperty("state", state);
        // Re-polish so the QSS attribute selector repaints the fill.
        m_dcaGo->style()->unpolish(m_dcaGo);
        m_dcaGo->style()->polish(m_dcaGo);
    }
    m_dcaGo->setToolTip(tooltip);
}

TransportBar::~TransportBar() = default;

void TransportBar::setNextCue(cues::Cue *cue)
{
    m_nextCue = cue;
    const char *state = cue ? "ready" : "standby";
    if (m_goButton->property("state").toString() != QLatin1String(state)) {
        m_goButton->setProperty("state", state);
        // Re-polish so the QSS attribute selector picks up the change.
        m_goButton->style()->unpolish(m_goButton);
        m_goButton->style()->polish(m_goButton);
    }
    if (!cue) {
        m_nextLabel->setText(tr("—"));
        return;
    }
    const auto numStr = QString::number(cue->number(), 'f', 2);
    const auto name = cue->name().isEmpty() ? cue->typeName() : cue->name();
    m_nextLabel->setText(QStringLiteral("%1   %2").arg(numStr, name));
}

} // namespace quewi::ui
