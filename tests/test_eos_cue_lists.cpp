#include <QTest>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QtEndian>

#include "osc/EosCueLists.h"
#include "osc/EosFeedback.h"
#include "osc/OscCodec.h"
#include "osc/OscMessage.h"

using namespace quewi;

namespace {

QByteArray framed(const osc::Message &m)
{
    const QByteArray pkt = osc::Codec::encode(m);
    QByteArray out(4, Qt::Uninitialized);
    qToBigEndian<quint32>(quint32(pkt.size()), out.data());
    return out + pkt;
}

struct FakeCue {
    QString number;
    int     part = 0;
    QString uid, label, notes, scene;
    int     upMs = 3000;
    int     followMs = -1;
};

// A pretend Eos desk on localhost: answers the OSC Get requests the way
// ETC's Show Control guide describes (count, then index/<i> → the cue plus
// its /fx, /links and /actions replies), and can push notifications.
class FakeEos : public QObject {
public:
    QTcpServer server;
    QTcpSocket *peer = nullptr;
    QMap<QString, QVector<FakeCue>> lists;      // list number → cues (desk order)
    QMap<QString, QString> listLabels;
    QStringList requests;                       // every address asked for
    int outstanding = 0, maxOutstanding = 0;    // index requests not yet answered
    bool answerIndices = true;                  // false: swallow index requests (a stall)
    QByteArray buf;
    QVector<QString> held;                      // swallowed requests, answerable later

    FakeEos()
    {
        server.listen(QHostAddress::LocalHost, 0);
        connect(&server, &QTcpServer::newConnection, this, [this] {
            peer = server.nextPendingConnection();
            connect(peer, &QTcpSocket::readyRead, this, [this] { read(); });
            // Say something, so the link goes Live.
            send({QStringLiteral("/eos/out/show/name"), {osc::Argument::s(QStringLiteral("Test show"))}});
        });
    }

    void send(const osc::Message &m)
    {
        if (peer) { peer->write(framed(m)); peer->flush(); }
    }

    void notify(const QString &list, const QString &cues)
    {
        send({QStringLiteral("/eos/out/notify/cue/%1").arg(list),
              {osc::Argument::i(7), osc::Argument::s(cues)}});
    }

    void read()
    {
        buf += peer->readAll();
        while (buf.size() >= 4) {
            const quint32 len = qFromBigEndian<quint32>(buf.constData());
            if (quint32(buf.size()) < 4 + len) return;
            const auto el = osc::Codec::decode(buf.mid(4, int(len)));
            buf.remove(0, int(4 + len));
            if (!el) continue;
            if (const auto *m = std::get_if<osc::Message>(&*el)) answer(m->address);
        }
    }

    void answer(const QString &a)
    {
        requests << a;
        const QStringList p = a.split(QLatin1Char('/'), Qt::SkipEmptyParts);
        // eos get cuelist count | eos get cuelist index i
        if (a == QLatin1String("/eos/get/cuelist/count")) {
            send({QStringLiteral("/eos/out/get/cuelist/count"), {osc::Argument::i(int(lists.size()))}});
            return;
        }
        if (p.size() == 5 && p[2] == QLatin1String("cuelist") && p[3] == QLatin1String("index")) {
            const int i = p[4].toInt();
            const QString num = lists.keys().value(i);
            send({QStringLiteral("/eos/out/get/cuelist/%1/list/%2/%3").arg(num).arg(i).arg(lists.size()),
                  {osc::Argument::i(i), osc::Argument::s(QStringLiteral("uid-list-") + num),
                   osc::Argument::s(listLabels.value(num))}});
            send({QStringLiteral("/eos/out/get/cuelist/%1/links/list/%2/%3").arg(num).arg(i).arg(lists.size()),
                  {osc::Argument::i(i), osc::Argument::s(QStringLiteral("uid-list-") + num)}});
            return;
        }
        // eos get cue <L> count | eos get cue <L> index <i>
        if (p.size() == 5 && p[2] == QLatin1String("cue") && p[4] == QLatin1String("count")) {
            send({QStringLiteral("/eos/out/get/cue/%1/count").arg(p[3]),
                  {osc::Argument::i(int(lists.value(p[3]).size()))}});
            return;
        }
        if (p.size() == 6 && p[2] == QLatin1String("cue") && p[4] == QLatin1String("index")) {
            ++outstanding;
            maxOutstanding = std::max(maxOutstanding, outstanding);
            if (!answerIndices) { held << a; return; }
            QTimer::singleShot(0, this, [this, a] { answerIndex(a); });
        }
    }

