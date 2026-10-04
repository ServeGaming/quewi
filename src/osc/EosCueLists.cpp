#include "osc/EosCueLists.h"

#include "osc/EosFeedback.h"
#include "osc/OscMessage.h"

#include <QElapsedTimer>
#include <QTimer>

#include <algorithm>

namespace quewi::osc {

namespace {

QElapsedTimer &clock()
{
    static QElapsedTimer t;
    if (!t.isValid()) t.start();
    return t;
}

// Eos is loose about argument types (a time can come as int or float, a
// label as a string or nothing at all), so read every argument leniently.
QString argText(const Message &m, size_t i)
{
    if (i >= m.args.size()) return {};
    const auto &a = m.args[i];
    if (a.tag == Argument::Tag::String || a.tag == Argument::Tag::Symbol)
        return std::get<QString>(a.value);
    if (a.tag == Argument::Tag::True) return QStringLiteral("true");
    if (a.tag == Argument::Tag::False) return QStringLiteral("false");
    if (const auto n = toNumber(a)) return QString::number(*n);
    return {};
}

int argInt(const Message &m, size_t i)
{
    if (i >= m.args.size()) return -1;
    if (const auto n = toNumber(m.args[i])) return int(*n);
    bool ok = false;
    const int v = argText(m, i).toInt(&ok);
    return ok ? v : -1;
}

bool argBool(const Message &m, size_t i)
{
    if (i >= m.args.size()) return false;
    const auto &a = m.args[i];
    if (a.tag == Argument::Tag::True) return true;
    if (a.tag == Argument::Tag::False) return false;
    if (const auto n = toNumber(a)) return *n != 0.0;
    const QString s = argText(m, i).toLower();
    return s == QLatin1String("true") || s == QLatin1String("1");
}

// Desk order for two cues: number, then part.
bool cueBefore(const EosCue &a, const EosCue &b)
{
    const int c = compareEosNumbers(a.number, b.number);
    if (c != 0) return c < 0;
    return a.part < b.part;
}

} // namespace

QString EosCue::key() const
{
    return QStringLiteral("%1/%2/%3").arg(list, normalEosNumber(number)).arg(part);
}

QString EosCue::displayNumber() const
{
    return part > 0 ? QStringLiteral("%1 P%2").arg(number).arg(part) : number;
}

bool EosCue::operator==(const EosCue &o) const
{
    return list == o.list && number == o.number && part == o.part && uid == o.uid
        && label == o.label && notes == o.notes && scene == o.scene && sceneEnd == o.sceneEnd
        && upMs == o.upMs && upDelayMs == o.upDelayMs && downMs == o.downMs
        && downDelayMs == o.downDelayMs && followMs == o.followMs && hangMs == o.hangMs
        && mark == o.mark && block == o.block && assert_ == o.assert_ && link == o.link
        && partCount == o.partCount && timecode == o.timecode;
}

QString normalEosNumber(const QString &n)
{
    QString s = n.trimmed();
    bool ok = false;
    s.toDouble(&ok);
    if (!ok) return s;
    if (s.contains(QLatin1Char('.'))) {
        while (s.endsWith(QLatin1Char('0'))) s.chop(1);
        if (s.endsWith(QLatin1Char('.'))) s.chop(1);
    }
    while (s.size() > 1 && s.startsWith(QLatin1Char('0')) && s.at(1) != QLatin1Char('.')) s.remove(0, 1);
    return s;
}

int compareEosNumbers(const QString &a, const QString &b)
{
    bool okA = false, okB = false;
    const double x = a.toDouble(&okA), y = b.toDouble(&okB);
    if (okA && okB) return x < y ? -1 : (x > y ? 1 : 0);
    if (okA != okB) return okA ? -1 : 1;          // numbers before anything odd
    return QString::compare(a, b);
}

EosCueLists::EosCueLists(EosFeedback *link, QObject *parent)
    : QObject(parent), m_link(link)
{
    m_notifyTimer = new QTimer(this);
    m_notifyTimer->setSingleShot(true);
    m_notifyTimer->setInterval(400);
    connect(m_notifyTimer, &QTimer::timeout, this, [this] {
        const auto dirty = m_dirty;
        m_dirty.clear();
        for (const auto &l : dirty) refreshList(l);
    });
    m_stallTimer = new QTimer(this);
    m_stallTimer->setInterval(m_stallMs / 4);
    connect(m_stallTimer, &QTimer::timeout, this, &EosCueLists::onStallTick);

    if (m_link) {
        connect(m_link, &EosFeedback::messageReceived, this, &EosCueLists::handle);
        connect(m_link, &EosFeedback::linkChanged, this, [this](EosFeedback::Link l) {
            if (l == EosFeedback::Link::Live) {
                refresh();
            } else {
                // A dropped link abandons any fetch in progress; what was
                // read stays (a stale list beats an empty one mid-show) and
                // is read again when the desk comes back.
                for (auto it = m_fetch.begin(); it != m_fetch.end(); ++it)
                    if (it->state == State::Fetching) {
                        it->outstanding.clear();
                        setState(it.key(), *it, m_cues.contains(it.key()) ? State::Partial : State::Idle);
                    }
                m_listsExpected = -1;
            }
        });
    }
}

EosCueLists::~EosCueLists() = default;

void EosCueLists::setTimings(int notifyDebounceMs, int stallMs, int window)
{
    m_notifyTimer->setInterval(notifyDebounceMs);
    m_stallMs = std::max(50, stallMs);
    m_stallTimer->setInterval(std::max(10, m_stallMs / 4));
    m_window = std::max(1, window);
}

bool EosCueLists::connected() const
{
    if (m_sender) return true;
    return m_link && m_link->isConnected();
}

void EosCueLists::send(const QString &address)
{
    const Message m{address, {}};
    if (m_sender) m_sender(m);
    else if (m_link) m_link->sendToDesk(m);
}

void EosCueLists::setWatched(const QStringList &lists)
{
    QStringList next;
    for (const auto &l : lists) {
        const QString n = normalEosNumber(l);
        if (!n.isEmpty() && !next.contains(n)) next << n;
    }
    if (next == m_watched) return;
    const QStringList before = m_watched;
    m_watched = next;
    for (const auto &l : before)
        if (!m_watched.contains(l)) {
            m_cues.remove(l);
            m_fetch.remove(l);
        }
    for (const auto &l : m_watched)
        if (!before.contains(l) && connected()) refreshList(l);
}

void EosCueLists::refresh()
{
    if (!connected()) return;
    m_listsIncoming.clear();
    m_listsExpected = -1;
    send(QStringLiteral("/eos/get/cuelist/count"));
    for (const auto &l : m_watched) refreshList(l);
}

void EosCueLists::refreshList(const QString &list)
{
    if (!m_watched.contains(list) || !connected()) return;
    auto &f = m_fetch[list];
    if (f.state == State::Fetching) {
        f.again = true;          // finish this one, then read it again
        return;
    }
    startFetch(list);
}

void EosCueLists::startFetch(const QString &list)
{
    auto &f = m_fetch[list];
    f = Fetch{};
    f.lastHeard = clock().elapsed();
    setState(list, f, State::Fetching);
    send(QStringLiteral("/eos/get/cue/%1/count").arg(list));
    if (!m_stallTimer->isActive()) m_stallTimer->start();
}

void EosCueLists::pump(const QString &list)
{
    auto it = m_fetch.find(list);
    if (it == m_fetch.end() || it->expected < 0) return;
    auto &f = *it;
    while (f.outstanding.size() < m_window && f.nextIndex < f.expected) {
        const int i = f.nextIndex++;
        f.outstanding.insert(i);
        send(QStringLiteral("/eos/get/cue/%1/index/%2").arg(list).arg(i));
    }
    if (f.outstanding.isEmpty() && f.nextIndex >= f.expected) finish(list, State::Ready);
}

void EosCueLists::finish(const QString &list, State how)
{
    auto it = m_fetch.find(list);
    if (it == m_fetch.end()) return;
    QVector<EosCue> all;
    for (const auto &c : std::as_const(it->byIndex)) all.push_back(c);
    for (const auto &c : std::as_const(it->byKey)) {
        const bool dup = std::any_of(all.cbegin(), all.cend(),
                                     [&c](const EosCue &x) { return x.key() == c.key(); });
        if (!dup) all.push_back(c);
    }
    std::stable_sort(all.begin(), all.end(), cueBefore);
    const bool again = it->again;
    it->outstanding.clear();
    setState(list, *it, how);
    // An incomplete read never replaces a complete one: a cue missing only
    // because the desk stopped answering would read as deleted.
    if (how == State::Partial && m_cues.contains(list)) {
        if (again) startFetch(list);
        return;
    }
    if (m_cues.value(list) != all || !m_cues.contains(list)) {
        m_cues.insert(list, all);
        emit cuesChanged(list);
    }
    if (again) startFetch(list);
}

void EosCueLists::onStallTick()
{
    const qint64 now = clock().elapsed();
    bool any = false;
    const auto keys = m_fetch.keys();
    for (const auto &list : keys) {
        auto &f = m_fetch[list];
        if (f.state != State::Fetching) continue;
        any = true;
        if (now - f.lastHeard < m_stallMs) continue;
        f.lastHeard = now;
        if (!f.retried) {
            // Ask once more for whatever went unanswered (UDP-free, but a
            // busy desk has been seen to drop requests under load).
            f.retried = true;
            if (f.expected < 0) {
                send(QStringLiteral("/eos/get/cue/%1/count").arg(list));
            } else {
                for (int i : std::as_const(f.outstanding))
                    send(QStringLiteral("/eos/get/cue/%1/index/%2").arg(list).arg(i));
            }
            continue;
        }
        if (f.expected < 0) {
            f.outstanding.clear();
            setState(list, f, m_cues.contains(list) ? State::Partial : State::Idle);
        } else {
            finish(list, State::Partial);
        }
    }
    if (!any) m_stallTimer->stop();
}

void EosCueLists::setState(const QString &list, Fetch &f, State s)
{
    if (f.state == s) return;
    f.state = s;
    emit stateChanged(list);
}

QVector<EosCue> EosCueLists::cues(const QString &list) const
{
    return m_cues.value(normalEosNumber(list));
}

EosCueLists::State EosCueLists::state(const QString &list) const
{
    const auto it = m_fetch.constFind(normalEosNumber(list));
    return it == m_fetch.constEnd() ? State::Idle : it->state;
}

int EosCueLists::received(const QString &list) const
{
    const auto it = m_fetch.constFind(normalEosNumber(list));
    if (it == m_fetch.constEnd()) return 0;
    return int(it->byIndex.size() + it->byKey.size() + it->empty.size());
}

int EosCueLists::expected(const QString &list) const
{
    const auto it = m_fetch.constFind(normalEosNumber(list));
    return it == m_fetch.constEnd() ? -1 : it->expected;
}

void EosCueLists::handle(const Message &m)
{
    const QString &a = m.address;
    if (!a.startsWith(QLatin1String("/eos/out/"))) return;
    const QStringList p = a.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    // p[0] eos, p[1] out, p[2] get|notify, p[3] cue|cuelist, ...
    if (p.size() < 4) return;

    if (p[2] == QLatin1String("notify") && p[3] == QLatin1String("cue") && p.size() >= 5) {
        const QString list = normalEosNumber(p[4]);
        if (m_watched.contains(list)) {
            m_dirty.insert(list);
            m_notifyTimer->start();
        }
        return;
    }
    if (p[2] == QLatin1String("notify") && p[3] == QLatin1String("cuelist")) {
        // A list was added, deleted or relabelled.
        if (connected()) {
            m_listsIncoming.clear();
            m_listsExpected = -1;
            send(QStringLiteral("/eos/get/cuelist/count"));
        }
        return;
    }
    if (p[2] != QLatin1String("get")) return;

    if (p[3] == QLatin1String("cuelist")) {
        if (p.size() == 5 && p[4] == QLatin1String("count")) {
            m_listsExpected = std::max(0, argInt(m, 0));
            m_listsIncoming.clear();
            for (int i = 0; i < m_listsExpected; ++i)
                send(QStringLiteral("/eos/get/cuelist/index/%1").arg(i));
            if (m_listsExpected == 0 && !m_lists.isEmpty()) {
                m_lists.clear();
                emit cueListsChanged();
            }
            return;
        }
        // /eos/out/get/cuelist/<n>/list/<i>/<count> (skip .../links/...)
        if (p.size() == 8 && p[5] == QLatin1String("list")) {
            const int idx = p[6].toInt();
            if (!m.args.empty() && !argText(m, 1).isEmpty()) {
                EosCueListInfo info;
                info.number = normalEosNumber(p[4]);
                info.uid = argText(m, 1);
                info.label = argText(m, 2);
                m_listsIncoming.insert(idx, info);
            } else {
                m_listsIncoming.insert(idx, {});      // gone: counts as answered
            }
            if (m_listsExpected >= 0 && m_listsIncoming.size() >= m_listsExpected) {
                QVector<EosCueListInfo> next;
                for (const auto &l : std::as_const(m_listsIncoming))
                    if (!l.number.isEmpty()) next.push_back(l);
                std::stable_sort(next.begin(), next.end(), [](const auto &x, const auto &y) {
                    return compareEosNumbers(x.number, y.number) < 0;
                });
                m_listsIncoming.clear();
                m_listsExpected = -1;
                if (next != m_lists) {
                    m_lists = next;
                    emit cueListsChanged();
                }
            }
        }
        return;
    }

    if (p[3] != QLatin1String("cue") || p.size() < 6) return;
    const QString list = normalEosNumber(p[4]);
    auto it = m_fetch.find(list);
    if (it == m_fetch.end() || it->state != State::Fetching) return;
    auto &f = *it;
    f.lastHeard = clock().elapsed();

    if (p.size() == 6 && p[5] == QLatin1String("count")) {
        if (f.expected >= 0) return;               // a retried count's echo
        f.expected = std::max(0, argInt(m, 0));
        pump(list);
        return;
    }
    if (p.size() < 7) return;
    // /eos/out/get/cue/<list>/<cue>/<part>[/list/<i>/<count>]; the desk also
    // sends /fx/, /links/ and /actions/ replies for each cue — not ours.
    if (p.size() > 7 && p[7] != QLatin1String("list")) return;
    int index = -1;
    if (p.size() >= 9) index = p[8].toInt();
    if (index >= 0) f.outstanding.remove(index);

    if (m.args.size() < 2 || argText(m, 1).isEmpty()) {
        // No UID: that cue doesn't exist (deleted between count and get).
        if (index >= 0) f.empty.insert(index);
    } else {
        EosCue c;
        c.list = list;
        c.number = p[5];
        c.part = p[6].toInt();
        c.uid = argText(m, 1);
        c.label = argText(m, 2);
        c.upMs = argInt(m, 3);
        c.upDelayMs = argInt(m, 4);
        c.downMs = argInt(m, 5);
        c.downDelayMs = argInt(m, 6);
        c.mark = argText(m, 16);
        c.block = argText(m, 17);
        c.assert_ = argText(m, 18);
        c.link = argText(m, 19);
        c.followMs = argInt(m, 20);
        c.hangMs = argInt(m, 21);
        c.timecode = argText(m, 25);
        c.partCount = std::max(0, argInt(m, 26));
        c.notes = argText(m, 27);
        c.scene = argText(m, 28);
        c.sceneEnd = argBool(m, 29);
        if (index >= 0) f.byIndex.insert(index, c);
        else f.byKey.insert(c.key(), c);
    }
    pump(list);
}

} // namespace quewi::osc
