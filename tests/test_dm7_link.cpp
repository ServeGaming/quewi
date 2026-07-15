#include <QTest>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>

#include "mix/ConsoleLink.h"
#include "mix/Dm7Link.h"
#include "mix/Dm7Value.h"

namespace dm7 = quewi::mix::dm7;
using quewi::mix::ConsoleLink;
using quewi::mix::DcaSet;
using quewi::mix::Dm7Link;

// Minimal stand-in for a DM7: accepts one TCP client, records every line, and
// answers devinfo so the link can reach Connected.
class FakeDm7 : public QObject {
    Q_OBJECT
public:
    QStringList received;

    FakeDm7()
    {
        server.listen(QHostAddress::LocalHost, 0);
        connect(&server, &QTcpServer::newConnection, this, [this] {
            peer = server.nextPendingConnection();
            connect(peer, &QTcpSocket::readyRead, this, &FakeDm7::onReadyRead);
        });
    }

    quint16 port() const { return server.serverPort(); }

    void push(const QString &line)          // unsolicited, like a surface move
    {
        if (peer) { peer->write(line.toUtf8() + '\n'); peer->flush(); }
    }

    QStringList matching(const QString &needle) const
    {
        QStringList out;
        for (const auto &l : received)
            if (l.contains(needle)) out.push_back(l);
        return out;
    }

    QString productName = QStringLiteral("DM7");

private slots:
    void onReadyRead()
    {
        buffer += QString::fromUtf8(peer->readAll());
        int nl = -1;
        while ((nl = buffer.indexOf(u'\n')) >= 0) {
            const QString line = buffer.left(nl);
            buffer.remove(0, nl + 1);
            received.push_back(line);

            if (line == QLatin1String("devinfo productname"))
                push(QStringLiteral("OK devinfo productname \"%1\"").arg(productName));
            else if (line == QLatin1String("devinfo version"))
                push(QStringLiteral("OK devinfo version \"1.60\""));
        }
    }

private:
    QTcpServer server;
    QTcpSocket *peer = nullptr;
    QString buffer;
};

class Dm7LinkTests : public QObject {
    Q_OBJECT

    std::unique_ptr<FakeDm7> desk;
    std::unique_ptr<Dm7Link> link;

    void connectAndSettle()
    {
        link->connectToConsole(QStringLiteral("127.0.0.1"), desk->port());
        QTRY_COMPARE_WITH_TIMEOUT(link->state(), ConsoleLink::State::Connected, 3000);
        // Wait for the startup burst to actually land, rather than guessing a
        // delay — over a real TCP socket a fixed qWait() is a race, and a
        // flaky test is worse than no test.
        QTRY_VERIFY_WITH_TIMEOUT(!desk->matching(QLatin1String(dm7::kSplitOn)).isEmpty(), 2000);
    }

    // Blocks until the fake console has seen `count` lines containing `needle`.
    bool awaitLines(const QString &needle, int count, int timeoutMs = 2000)
    {
        QElapsedTimer t; t.start();
        while (t.elapsed() < timeoutMs) {
            if (desk->matching(needle).size() >= count) return true;
            QTest::qWait(5);
        }
        return desk->matching(needle).size() >= count;
    }

private slots:
    void init()
    {
        desk = std::make_unique<FakeDm7>();
        link = std::make_unique<Dm7Link>();
    }
    void cleanup() { link.reset(); desk.reset(); }

    void connectsAndReadsCapabilities()
    {
        connectAndSettle();
        QCOMPARE(link->capabilities().model, QStringLiteral("DM7"));
        QCOMPARE(link->capabilities().firmware, QStringLiteral("1.60"));
        QCOMPARE(link->capabilities().channelCount, 120);
        QCOMPARE(link->capabilities().dcaCount, 24);     // not the X32's 8
        QCOMPARE(link->capabilities().muteGroupCount, 12);
        QVERIFY(link->capabilities().channelEq);         // unlike CL/QL and TF
        QVERIFY(link->capabilities().inputMetering);
    }

    // DM7 vs DM7 Compact differ only in input count. The community module
    // hardcodes one number for both and gets it wrong on a Compact.
    void compactHasFewerChannelsButTheSameDcas()
    {
        desk->productName = QStringLiteral("DM7 Compact");
        connectAndSettle();
        QCOMPARE(link->capabilities().channelCount, 72);
        QCOMPARE(link->capabilities().dcaCount, 24);     // identical
        QCOMPARE(link->capabilities().muteGroupCount, 12);
    }