    void answerIndex(const QString &a)
    {
        --outstanding;
        const QStringList p = a.split(QLatin1Char('/'), Qt::SkipEmptyParts);
        const QString list = p[3];
        const int i = p[5].toInt();
        const auto &cues = lists[list];
        const int n = int(cues.size());
        if (i >= n) {
            // Gone since the count: the desk answers with no arguments.
            send({QStringLiteral("/eos/out/get/cue/%1/0/0/list/%2/%3").arg(list).arg(i).arg(n), {}});
            return;
        }
        const auto &c = cues[i];
        const QString base = QStringLiteral("/eos/out/get/cue/%1/%2/%3").arg(list, c.number).arg(c.part);
        std::vector<osc::Argument> args;
        args.push_back(osc::Argument::i(i));               // 0 index
        args.push_back(osc::Argument::s(c.uid));           // 1 uid
        args.push_back(osc::Argument::s(c.label));         // 2 label
        args.push_back(osc::Argument::i(c.upMs));          // 3 up
        args.push_back(osc::Argument::i(0));               // 4 up delay
        args.push_back(osc::Argument::i(c.upMs + 1000));   // 5 down
        for (int k = 6; k <= 15; ++k) args.push_back(osc::Argument::i(-1));
        args.push_back(osc::Argument::s(QString()));       // 16 mark
        args.push_back(osc::Argument::s(QString()));       // 17 block
        args.push_back(osc::Argument::s(QString()));       // 18 assert
        args.push_back(osc::Argument::s(QString()));       // 19 link
        args.push_back(osc::Argument::i(c.followMs));      // 20 follow
        args.push_back(osc::Argument::i(-1));              // 21 hang
        args.push_back(osc::Argument::F());                // 22 all fade
        args.push_back(osc::Argument::i(0));               // 23 loop
        args.push_back(osc::Argument::F());                // 24 solo
        args.push_back(osc::Argument::s(QString()));       // 25 timecode
        args.push_back(osc::Argument::i(0));               // 26 part count
        args.push_back(osc::Argument::s(c.notes));         // 27 notes
        args.push_back(osc::Argument::s(c.scene));         // 28 scene
        args.push_back(osc::Argument::F());                // 29 scene end
        args.push_back(osc::Argument::i(c.part));          // 30 part index
        send({base + QStringLiteral("/list/%1/%2").arg(i).arg(n), args});
        // The desk's follow-ups for the same cue — must be ignored.
        send({base + QStringLiteral("/fx/list/%1/%2").arg(i).arg(n),
              {osc::Argument::i(i), osc::Argument::s(c.uid)}});
        send({base + QStringLiteral("/links/list/%1/%2").arg(i).arg(n),
              {osc::Argument::i(i), osc::Argument::s(c.uid)}});
        send({base + QStringLiteral("/actions/list/%1/%2").arg(i).arg(n),
              {osc::Argument::i(i), osc::Argument::s(c.uid), osc::Argument::s(QStringLiteral("(ext)"))}});
    }

    void releaseHeld()
    {
        const auto h = held;
        held.clear();
        for (const auto &a : h) answerIndex(a);
    }
};

FakeCue cue(const QString &n, const QString &label, int part = 0)
{
    FakeCue c;
    c.number = n;
    c.part = part;
    c.uid = QStringLiteral("uid-%1-%2").arg(n).arg(part);
    c.label = label;
    return c;
}

} // namespace

