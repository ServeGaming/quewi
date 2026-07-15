#include "mix/MixCue.h"

#include "mix/MixShow.h"

#include <QJsonArray>
#include <QJsonObject>
#include <algorithm>

namespace quewi::mix {

MixCue::MixCue(QObject *parent) : cues::Cue(parent) {}
MixCue::~MixCue() = default;

QSet<int>   MixCue::dcaStrips(int dca) const    { return m_dcas.value(dca).strips; }
QStringList MixCue::dcaEnsembles(int dca) const { return m_dcas.value(dca).ensembles; }

void MixCue::setDcaStrips(int dca, const QSet<int> &strips)
{
    if (dca < 1) return;
    auto &entry = m_dcas[dca];
    if (entry.strips == strips) return;
    entry.strips = strips;
    if (entry.isEmpty()) m_dcas.remove(dca);   // don't persist empty entries
    emitChanged();
}

void MixCue::setDcaEnsembles(int dca, const QStringList &ensembles)
{
    if (dca < 1) return;
    auto &entry = m_dcas[dca];
    if (entry.ensembles == ensembles) return;
    entry.ensembles = ensembles;
    if (entry.isEmpty()) m_dcas.remove(dca);
    emitChanged();
}

void MixCue::clearDca(int dca)
{
    if (m_dcas.remove(dca) > 0) emitChanged();
}

QList<int> MixCue::assignedDcas() const
{
    QList<int> out;
    for (auto it = m_dcas.constBegin(); it != m_dcas.constEnd(); ++it)
        if (!it.value().isEmpty()) out.push_back(it.key());
    std::sort(out.begin(), out.end());
    return out;
}

bool MixCue::isEmpty() const { return assignedDcas().isEmpty(); }

QHash<int, DcaSet> MixCue::channelAssignments(const MixShow &show) const
{
    QHash<int, DcaSet> out;
    for (auto it = m_dcas.constBegin(); it != m_dcas.constEnd(); ++it) {
        const int dca = it.key();
        // Resolve here, at fire time, rather than at edit time: an ensemble's
        // membership can change after this cue was written, and the cue should
        // follow it.
        for (int strip : show.resolve(it.value().strips, it.value().ensembles))
            out[strip].insert(dca);
    }
    return out;
}

bool MixCue::reassignStrip(int fromStrip, int toStrip)
{
    if (fromStrip == toStrip || fromStrip < 1 || toStrip < 1) return false;

    bool changed = false;
    for (auto &entry : m_dcas) {
        if (entry.strips.remove(fromStrip)) {
            entry.strips.insert(toStrip);
            changed = true;
        }
    }
    if (changed) emitChanged();
    return changed;
}

QJsonObject MixCue::toPayload() const
{
    QJsonObject payload = cues::Cue::toPayload();

    QJsonObject dcas;
    for (int dca : assignedDcas()) {              // sorted -> diffable show file
        const auto &entry = m_dcas.value(dca);
        QJsonObject o;

        if (!entry.strips.isEmpty()) {
            QVector<int> sorted(entry.strips.begin(), entry.strips.end());
            std::sort(sorted.begin(), sorted.end());
            QJsonArray a;
            for (int s : sorted) a.append(s);
            o["strips"] = a;
        }
        if (!entry.ensembles.isEmpty()) {
            QJsonArray a;
            for (const auto &e : entry.ensembles) a.append(e);
            o["ensembles"] = a;
        }
        dcas[QString::number(dca)] = o;
    }
    payload["dcas"] = dcas;
    return payload;
}

void MixCue::fromPayload(const QJsonObject &payload)
{
    cues::Cue::fromPayload(payload);
    m_dcas.clear();

    const auto dcas = payload.value(QStringLiteral("dcas")).toObject();
    for (auto it = dcas.begin(); it != dcas.end(); ++it) {
        bool ok = false;
        const int dca = it.key().toInt(&ok);
        if (!ok || dca < 1) continue;             // skip corrupt keys

        const auto o = it.value().toObject();
        DcaEntry entry;
        for (const auto v : o.value(QStringLiteral("strips")).toArray()) {
            const int s = v.toInt();
            if (s >= 1) entry.strips.insert(s);
        }
        for (const auto v : o.value(QStringLiteral("ensembles")).toArray()) {
            const auto name = v.toString();
            if (!name.isEmpty()) entry.ensembles.push_back(name);
        }
        if (!entry.isEmpty()) m_dcas.insert(dca, entry);
    }
    emitChanged();
}

} // namespace quewi::mix
