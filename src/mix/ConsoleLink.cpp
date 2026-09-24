#include "mix/ConsoleLink.h"

#include <algorithm>

namespace quewi::mix {

ConsoleLink::ConsoleLink(QObject *parent) : QObject(parent) {}
ConsoleLink::~ConsoleLink() = default;

void ConsoleLink::setState(State s)
{
    if (m_state == s) return;
    m_state = s;
    emit stateChanged(s);
}

void ConsoleLink::setCapabilities(const Capabilities &caps)
{
    m_caps = caps;
    emit capabilitiesChanged();
}

void ConsoleLink::setError(const QString &message)
{
    m_lastError = message;
    emit errorOccurred(message);
}

bool ConsoleLink::isChannelValid(int channel) const
{
    return channel >= 1 && channel <= m_caps.channelCount;
}

bool ConsoleLink::isDcaValid(int dca) const
{
    return dca >= 1 && dca <= m_caps.dcaCount;
}

DcaSet ConsoleLink::sanitize(const DcaSet &dcas) const
{
    DcaSet out;
    for (int dca : dcas)
        if (isDcaValid(dca)) out.insert(dca);
    return out;
}

DcaSet ConsoleLink::dcaAssignment(int channel) const
{
    return m_dcaCache.value(channel);
}

void ConsoleLink::setDcaAssignment(int channel, const DcaSet &dcas)
{
    if (!isChannelValid(channel)) return;

    // Sanitise before the cache, not after: a rejected DCA must never be
    // remembered as assigned, or a later diff would try to "remove" it.
    const DcaSet next     = sanitize(dcas);
    const DcaSet previous = m_dcaCache.value(channel);
    const bool   known    = m_dcaKnown.contains(channel);
    // A no-op is only a no-op if we actually KNOW the desk's state. An empty
    // cache used to count as "the desk has nothing" — so after connect or a
    // scene recall, a mic the desk had on DCA 3 was never taken off it.
    if (known && previous == next) return;

    m_dcaCache.insert(channel, next);
    m_dcaKnown.insert(channel);
    writeDcaAssignment(channel, previous, next, known);
}

void ConsoleLink::noteSurfaceDcaAssignment(int channel, const DcaSet &dcas, bool complete)
{
    if (!isChannelValid(channel)) return;

    const DcaSet next = sanitize(dcas);
    if (complete) m_dcaKnown.insert(channel);
    if (m_dcaCache.value(channel) == next) return;

    m_dcaCache.insert(channel, next);
    emit surfaceDcaAssignmentChanged(channel, next);
}

void ConsoleLink::forgetDcaState()
{
    m_dcaCache.clear();
    m_dcaKnown.clear();
}

void ConsoleLink::applyCue(const QHash<int, DcaSet> &assignments,
                           const QSet<int> &controlled)
{
    // Every controlled channel not named by the cue is unassigned and muted.
    // That rule is the whole safety property of DCA cueing: a mic that isn't
    // in this scene is off, and there's no way to forget one. Channels quewi
    // doesn't control are left exactly as the operator has them.
    QSet<int> channels = controlled;
    for (auto it = assignments.constBegin(); it != assignments.constEnd(); ++it)
        channels.insert(it.key());
    QList<int> ordered(channels.begin(), channels.end());
    std::sort(ordered.begin(), ordered.end());   // deterministic wire order

    for (int channel : ordered) {
        if (!isChannelValid(channel)) continue;
        const auto it = assignments.constFind(channel);
        const DcaSet want = (it != assignments.constEnd()) ? sanitize(*it) : DcaSet{};

        setDcaAssignment(channel, want);
        setChannelMuted(channel, want.isEmpty());
    }
}

} // namespace quewi::mix
