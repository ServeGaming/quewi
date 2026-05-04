#include "ui/TransportBar.h"

#include "cues/Cue.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
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

    // Right: action buttons, escalating right-to-left
    m_pause = new QPushButton(tr("Pause"), this);
    m_pause->setObjectName(QStringLiteral("pauseButton"));
    m_pause->setMinimumHeight(36);

    m_panic = new QPushButton(tr("Panic"), this);
    m_panic->setObjectName(QStringLiteral("panicButton"));
    m_panic->setMinimumHeight(36);

    m_goButton = new QPushButton(tr("GO"), this);
    m_goButton->setObjectName(QStringLiteral("goButton"));
    m_goButton->setShortcut(Qt::Key_Space);
    m_goButton->setDefault(true);

    layout->addWidget(m_pause);
    layout->addWidget(m_panic);
    layout->addWidget(m_goButton);

    connect(m_goButton, &QPushButton::clicked, this, &TransportBar::goPressed);
    connect(m_panic,    &QPushButton::clicked, this, &TransportBar::panicPressed);
    connect(m_pause,    &QPushButton::clicked, this, &TransportBar::pausePressed);
}

TransportBar::~TransportBar() = default;

void TransportBar::setNextCue(cues::Cue *cue)
{
    m_nextCue = cue;
    if (!cue) {
        m_nextLabel->setText(tr("—"));
        return;
    }
    const auto numStr = QString::number(cue->number(), 'f', 2);
    const auto name = cue->name().isEmpty() ? cue->typeName() : cue->name();
    m_nextLabel->setText(QStringLiteral("%1   %2").arg(numStr, name));
}

} // namespace quewi::ui
