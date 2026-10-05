#include "ui/DeskTakeRecorder.h"

#include "core/LightingDesk.h"
#include "osc/EosFeedback.h"

namespace quewi::ui {

DeskTakeRecorder::DeskTakeRecorder(QObject *parent) : QObject(parent) {}

void DeskTakeRecorder::setFeedback(osc::EosFeedback *fb)
{
    if (m_fb) m_fb->disconnect(this);
    m_fb = fb;
    if (m_fb) connect(m_fb, &osc::EosFeedback::cueFired, this, &DeskTakeRecorder::onFired);
}

QString DeskTakeRecorder::whyNot() const
{
    if (!m_fb)
        return tr("quewi isn't reading a lighting desk. Set up an ETC Eos desk in Tools → Lighting Desk… "
                  "with \"Read the desk's state back\" on.");
    switch (m_fb->link()) {
    case osc::EosFeedback::Link::Live:       return {};
    case osc::EosFeedback::Link::Connecting: return tr("Still connecting to the desk — try again in a moment.");
    case osc::EosFeedback::Link::Failed:     return tr("quewi can't reach the desk (%1).").arg(m_fb->detail());
    case osc::EosFeedback::Link::Off:        break;
    }
    return tr("Reading the desk is off. Turn on \"Read the desk's state back\" in Tools → Lighting Desk… "
              "(ETC Eos family desks only).");
}

bool DeskTakeRecorder::start(const QString &list)
{
    if (!whyNot().isEmpty()) return false;
    m_take.start(list);
    emit recordingChanged(true);
    return true;
}

void DeskTakeRecorder::stop()
{
    if (!m_take.isRecording()) return;
    m_take.stop();
    emit recordingChanged(false);
}

void DeskTakeRecorder::onFired(const QString &list, const QString &cue, const QString &label)
{
    if (!m_take.isRecording()) return;
    const double at = m_playhead ? m_playhead() : -1.0;
    if (m_take.add(list, cue, label, at)) emit recorded(m_take.takes().back());
}

} // namespace quewi::ui