    void sendsRequiredSessionModes()
    {
        connectAndSettle();
        // Scene numbers are strings on DM7; the t-suffix verbs misbehave
        // without this, so it must be in the startup sequence.
        QVERIFY(awaitLines(QStringLiteral("scpmode sstype"), 1));
        // Non-ASCII names mangle without this.
        QVERIFY(!desk->matching(QStringLiteral("scpmode encoding utf8")).isEmpty());
        // A dead client otherwise blocks reconnection until the console
        // notices — the exact thing we can't have mid-show.
        QVERIFY(!desk->matching(QStringLiteral("scpmode keepalive")).isEmpty());
    }

    void readsSplitModeBeforeTouchingDcaIndices()
    {
        connectAndSettle();
        QVERIFY(!desk->matching(QLatin1String(dm7::kSplitOn)).isEmpty());
        QVERIFY(!desk->matching(QLatin1String(dm7::kSplitDcaStart)).isEmpty());
    }

    // ── The reason DM7 is built second ───────────────────────────────
    //
    // X32 ignores `previous` and writes a whole bitmask. DM7 must do the
    // opposite: address each (channel, DCA) pair individually, so it sends
    // ONLY the difference. One ConsoleLink call, two opposite wire forms.
    void writesOnlyTheChangedPairs()
    {
        connectAndSettle();

        link->setDcaAssignment(3, {1, 2});
        QVERIFY(awaitLines(QStringLiteral("set ") + QLatin1String(dm7::kDcaAssign), 2));
        auto writes = desk->matching(QStringLiteral("set ") + QLatin1String(dm7::kDcaAssign));
        QCOMPARE(writes.size(), 2);                       // two adds, not 24 pairs
        QVERIFY(writes.contains(QStringLiteral("set %1 2 0 1").arg(QLatin1String(dm7::kDcaAssign))));
        QVERIFY(writes.contains(QStringLiteral("set %1 2 1 1").arg(QLatin1String(dm7::kDcaAssign))));

        desk->received.clear();
        link->setDcaAssignment(3, {2, 3});                // drop DCA1, add DCA3
        QVERIFY(awaitLines(QStringLiteral("set ") + QLatin1String(dm7::kDcaAssign), 2));
        QTest::qWait(50);       // give a spurious third write a chance to show up
        writes = desk->matching(QStringLiteral("set ") + QLatin1String(dm7::kDcaAssign));

        QCOMPARE(writes.size(), 2);                       // NOT 3 — DCA2 is untouched
        QVERIFY(writes.contains(QStringLiteral("set %1 2 0 0").arg(QLatin1String(dm7::kDcaAssign))));
        QVERIFY(writes.contains(QStringLiteral("set %1 2 2 1").arg(QLatin1String(dm7::kDcaAssign))));
    }

    void indicesAreZeroBasedOnTheWire()
    {
        connectAndSettle();
        link->setDcaAssignment(1, {1});     // channel 1, DCA 1 -> "0 0"
        QVERIFY(awaitLines(QStringLiteral("set %1 0 0 1").arg(QLatin1String(dm7::kDcaAssign)), 1));
    }

    void muteUsesInvertedOnSemantics()
    {
        connectAndSettle();
        desk->received.clear();
        link->setChannelMuted(5, true);
        // muted == Fader/On 0
        QVERIFY(awaitLines(QStringLiteral("set %1 4 0 0").arg(QLatin1String(dm7::kChannelOn)), 1));
    }

    void dcaLabelIsQuoted()
    {
        connectAndSettle();
        link->setDcaLabel(2, QStringLiteral("Cast Full"));
        QVERIFY(awaitLines(QLatin1String(dm7::kDcaName), 1));
        const auto w = desk->matching(QLatin1String(dm7::kDcaName));
        QVERIFY(!w.isEmpty());
        QVERIFY(w.last().endsWith(QStringLiteral("\"Cast Full\"")));   // spaces survive
    }

    // ── Feedback-loop safety ─────────────────────────────────────────

