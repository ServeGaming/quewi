#pragma once

#include <QHash>
#include <QMap>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>

class QTimer;

namespace quewi::osc {

class EosFeedback;
struct Message;

// One cue (or one part of a multipart cue) as an ETC Eos desk records it.
// Cue numbers stay strings exactly as the desk sends them ("12", "12.5") —
// they are labels, not floats.
struct EosCue {
    QString list;                 // "1"
    QString number;               // "12.5"
    int     part = 0;             // 0 = the cue itself; 1.. = its parts
    QString uid;                  // the desk's OSC UID (stable across renumbers)
    QString label;
    QString notes;
    QString scene;
    bool    sceneEnd = false;
    int     upMs = -1, upDelayMs = -1;       // -1 = not set
    int     downMs = -1, downDelayMs = -1;
    int     followMs = -1, hangMs = -1;
    QString mark, block, assert_, link;      // as the desk words them
    int     partCount = 0;
    QString timecode;

    QString key() const;                     // "1/12.5/0"
    QString displayNumber() const;           // "12.5", "12.5 P2"
    bool operator==(const EosCue &o) const;
};

// One cue list on the desk.
struct EosCueListInfo {
    QString number;               // "1"
    QString uid;
    QString label;
    bool operator==(const EosCueListInfo &o) const
    { return number == o.number && uid == o.uid && label == o.label; }
};

// "12.50" → "12.5", "007" → "7": so a number typed in quewi ("5.0") meets
// the same cue the desk calls "5". Non-numbers come back trimmed.
QString normalEosNumber(const QString &n);
// Desk order: 1 < 1.5 < 2 < 10 (numeric, not text). < 0, 0, > 0.
int compareEosNumbers(const QString &a, const QString &b);

// Reads cue lists off an ETC Eos desk (Eos, Ion, Element, Nomad) over the
// EosFeedback TCP link — the same connection, never a second socket.
//
// How (ETC's "OSC Get" API, the way ETC's own Luminosus and node-eos-console
// do it — HeliOSC uses a cue-list bank instead, which only shows a window
// around the pending cue):
//   /eos/get/cuelist/count             → /eos/out/get/cuelist/count  <n>
//   /eos/get/cuelist/index/<i>         → /eos/out/get/cuelist/<list>/list/0/13
//       args: 0 index, 1 uid, 2 label, 3 playback mode, …
//   /eos/get/cue/<list>/count          → /eos/out/get/cue/<list>/count  <n>
//   /eos/get/cue/<list>/index/<i>      → /eos/out/get/cue/<list>/<cue>/<part>/list/0/31
//       args: 0 index, 1 uid, 2 label, 3 up ms, 4 up delay, 5 down ms,
//       6 down delay, 7-12 focus/colour/beam time+delay, 13 preheat,
//       14 curve, 15 rate, 16 mark, 17 block, 18 assert, 19 link,
//       20 follow ms, 21 hang ms, 22 all fade, 23 loop, 24 solo,
//       25 timecode, 26 part count, 27 notes, 28 scene, 29 scene end,
//       30 cue part index. (-1 = not set.)
//   Checked against Eos 3.3.9 (Nomad), 2026-10-04: indices are 0-based and
//   travel in args[0]; the trailing list/<a>/<b> pages the ARGUMENTS (first,
//   total), not the cues. An index past the end answers
//   /eos/out/get/cue/0/0 <index> (/eos/out/get/cuelist/0 <index> for lists).
//   The desk follows each cue with .../fx/..., .../links/... and
//   .../actions/... replies, which are skipped.
//   /eos/out/notify/cue/<list> <seq> <cues…> — something in that list
//   changed (needs /eos/subscribe, which EosFeedback sends). A moment later
//   just those cues are asked for again by number (/eos/get/cue/<list>/
//   <cue>/<part>; an empty reply = deleted) and the list's count is checked;
//   if the count doesn't match what that leaves (a renumber the notice only
//   half-described, a range of cues), or there were lots, the whole list is
//   read again — so a renumber or delete can't leave a stale cue behind.
//   (The notify format is ETC's documented one; not yet seen from a real
//   desk — nothing was edited on one while this was written.)
//
// Speed: a desk answers GETs one at a time — Nomad 3.3.9 took ~110 ms per
// cue whatever the window (146 cues ≈ 17 s) — so a whole read is slow and
// the last complete list stays up meanwhile. Requests go out a window at a
// time (32 in flight), and a fetch that stalls re-asks for what's missing
// once before settling for what it has (state Partial). Read-only: it only
// ever sends /eos/get/... requests.
class EosCueLists : public QObject {
    Q_OBJECT
public:
    enum class State { Idle, Fetching, Ready, Partial };
    Q_ENUM(State)