// Reading cue lists off an Eos desk (osc::EosCueLists) over the EosFeedback
// TCP link: ETC's OSC Get, decimal cues and parts, a big list, the desk's
// change notifications, and a desk that stalls.
class EosCueListsTests : public QObject {
    Q_OBJECT

private slots:
    void numbersCompareLikeTheDesk()
    {
        QCOMPARE(osc::normalEosNumber(QStringLiteral("12.50")), QStringLiteral("12.5"));
        QCOMPARE(osc::normalEosNumber(QStringLiteral("5.0")), QStringLiteral("5"));
        QCOMPARE(osc::normalEosNumber(QStringLiteral("007")), QStringLiteral("7"));
        QCOMPARE(osc::normalEosNumber(QStringLiteral("0.5")), QStringLiteral("0.5"));
        QVERIFY(osc::compareEosNumbers(QStringLiteral("2"), QStringLiteral("10")) < 0);
        QVERIFY(osc::compareEosNumbers(QStringLiteral("1.5"), QStringLiteral("2")) < 0);
        QCOMPARE(osc::compareEosNumbers(QStringLiteral("3.0"), QStringLiteral("3")), 0);
    }

    // The parser alone, fed messages directly (no TCP): argument positions,
    // lenient types, the /fx /links /actions follow-ups skipped, an empty
    // reply counted as "gone".
    void parsesCueRepliesDirectly()
    {
        osc::EosCueLists r(nullptr);
        QStringList sent;
        r.setSender([&sent](const osc::Message &m) { sent << m.address; });
        r.setWatched({QStringLiteral("2")});
        QCOMPARE(sent, QStringList{QStringLiteral("/eos/get/cue/2/count")});
        QCOMPARE(r.state(QStringLiteral("2")), osc::EosCueLists::State::Fetching);

        r.handle({QStringLiteral("/eos/out/get/cue/2/count"), {osc::Argument::i(3)}});
        QCOMPARE(sent.size(), 4);
        QCOMPARE(sent.last(), QStringLiteral("/eos/get/cue/2/index/2"));

        QSignalSpy changed(&r, &osc::EosCueLists::cuesChanged);
        std::vector<osc::Argument> a(31, osc::Argument::i(-1));
        a[0] = osc::Argument::i(0);
        a[1] = osc::Argument::s(QStringLiteral("U1"));
        a[2] = osc::Argument::s(QStringLiteral("Preset"));
        a[3] = osc::Argument::f(2500.0f);                  // a float time is fine too
        a[20] = osc::Argument::i(1500);
        a[26] = osc::Argument::i(2);
        a[27] = osc::Argument::s(QStringLiteral("Wait for applause"));
        a[28] = osc::Argument::s(QStringLiteral("Act 1"));
        a[29] = osc::Argument::T();
        r.handle({QStringLiteral("/eos/out/get/cue/2/0.5/0/list/0/3"), a});
        r.handle({QStringLiteral("/eos/out/get/cue/2/0.5/0/fx/list/0/3"),
                  {osc::Argument::i(0), osc::Argument::s(QStringLiteral("U1"))}});
        a[0] = osc::Argument::i(1);
        a[1] = osc::Argument::s(QStringLiteral("U2"));
        a[2] = osc::Argument::s(QStringLiteral("Part two"));
        r.handle({QStringLiteral("/eos/out/get/cue/2/0.5/2/list/1/3"), a});
        QCOMPARE(changed.count(), 0);                       // not done yet
        r.handle({QStringLiteral("/eos/out/get/cue/2/0/0/list/2/3"), {}});   // gone
        QCOMPARE(changed.count(), 1);
        QCOMPARE(r.state(QStringLiteral("2")), osc::EosCueLists::State::Ready);

        const auto cues = r.cues(QStringLiteral("2"));
        QCOMPARE(cues.size(), 2);
        QCOMPARE(cues[0].number, QStringLiteral("0.5"));
        QCOMPARE(cues[0].part, 0);
        QCOMPARE(cues[0].label, QStringLiteral("Preset"));
        QCOMPARE(cues[0].upMs, 2500);
        QCOMPARE(cues[0].followMs, 1500);
        QCOMPARE(cues[0].partCount, 2);
        QCOMPARE(cues[0].notes, QStringLiteral("Wait for applause"));
        QCOMPARE(cues[0].scene, QStringLiteral("Act 1"));
        QVERIFY(cues[0].sceneEnd);
        QCOMPARE(cues[1].part, 2);
        QCOMPARE(cues[1].displayNumber(), QStringLiteral("0.5 P2"));

        // Messages for a list nobody watches, or stray replies, change nothing.
        r.handle({QStringLiteral("/eos/out/get/cue/9/count"), {osc::Argument::i(5)}});
        r.handle({QStringLiteral("/eos/out/get/cue/2/1/0/list/0/1"), a});
        QCOMPARE(r.cues(QStringLiteral("2")).size(), 2);
    }

