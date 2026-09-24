#include <QTest>
#include <QSignalSpy>

#include "mix/ConsoleLink.h"

using quewi::mix::ConsoleLink;
using quewi::mix::DcaSet;

// Records what would have gone on the wire, so the base class's cache and
// diff logic can be tested without a console or a socket.
class FakeLink : public ConsoleLink {
    Q_OBJECT
public:
    struct Write { int channel; DcaSet previous; DcaSet next; bool previousKnown; };
    QVector<Write> writes;
    QVector<QPair<int, bool>> mutes;

    void connectToConsole(const QString &, quint16) override {}
    void disconnectFromConsole() override {}
    quint16 defaultPort() const override { return 1234; }
    QString protocolName() const override { return QStringLiteral("fake"); }
    void setDcaLabel(int, const QString &) override {}
    void setChannelMuted(int channel, bool muted) override { mutes.push_back({channel, muted}); }

    void reset() { writes.clear(); mutes.clear(); }

    using ConsoleLink::noteSurfaceDcaAssignment;
    using ConsoleLink::setCapabilities;
    using ConsoleLink::forgetDcaState;

protected:
    void writeDcaAssignment(int channel, const DcaSet &previous, const DcaSet &next,
                            bool previousKnown) override
    {
        writes.push_back({channel, previous, next, previousKnown});
    }
};

class ConsoleLinkTests : public QObject {
    Q_OBJECT

    std::unique_ptr<FakeLink> link;

private slots:
    void init()
    {
        link = std::make_unique<FakeLink>();
        ConsoleLink::Capabilities caps;
        caps.channelCount = 8;
        caps.dcaCount     = 4;
        link->setCapabilities(caps);
        link->reset();
    }

    void assignmentIsCachedAndWritten()
    {
        link->setDcaAssignment(3, {1, 2});
        QCOMPARE(link->writes.size(), 1);
        QCOMPARE(link->writes[0].channel, 3);
        QCOMPARE(link->writes[0].previous, DcaSet{});
        QCOMPARE(link->writes[0].next, DcaSet({1, 2}));
        QCOMPARE(link->dcaAssignment(3), DcaSet({1, 2}));
    }

    // The X32 has no per-membership address, so it needs the whole mask and
    // therefore the previous value. The DM7 has ONLY per-membership addresses
    // and a 2880-message full sync, so it needs the diff. Handing both
    // (previous, next) is what lets one call serve two opposite protocols.
    void writeCarriesPreviousSoLinksCanDiff()
    {
        link->setDcaAssignment(3, {1, 2});
        link->reset();

        link->setDcaAssignment(3, {2, 3});
        QCOMPARE(link->writes.size(), 1);
        QCOMPARE(link->writes[0].previous, DcaSet({1, 2}));
        QCOMPARE(link->writes[0].next, DcaSet({2, 3}));
        // A DM7 link would derive: remove DCA1, add DCA3, leave DCA2 alone.
    }

    // Until we've written a channel's row (or the desk reported all of it) we
    // don't know what it holds. The first write must say so, so a diffing link
    // (DM7) sends the whole row — otherwise DCAs the desk already had the mic
    // on (from a scene, or before we connected) were never cleared.
    void firstWriteIsMarkedUnknownThenKnown()
    {
        link->setDcaAssignment(3, {1});
        QCOMPARE(link->writes.size(), 1);
        QVERIFY(!link->writes[0].previousKnown);

        link->setDcaAssignment(3, {2});
        QCOMPARE(link->writes.size(), 2);
        QVERIFY(link->writes[1].previousKnown);
    }

    // An empty cache is NOT "the desk has nothing": clearing an unknown
    // channel must still go on the wire.
    void clearingAnUnknownChannelStillWrites()
    {
        link->setDcaAssignment(6, {});
        QCOMPARE(link->writes.size(), 1);
        QVERIFY(!link->writes[0].previousKnown);
    }

    void forgettingStateMakesChannelsUnknownAgain()
    {
        link->setDcaAssignment(3, {1});
        link->forgetDcaState();            // disconnect / scene recall
        link->reset();
        link->setDcaAssignment(3, {1});    // same set — but we no longer know
        QCOMPARE(link->writes.size(), 1);
        QVERIFY(!link->writes[0].previousKnown);
    }

    // A single-pair report (DM7 NOTIFY) doesn't reveal the rest of the row.
    void partialSurfaceReportLeavesChannelUnknown()
    {
        link->noteSurfaceDcaAssignment(4, {2}, /*complete=*/false);
        link->setDcaAssignment(4, {2});
        QCOMPARE(link->writes.size(), 1);
        QVERIFY(!link->writes[0].previousKnown);

        link->reset();
        link->noteSurfaceDcaAssignment(5, {2}, /*complete=*/true);   // X32 full mask
        link->setDcaAssignment(5, {2});
        QCOMPARE(link->writes.size(), 0);  // genuinely a no-op now
    }

    void noOpWritesNothing()
    {
        link->setDcaAssignment(3, {1, 2});
        link->reset();

        link->setDcaAssignment(3, {1, 2});
        QCOMPARE(link->writes.size(), 0);   // identical set: nothing on the wire

        link->setDcaAssignment(3, {2, 1});  // order must not matter
        QCOMPARE(link->writes.size(), 0);
    }

