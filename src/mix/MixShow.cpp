#include "mix/MixShow.h"

#include <QJsonArray>
#include <algorithm>

namespace quewi::mix {
namespace {

QJsonArray stripsToJson(const QSet<int> &strips)
{
    QVector<int> sorted(strips.begin(), strips.end());
    std::sort(sorted.begin(), sorted.end());   // stable output = diffable show file
    QJsonArray a;
    for (int s : sorted) a.append(s);
    return a;
}

QSet<int> stripsFromJson(const QJsonArray &a)
{
    QSet<int> out;
    for (const auto v : a) {
        const int s = v.toInt();
        if (s >= 1) out.insert(s);      // drop junk rather than carry it forward
    }
    return out;
}

} // namespace

MixShow::MixShow(QObject *parent) : QObject(parent) {}
MixShow::~MixShow() = default;

// ── DCA count ────────────────────────────────────────────────────────

void MixShow::setDcaCount(int count)
{
    const int clamped = std::clamp(count, kMinDcaCount, kMaxDcaCount);
    if (clamped == m_dcaCount) return;
    m_dcaCount = clamped;
    // Deliberately does NOT prune assignments above the new count. Lowering
    // the count on a mis-click would otherwise silently destroy programming
    // that raising it back can't recover. Cues keep their data; the grid just
    // stops showing those columns, and ConsoleLink::sanitize drops anything
    // the connected desk can't address.
    emit dcaCountChanged();
}

// ── Channels ─────────────────────────────────────────────────────────

QVector<MixChannel> MixShow::channels() const
{
    QVector<MixChannel> out;
    out.reserve(m_channels.size());
    for (const auto &c : m_channels) out.push_back(c);
    std::sort(out.begin(), out.end(),
              [](const MixChannel &a, const MixChannel &b) { return a.strip < b.strip; });
    return out;
}

MixChannel MixShow::channel(int strip) const { return m_channels.value(strip); }
bool       MixShow::hasChannel(int strip) const { return m_channels.contains(strip); }

void MixShow::setChannel(const MixChannel &channel)
{
    if (!channel.isValid()) return;
    m_channels.insert(channel.strip, channel);
    emit channelsChanged();
}

void MixShow::removeChannel(int strip)
{
    if (m_channels.remove(strip) == 0) return;

    // A deleted channel must not linger as someone's backup or as a ghost
    // member of an ensemble — either would silently open a mic that no longer
    // has an owner.
    bool ensemblesTouched = false;
    for (auto &members : m_ensembles)
        if (members.remove(strip)) ensemblesTouched = true;

    for (auto &c : m_channels)
        if (c.backupStrip == strip) c.backupStrip = 0;

    emit channelsChanged();
    if (ensemblesTouched) emit ensemblesChanged();
}

// ── Ensembles ────────────────────────────────────────────────────────

QStringList MixShow::ensembleNames() const
{
    QStringList names = m_ensembles.keys();
    names.sort(Qt::CaseInsensitive);
    return names;
}

Ensemble MixShow::ensemble(const QString &name) const { return m_ensembles.value(name); }

void MixShow::setEnsemble(const QString &name, const Ensemble &strips)
{
    if (name.isEmpty()) return;
    m_ensembles.insert(name, strips);
    emit ensemblesChanged();
}

void MixShow::removeEnsemble(const QString &name)
{
    if (m_ensembles.remove(name) > 0) emit ensemblesChanged();
}

QSet<int> MixShow::resolve(const QSet<int> &strips, const QStringList &ensembles) const
{
    QSet<int> out;
    for (int s : strips)
        if (m_channels.contains(s)) out.insert(s);

    for (const auto &name : ensembles) {
        const auto it = m_ensembles.constFind(name);
        if (it == m_ensembles.constEnd()) continue;   // deleted ensemble: skip
        for (int s : *it)
            if (m_channels.contains(s)) out.insert(s);
    }
    return out;
}

// ── Recasting ────────────────────────────────────────────────────────

int MixShow::reassignStrip(int fromStrip, int toStrip)
{
    if (fromStrip == toStrip || fromStrip < 1 || toStrip < 1) return 0;

    int touched = 0;
    bool ensemblesTouched = false;

    if (const auto it = m_channels.constFind(fromStrip); it != m_channels.constEnd()) {
        MixChannel moved = *it;
        moved.strip = toStrip;
        m_channels.remove(fromStrip);
        m_channels.insert(toStrip, moved);   // replaces whatever was on toStrip
        ++touched;
    }

    for (auto &c : m_channels) {
        if (c.backupStrip == fromStrip) { c.backupStrip = toStrip; ++touched; }
    }

    for (auto &members : m_ensembles) {
        if (members.remove(fromStrip)) { members.insert(toStrip); ensemblesTouched = true; }
    }

    if (touched) emit channelsChanged();
    if (ensemblesTouched) emit ensemblesChanged();
    return touched;
}

// ── Persistence ──────────────────────────────────────────────────────

QJsonObject MixShow::toJson() const
{
    QJsonArray chans;
    for (const auto &c : channels()) {          // sorted, for a stable file
        QJsonObject o;
        o["strip"] = c.strip;
        if (!c.name.isEmpty())  o["name"]   = c.name;
        if (!c.actor.isEmpty()) o["actor"]  = c.actor;
        if (c.backupStrip)      o["backup"] = c.backupStrip;
        chans.append(o);
    }

    QJsonObject ens;
    for (const auto &name : ensembleNames())    // sorted
        ens[name] = stripsToJson(m_ensembles.value(name));

    QJsonObject root;
    root["dcaCount"]  = m_dcaCount;
    root["channels"]  = chans;
    root["ensembles"] = ens;
    return root;
}

void MixShow::fromJson(const QJsonObject &json)
{
    m_channels.clear();
    m_ensembles.clear();
    m_dcaCount = std::clamp(json.value(QStringLiteral("dcaCount")).toInt(kMinDcaCount),
                            kMinDcaCount, kMaxDcaCount);

    for (const auto v : json.value(QStringLiteral("channels")).toArray()) {
        const auto o = v.toObject();
        MixChannel c;
        c.strip       = o.value(QStringLiteral("strip")).toInt();
        c.name        = o.value(QStringLiteral("name")).toString();
        c.actor       = o.value(QStringLiteral("actor")).toString();
        c.backupStrip = o.value(QStringLiteral("backup")).toInt();
        if (c.isValid()) m_channels.insert(c.strip, c);   // skip corrupt entries
    }

    const auto ens = json.value(QStringLiteral("ensembles")).toObject();
    for (auto it = ens.begin(); it != ens.end(); ++it)
        m_ensembles.insert(it.key(), stripsFromJson(it.value().toArray()));

    emit channelsChanged();
    emit ensemblesChanged();
    emit dcaCountChanged();
}

void MixShow::clear()
{
    m_channels.clear();
    m_ensembles.clear();
    m_dcaCount = kMinDcaCount;
    emit channelsChanged();
    emit ensemblesChanged();
    emit dcaCountChanged();
}

} // namespace quewi::mix
