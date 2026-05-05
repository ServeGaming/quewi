#include "ui/TransportBar.h"

#include "cues/Cue.h"
#include "ui/AnimatedButton.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>

namespace quewi::ui {

TransportBar::TransportBar(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("transportBar"));
    setMinimumHeight(96);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(20, 12, 20, 12);
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
    pauseBtn->setMinimumHeight(36);
    pauseBtn->setBorderRadius(6);
    pauseBtn->setColors(QColor(0x34, 0x30, 0x2C),
                        QColor(0x40, 0x3A, 0x34),
                        QColor(0x2A, 0x28, 0x25),
                        QColor(0xE8, 0xE2, 0xD4));
    m_pause = pauseBtn;

    auto *fadeBtn = new AnimatedButton(tr("Fade All"), this);
    fadeBtn->setMinimumHeight(36);
    fadeBtn->setBorderRadius(6);
    fadeBtn->setColors(QColor(0x26, 0x24, 0x22),
                        QColor(0xD7, 0xA2, 0x4E),  // amber on hover
                        QColor(0xB0, 0x82, 0x3E),
                        QColor(0xD7, 0xA2, 0x4E));
    fadeBtn->setBorderColor(QColor(0xD7, 0xA2, 0x4E));
    fadeBtn->setToolTip(tr("Fade every running cue out over 2 seconds"));
    m_fadeAll = fadeBtn;

    auto *panicBtn = new AnimatedButton(tr("Panic"), this);
    panicBtn->setMinimumHeight(36);
    panicBtn->setBorderRadius(6);
    panicBtn->setColors(QColor(0x26, 0x24, 0x22),
                        QColor(0xC2, 0x6A, 0x55),  // terracotta on hover
                        QColor(0x9E, 0x55, 0x44),
                        QColor(0xC2, 0x6A, 0x55));
    panicBtn->setBorderColor(QColor(0xC2, 0x6A, 0x55));
    m_panic = panicBtn;

    m_goButton = new QPushButton(tr("GO"), this);
    m_goButton->setObjectName(QStringLiteral("goButton"));
    // Dynamic property drives QSS state colour: standby (blue) until a
    // cue is queued, ready (green) when one is, disabled when none.
    m_goButton->setProperty("state", "standby");
    // Shortcut handled by the rebindable QAction owned by MainWindow.
    m_goButton->setDefault(true);

    layout->addWidget(m_pause);
    layout->addWidget(m_fadeAll);
    layout->addWidget(m_panic);
    layout->addWidget(m_goButton);

    connect(m_goButton, &QPushButton::clicked, this, &TransportBar::goPressed);
    connect(m_panic,    &QPushButton::clicked, this, &TransportBar::panicPressed);
    connect(m_pause,    &QPushButton::clicked, this, &TransportBar::pausePressed);
    connect(m_fadeAll,  &QPushButton::clicked, this, &TransportBar::fadeAllPressed);
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