    void readsAListFromAFakeDesk()
    {
        FakeEos desk;
        desk.lists[QStringLiteral("1")] = {
            cue(QStringLiteral("1"), QStringLiteral("Preshow")),
            cue(QStringLiteral("2"), QStringLiteral("House out")),
            cue(QStringLiteral("2.5"), QStringLiteral("Lamp")),
            cue(QStringLiteral("10"), QStringLiteral("Storm")),
            cue(QStringLiteral("10"), QStringLiteral("Storm — lightning"), 1),
            cue(QStringLiteral("10"), QStringLiteral("Storm — rain"), 2),
        };
        desk.lists[QStringLiteral("2")] = {cue(QStringLiteral("1"), QStringLiteral("Practicals"))};
        desk.listLabels[QStringLiteral("1")] = QStringLiteral("Main");
        desk.listLabels[QStringLiteral("2")] = QStringLiteral("Practicals");

        osc::EosFeedback fb;
        fb.setTimings(200, 1000, 5000);
        osc::EosCueLists r(&fb);
        r.setWatched({QStringLiteral("1")});
        QSignalSpy changed(&r, &osc::EosCueLists::cuesChanged);
        QSignalSpy lists(&r, &osc::EosCueLists::cueListsChanged);
        fb.start(QStringLiteral("127.0.0.1"), desk.server.serverPort());

        QTRY_COMPARE(r.state(QStringLiteral("1")), osc::EosCueLists::State::Ready);
        QCOMPARE(changed.count(), 1);
        const auto cues = r.cues(QStringLiteral("1"));
        QCOMPARE(cues.size(), 6);
        QStringList order;
        for (const auto &c : cues) order << c.displayNumber();
        QCOMPARE(order, (QStringList{QStringLiteral("1"), QStringLiteral("2"), QStringLiteral("2.5"),
                                     QStringLiteral("10"), QStringLiteral("10 P1"), QStringLiteral("10 P2")}));
        QCOMPARE(cues[2].label, QStringLiteral("Lamp"));
        QCOMPARE(cues[2].upMs, 3000);
        QCOMPARE(cues[2].downMs, 4000);

        // The desk's list of cue lists came too.
        QTRY_COMPARE(r.cueLists().size(), 2);
        QCOMPARE(r.cueLists()[1].label, QStringLiteral("Practicals"));
        QVERIFY(lists.count() >= 1);

        // Read-only: nothing but /eos/get/... (and the link's own subscribe/ping).
        for (const auto &a : std::as_const(desk.requests))
            QVERIFY2(a.startsWith(QLatin1String("/eos/get/")) || a == QLatin1String("/eos/subscribe")
                         || a == QLatin1String("/eos/ping"),
                     qPrintable(a));
    }

    // 300 cues: windowed (never more than the window in flight), all read,
    // in order.
    void readsABigListInWindows()
    {
        FakeEos desk;
        QVector<FakeCue> big;
        for (int i = 1; i <= 300; ++i) big.push_back(cue(QString::number(i * 0.5), QStringLiteral("Cue %1").arg(i)));
        desk.lists[QStringLiteral("1")] = big;

        osc::EosFeedback fb;
        osc::EosCueLists r(&fb);
        r.setTimings(50, 4000, 16);
        r.setWatched({QStringLiteral("1")});
        fb.start(QStringLiteral("127.0.0.1"), desk.server.serverPort());
        QTRY_COMPARE_WITH_TIMEOUT(r.state(QStringLiteral("1")), osc::EosCueLists::State::Ready, 10000);
        const auto cues = r.cues(QStringLiteral("1"));
        QCOMPARE(cues.size(), 300);
        QCOMPARE(cues.first().number, QStringLiteral("0.5"));
        QCOMPARE(cues.last().number, QStringLiteral("150"));
        QVERIFY(desk.maxOutstanding <= 16);
        QCOMPARE(r.expected(QStringLiteral("1")), 300);
    }