    void notifyIsASurfaceChange()
    {
        connectAndSettle();
        QSignalSpy spy(link.get(), &ConsoleLink::surfaceDcaAssignmentChanged);

        desk->push(QStringLiteral("NOTIFY set %1 4 2 1").arg(QLatin1String(dm7::kDcaAssign)));
        QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 1000);
        QCOMPARE(link->dcaAssignment(5), DcaSet{3});   // ch 5, DCA 3 (1-based)
    }

    // The console echoes our own writes back as OK. Treating that as news is
    // how a feedback loop starts, so it must be ignored.
    void ourOwnEchoIsNotASurfaceChange()
    {
        connectAndSettle();
        QSignalSpy spy(link.get(), &ConsoleLink::surfaceDcaAssignmentChanged);

        desk->push(QStringLiteral("OK set %1 4 2 1").arg(QLatin1String(dm7::kDcaAssign)));
        QTest::qWait(80);
        QCOMPARE(spy.count(), 0);
    }

    void surfaceChangeAccumulatesPerPair()
    {
        connectAndSettle();
        // DM7 reports one pair at a time, so the link must merge them into the
        // channel's set rather than replace it.
        desk->push(QStringLiteral("NOTIFY set %1 0 0 1").arg(QLatin1String(dm7::kDcaAssign)));
        desk->push(QStringLiteral("NOTIFY set %1 0 2 1").arg(QLatin1String(dm7::kDcaAssign)));
        QTRY_COMPARE_WITH_TIMEOUT(link->dcaAssignment(1), DcaSet({1, 3}), 1000);

        desk->push(QStringLiteral("NOTIFY set %1 0 0 0").arg(QLatin1String(dm7::kDcaAssign)));
        QTRY_COMPARE_WITH_TIMEOUT(link->dcaAssignment(1), DcaSet{3}, 1000);
    }

    void splitModeIsDetected()
    {
        connectAndSettle();
        QSignalSpy spy(link.get(), &Dm7Link::splitModeDetected);
        QVERIFY(!link->isSplit());

        desk->push(QStringLiteral("OK get %1 0 0 1").arg(QLatin1String(dm7::kSplitOn)));
        QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 1000);
        QVERIFY(link->isSplit());
    }

    void sceneRecallForcesResync()
    {
        connectAndSettle();
        link->setDcaAssignment(4, {1});
        QCOMPARE(link->dcaAssignment(4), DcaSet{1});

        QSignalSpy spy(link.get(), &ConsoleLink::resyncRequired);
        desk->push(QStringLiteral("NOTIFY sscurrentt_ex scene_a \"4.00\" modified"));
        QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 1000);
        QCOMPARE(link->dcaAssignment(4), DcaSet{});
    }

    // TCP is a stream: replies split across reads, or several arrive at once.
    void handlesLinesSplitAcrossReads()
    {
        connectAndSettle();
        QSignalSpy spy(link.get(), &ConsoleLink::surfaceDcaAssignmentChanged);

        // Two complete lines in one write, plus a partial third.
        desk->push(QStringLiteral("NOTIFY set %1 0 0 1").arg(QLatin1String(dm7::kDcaAssign)));
        desk->push(QStringLiteral("NOTIFY set %1 1 0 1").arg(QLatin1String(dm7::kDcaAssign)));
        QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 2, 1000);
        QCOMPARE(link->dcaAssignment(1), DcaSet{1});
        QCOMPARE(link->dcaAssignment(2), DcaSet{1});
    }

    void unparseableLineIsIgnoredNotFatal()
    {
        connectAndSettle();
        desk->push(QStringLiteral("total garbage not a reply"));
        QTest::qWait(50);
        QCOMPARE(link->state(), ConsoleLink::State::Connected);
    }

    void errorReplyIsNonFatal()
    {
        connectAndSettle();
        QSignalSpy spy(link.get(), &ConsoleLink::errorOccurred);
        // An unknown address usually means this firmware lacks something we
        // probed for. That's information, not a disconnect.
        desk->push(QStringLiteral("ERROR get UnknownAddress"));
        QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 1000);
        QCOMPARE(link->state(), ConsoleLink::State::Connected);
    }

    void badHostFailsCleanly()
    {
        link->connectToConsole(QStringLiteral("127.0.0.1"), 1);   // nothing listening
        // Generous: a refused TCP connect isn't instant on Windows (observed
        // ~3.4 s), and this asserts that we fail cleanly, not how fast.
        QTRY_COMPARE_WITH_TIMEOUT(link->state(), ConsoleLink::State::Failed, 15000);
        QVERIFY(!link->lastError().isEmpty());
    }
};

QTEST_MAIN(Dm7LinkTests)
#include "test_dm7_link.moc"