    void outOfRangeDcasAreDroppedBeforeTheWire()
    {
        link->setDcaAssignment(3, {1, 99, 0, -5});
        QCOMPARE(link->writes.size(), 1);
        QCOMPARE(link->writes[0].next, DcaSet{1});

        // Critically, the bad values must not be cached either — otherwise a
        // later diff would try to "remove" a DCA that was never assigned.
        QCOMPARE(link->dcaAssignment(3), DcaSet{1});
    }

    void outOfRangeChannelIsIgnored()
    {
        link->setDcaAssignment(0, {1});
        link->setDcaAssignment(9, {1});      // capability says 8 channels
        link->setDcaAssignment(-1, {1});
        QCOMPARE(link->writes.size(), 0);
    }

    void sanitizedNoOpStillWritesNothing()
    {
        link->setDcaAssignment(3, {1});
        link->reset();
        // Sanitises down to {1}, which equals the cache -> no write.
        link->setDcaAssignment(3, {1, 77});
        QCOMPARE(link->writes.size(), 0);
    }

    // The whole safety property of DCA cueing: a CONTROLLED mic not named by
    // the cue is unassigned AND muted. There is no way to forget one.
    void applyCueMutesEveryControlledChannelTheCueDoesNotName()
    {
        link->applyCue({{2, DcaSet{1}}, {5, DcaSet{2, 3}}}, {1, 2, 3, 4, 5, 6});

        QCOMPARE(link->mutes.size(), 6);    // every controlled channel gets a decision
        for (const auto &[channel, muted] : link->mutes) {
            const bool named = (channel == 2 || channel == 5);
            QVERIFY2(muted == !named,
                     qPrintable(QStringLiteral("channel %1 mute wrong").arg(channel)));
        }
        QCOMPARE(link->dcaAssignment(2), DcaSet{1});
        QCOMPARE(link->dcaAssignment(5), DcaSet({2, 3}));
        QCOMPARE(link->dcaAssignment(6), DcaSet{});
    }

    // Channels the show doesn't control (the band on 7-8, say) are never
    // touched: not muted, not pulled off the DCAs the operator put them on.
    void applyCueLeavesUncontrolledChannelsAlone()
    {
        link->noteSurfaceDcaAssignment(7, {4});   // band channel, on DCA 4 by hand
        link->reset();

        link->applyCue({{2, DcaSet{1}}}, {1, 2, 3});
        for (const auto &[channel, muted] : link->mutes)
            QVERIFY2(channel <= 3, qPrintable(QStringLiteral("touched channel %1").arg(channel)));
        for (const auto &w : link->writes)
            QVERIFY(w.channel <= 3);
        QCOMPARE(link->dcaAssignment(7), DcaSet{4});
    }

    void applyCueClearsChannelsFromThePreviousCue()
    {
        link->applyCue({{2, DcaSet{1}}}, {2, 3});
        link->reset();

        link->applyCue({{3, DcaSet{1}}}, {2, 3});   // channel 2 no longer named
        QCOMPARE(link->dcaAssignment(2), DcaSet{});

        bool ch2Muted = false;
        for (const auto &[channel, muted] : link->mutes)
            if (channel == 2 && muted) ch2Muted = true;
        QVERIFY(ch2Muted);
    }

    void applyCueIgnoresUnknownChannels()
    {
        link->applyCue({{99, DcaSet{1}}}, {99});  // beyond capability
        QCOMPARE(link->dcaAssignment(99), DcaSet{});
        for (const auto &w : link->writes)
            QVERIFY(w.channel >= 1 && w.channel <= 8);
    }

    // A console-originated change updates our view but must never be echoed
    // back to the console — that's how a feedback loop starts.
    void surfaceChangeUpdatesCacheWithoutWritingBack()
    {
        QSignalSpy spy(link.get(), &ConsoleLink::surfaceDcaAssignmentChanged);

        link->noteSurfaceDcaAssignment(4, {2, 3});
        QCOMPARE(link->writes.size(), 0);              // nothing written back
        QCOMPARE(link->dcaAssignment(4), DcaSet({2, 3}));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy[0][0].toInt(), 4);
    }

    void surfaceChangeMatchingCacheIsNotReEmitted()
    {
        link->setDcaAssignment(4, {2});
        link->reset();
        QSignalSpy spy(link.get(), &ConsoleLink::surfaceDcaAssignmentChanged);

        // The console confirming what we already believe is not news.
        link->noteSurfaceDcaAssignment(4, {2});
        QCOMPARE(spy.count(), 0);
    }

    // After a surface change, our next write must diff against what the
    // console actually has — not against what we last sent.
    void writeAfterSurfaceChangeDiffsAgainstReality()
    {
        link->setDcaAssignment(4, {1});
        link->noteSurfaceDcaAssignment(4, {2, 3});   // operator moved it
        link->reset();

        link->setDcaAssignment(4, {3});
        QCOMPARE(link->writes.size(), 1);
        QCOMPARE(link->writes[0].previous, DcaSet({2, 3}));  // reality, not {1}
        QCOMPARE(link->writes[0].next, DcaSet{3});
    }

    void stateAndErrorSignals()
    {
        QSignalSpy stateSpy(link.get(), &ConsoleLink::stateChanged);
        QCOMPARE(link->state(), ConsoleLink::State::Disconnected);

        link->setDcaAssignment(1, {1});
        QCOMPARE(stateSpy.count(), 0);   // assignments don't touch state
    }
};

QTEST_MAIN(ConsoleLinkTests)
#include "test_console_link.moc"