    // The desk says list 1 changed (a renumber, a delete): it's read again,
    // whole, and the stale cue is gone.
    void notificationRereadsTheList()
    {
        FakeEos desk;
        desk.lists[QStringLiteral("1")] = {cue(QStringLiteral("1"), QStringLiteral("A")),
                                           cue(QStringLiteral("2"), QStringLiteral("B")),
                                           cue(QStringLiteral("3"), QStringLiteral("C"))};
        osc::EosFeedback fb;
        osc::EosCueLists r(&fb);
        r.setTimings(50, 4000, 32);
        r.setWatched({QStringLiteral("1")});
        fb.start(QStringLiteral("127.0.0.1"), desk.server.serverPort());
        QTRY_COMPARE(r.cues(QStringLiteral("1")).size(), 3);
        QSignalSpy changed(&r, &osc::EosCueLists::cuesChanged);

        // Renumber 2 → 2.5 (same UID), delete 3.
        auto &l = desk.lists[QStringLiteral("1")];
        l[1].number = QStringLiteral("2.5");
        l.removeLast();
        desk.notify(QStringLiteral("1"), QStringLiteral("2-3"));
        QTRY_COMPARE(changed.count(), 1);
        const auto cues = r.cues(QStringLiteral("1"));
        QCOMPARE(cues.size(), 2);
        QCOMPARE(cues[1].number, QStringLiteral("2.5"));
        QCOMPARE(cues[1].uid, QStringLiteral("uid-2-0"));

        // A notification for a list nobody watches is ignored.
        const int asked = int(desk.requests.size());
        desk.notify(QStringLiteral("7"), QStringLiteral("1"));
        QTest::qWait(200);
        QCOMPARE(int(desk.requests.size()), asked);
    }

    // A desk that stops answering mid-fetch: asked once more, then the
    // fetch settles as Partial with what came in — the old list isn't lost.
    void aStalledDeskEndsPartial()
    {
        FakeEos desk;
        desk.lists[QStringLiteral("1")] = {cue(QStringLiteral("1"), QStringLiteral("A")),
                                           cue(QStringLiteral("2"), QStringLiteral("B"))};
        osc::EosFeedback fb;
        osc::EosCueLists r(&fb);
        r.setTimings(50, 300, 32);
        r.setWatched({QStringLiteral("1")});
        fb.start(QStringLiteral("127.0.0.1"), desk.server.serverPort());
        QTRY_COMPARE(r.state(QStringLiteral("1")), osc::EosCueLists::State::Ready);

        desk.answerIndices = false;
        desk.lists[QStringLiteral("1")].push_back(cue(QStringLiteral("3"), QStringLiteral("C")));
        r.refreshList(QStringLiteral("1"));
        QCOMPARE(r.state(QStringLiteral("1")), osc::EosCueLists::State::Fetching);
        QTRY_COMPARE_WITH_TIMEOUT(r.state(QStringLiteral("1")), osc::EosCueLists::State::Partial, 3000);
        // Asked twice for each index (the retry).
        int asks = 0;
        for (const auto &a : std::as_const(desk.requests))
            if (a == QLatin1String("/eos/get/cue/1/index/2")) ++asks;
        QCOMPARE(asks, 2);
        // An incomplete read doesn't replace the last complete one.
        QCOMPARE(r.cues(QStringLiteral("1")).size(), 2);
        // The desk wakes up: reading again completes.
        desk.answerIndices = true;
        desk.held.clear();
        r.refreshList(QStringLiteral("1"));
        QTRY_COMPARE(r.state(QStringLiteral("1")), osc::EosCueLists::State::Ready);
        QCOMPARE(r.cues(QStringLiteral("1")).size(), 3);
    }
};

QTEST_MAIN(EosCueListsTests)
#include "test_eos_cue_lists.moc"
