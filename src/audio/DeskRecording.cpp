#include "audio/DeskRecording.h"

#include <QObject>

#include <algorithm>
#include <cmath>

namespace quewi::audio {

namespace {
QString normal(const QString &n)
{
    QString s = n.trimmed();
    bool ok = false;
    s.toDouble(&ok);
    if (!ok) return s;
    if (s.contains(QLatin1Char('.'))) {
        while (s.endsWith(QLatin1Char('0'))) s.chop(1);
        if (s.endsWith(QLatin1Char('.'))) s.chop(1);
    }
    return s;
}
} // namespace

void DeskRecording::start(const QString &list)
{
    m_takes.clear();
    m_list = normal(list);
    m_recording = true;
}

bool DeskRecording::add(const QString &list, const QString &cue, const QString &label, double at)
{
    if (!m_recording || cue.trimmed().isEmpty() || at < 0.0) return false;
    if (!m_list.isEmpty() && !list.isEmpty() && normal(list) != m_list) return false;
    for (auto it = m_takes.rbegin(); it != m_takes.rend(); ++it) {
        if (std::abs(it->at - at) > kSameCueWindow) break;
        if (normal(it->cue) == normal(cue) && normal(it->list) == normal(list)) return false;
    }
    m_takes.push_back({at, list.trimmed(), cue.trimmed(), label.trimmed()});
    return true;
}

LightTriggers DeskRecording::toTriggers(const std::vector<RecordedCue> &takes, const QString &group,
                                        const BeatGrid *snap)
{
    std::vector<RecordedCue> sorted = takes;
    std::stable_sort(sorted.begin(), sorted.end(),
                     [](const RecordedCue &a, const RecordedCue &b) { return a.at < b.at; });
    LightTriggers out;
    for (const auto &t : sorted) {
        LightTrigger lt;
        lt.name = t.label.isEmpty() ? QStringLiteral("LX %1").arg(t.cue)
                                    : QStringLiteral("LX %1 %2").arg(t.cue, t.label);
        lt.start = (snap && snap->isSet()) ? std::max(0.0, snap->snap(t.at)) : t.at;
        lt.group = group;
        lt.enter.kind = TriggerAction::Kind::Desk;
        lt.enter.deskDo = TriggerAction::DeskDo::GoToCue;
        lt.enter.number = t.cue;
        lt.enter.list = std::max(1, t.list.toInt());
        out.push_back(lt);
    }
    return out;
}

QString DeskRecording::timeText(double seconds)
{
    if (seconds < 0.0) seconds = 0.0;
    const int whole = int(std::floor(seconds));
    const int hundredths = int(std::lround((seconds - whole) * 100.0)) % 100;
    return QStringLiteral("%1:%2.%3").arg(whole / 60).arg(whole % 60, 2, 10, QLatin1Char('0'))
        .arg(hundredths, 2, 10, QLatin1Char('0'));
}

} // namespace quewi::audio