    // `link` may be null (tests feed handle() and set a sender).
    explicit EosCueLists(EosFeedback *link, QObject *parent = nullptr);
    ~EosCueLists() override;

    // The cue lists whose cues are kept fetched ("1", "2"…). Lists no longer
    // watched are forgotten; newly watched ones are fetched straight away
    // when the desk is connected.
    void setWatched(const QStringList &lists);
    QStringList watched() const { return m_watched; }

    // Ask again: the desk's list of cue lists, and every watched list.
    void refresh();
    void refreshList(const QString &list);

    QVector<EosCueListInfo> cueLists() const { return m_lists; }
    // A watched list's cues in desk order (number, then part). Empty until fetched.
    QVector<EosCue> cues(const QString &list) const;
    State state(const QString &list) const;
    // While fetching: how many of how many have come in.
    int received(const QString &list) const;
    int expected(const QString &list) const;

    // Feed one message from the desk (the link does this; tests call it).
    void handle(const Message &m);

    // Tests: send through this instead of the link; tune the timings.
    void setSender(std::function<void(const Message &)> send) { m_sender = std::move(send); }
    void setTimings(int notifyDebounceMs, int stallMs, int window);

signals:
    void cueListsChanged();
    void cuesChanged(const QString &list);   // a fetch finished with something new
    void stateChanged(const QString &list);  // Fetching / Ready / Partial

private:
    struct Fetch {
        State state = State::Idle;
        int   expected = -1;                 // -1 = waiting for the count
        int   nextIndex = 0;                 // next index to ask for
        QSet<int> outstanding;
        QMap<int, EosCue> byIndex;           // replies that carried an index
        QHash<QString, EosCue> byKey;        // ... and ones that didn't
        QSet<int> empty;                     // indices that came back empty
        bool  retried = false;
        bool  again = false;                 // a change came in mid-fetch
        qint64 lastHeard = 0;
    };

    void send(const QString &address);
    bool connected() const;
    void startFetch(const QString &list);
    void pump(const QString &list);
    void finish(const QString &list, State how);
    void onStallTick();
    void setState(const QString &list, Fetch &f, State s);
    // A few cues changed (a notify): ask for just those.
    struct Patch {
        bool active = false;
        QSet<QString> pending;               // "number/part" still to hear about
        QHash<QString, EosCue> cues;         // the list being patched, by key
        int   countReply = -1;
        qint64 since = 0;
    };
    void startPatch(const QString &list, const QSet<QString> &numbers);
    void finishPatchIfDone(const QString &list);
    QHash<QString, Patch> m_patch;
    QHash<QString, QSet<QString>> m_notified;   // list → cue numbers ("*" = all)

    EosFeedback *m_link = nullptr;
    std::function<void(const Message &)> m_sender;
    QStringList m_watched;
    QVector<EosCueListInfo> m_lists;
    QMap<int, EosCueListInfo> m_listsIncoming;
    int m_listsExpected = -1;
    QHash<QString, QVector<EosCue>> m_cues;
    QHash<QString, Fetch> m_fetch;
    QTimer *m_notifyTimer = nullptr;
    QTimer *m_stallTimer = nullptr;
    int m_stallMs = 4000;
    int m_window = 32;
    qint64 m_ticks = 0;                      // ms on the stall clock
};

} // namespace quewi::osc
