#include "ui/TransportBar.h"

#include "cues/Cue.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>

namespace quewi::ui {

TransportBar::TransportBar(QWidget *parent)
    : QWidget(parent)
{
    setMinimumHeight(72);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(12, 8, 12, 8);

    auto *nextHeader = new QLabel(tr("NEXT"), this);
    auto headerFont = nextHeader->font();
    headerFont.setBold(true);
    nextHeader->setFont(headerFont);
    layout->addWidget(nextHeader);

    m_nextLabel = new QLabel(tr("—"), this);
    auto nextFont = m_nextLabel->font();
    nextFont.setPointSizeF(nextFont.pointSizeF() + 6.0);
    nextFont.setBold(true);
    m_nextLabel->setFont(nextFont);
    layout->addWidget(m_nextLabel, 1);

    m_pause = new QPushButton(tr("Pause"), this);
    m_panic = new QPushButton(tr("Panic"), this);
    m_goButton = new QPushButton(tr("GO"), this);
    m_goButton->setMinimumSize(160, 56);
    auto goFont = m_goButton->font();
    goFont.setPointSizeF(goFont.pointSizeF() + 8.0);
    goFont.setBold(true);
    m_goButton->setFont(goFont);
    m_goButton->setShortcut(Qt::Key_Space);

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
