#include "ui/TransportBar.h"

#include "cues/Cue.h"

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
    m_pause = new QPushButton(tr("Pause"), this);
    m_pause->setObjectName(QStringLiteral("pauseButton"));
    m_pause->setMinimumHeight(36);

    m_fadeAll = new QPushButton(tr("Fade All"), this);
    m_fadeAll->setObjectName(QStringLiteral("fadeAllButton"));
    m_fadeAll->setMinimumHeight(36);
    m_fadeAll->setToolTip(tr("Fade every running cue out over 2 seconds"));

    m_panic = new QPushButton(tr("Panic"), this);
    m_panic->setObjectName(QStringLiteral("panicButton"));
    m_panic->setMinimumHeight(36);

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
