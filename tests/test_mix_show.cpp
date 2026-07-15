#include <QTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "mix/MixCue.h"
#include "mix/MixShow.h"

using quewi::mix::DcaSet;
using quewi::mix::Ensemble;
using quewi::mix::MixChannel;
using quewi::mix::MixCue;
using quewi::mix::MixShow;

class MixShowTests : public QObject {
    Q_OBJECT

    static MixChannel ch(int strip, const QString &name)
    {
        MixChannel c;
        c.strip = strip;
        c.name  = name;
        return c;
    }

    MixShow show;

private slots:
    void init()
    {
        show.clear();
        show.setChannel(ch(1, QStringLiteral("Elphaba")));
        show.setChannel(ch(2, QStringLiteral("Glinda")));
        show.setChannel(ch(3, QStringLiteral("Fiyero")));
        show.setChannel(ch(4, QStringLiteral("Ens W 1")));
        show.setChannel(ch(5, QStringLiteral("Ens W 2")));
        show.setEnsemble(QStringLiteral("Ensemble Women"), {4, 5});
    }

    // ── Channels ─────────────────────────────────────────────────────

    void channelsAreOrderedByStrip()
    {
        show.clear();
        show.setChannel(ch(7, QStringLiteral("g")));
        show.setChannel(ch(2, QStringLiteral("b")));
        show.setChannel(ch(5, QStringLiteral("e")));

        const auto list = show.channels();
        QCOMPARE(list.size(), 3);
        QCOMPARE(list[0].strip, 2);
        QCOMPARE(list[1].strip, 5);
        QCOMPARE(list[2].strip, 7);
    }

    void invalidChannelIsRejected()
    {
        show.clear();
        show.setChannel(ch(0, QStringLiteral("nope")));
        show.setChannel(ch(-1, QStringLiteral("nope")));
        QVERIFY(show.channels().isEmpty());
    }

    // A deleted channel must not linger as a ghost ensemble member — that
    // would open a mic with no owner.
    void removingAChannelPurgesItFromEnsembles()
    {
        show.removeChannel(4);
        QCOMPARE(show.ensemble(QStringLiteral("Ensemble Women")), Ensemble{5});
        QVERIFY(!show.hasChannel(4));
    }

    void removingAChannelClearsItAsABackup()
    {
        MixChannel c = show.channel(1);
        c.backupStrip = 3;
        show.setChannel(c);
        QCOMPARE(show.channel(1).backupStrip, 3);

        show.removeChannel(3);
        QCOMPARE(show.channel(1).backupStrip, 0);
    }

    // ── resolve ──────────────────────────────────────────────────────

    void resolveExpandsEnsembles()
    {
        const auto got = show.resolve({1}, {QStringLiteral("Ensemble Women")});
        QCOMPARE(got, QSet<int>({1, 4, 5}));
    }

    // A cue referencing a deleted ensemble should mean "fewer mics open",
    // never a crash or a stuck-open mic.
    void resolveDropsUnknownEnsemblesAndDeadStrips()
    {
        QCOMPARE(show.resolve({1}, {QStringLiteral("No Such Ensemble")}), QSet<int>{1});
        QCOMPARE(show.resolve({1, 99}, {}), QSet<int>{1});   // 99 has no channel
    }

    void resolveDeduplicatesOverlap()
    {
        // Strip 4 named explicitly AND via the ensemble.
        QCOMPARE(show.resolve({4}, {QStringLiteral("Ensemble Women")}), QSet<int>({4, 5}));
    }

    // ── Recasting ────────────────────────────────────────────────────

    void reassignStripMovesChannelIdentityAndReferences()
    {
        MixChannel c = show.channel(1);
        c.backupStrip = 3;
        show.setChannel(c);

        const int touched = show.reassignStrip(4, 9);
        QVERIFY(touched > 0);
        QVERIFY(!show.hasChannel(4));
        QCOMPARE(show.channel(9).name, QStringLiteral("Ens W 1"));
        QCOMPARE(show.ensemble(QStringLiteral("Ensemble Women")), Ensemble({5, 9}));
    }

    void reassignStripToItselfIsANoOp()
    {
        QCOMPARE(show.reassignStrip(1, 1), 0);
        QCOMPARE(show.reassignStrip(0, 5), 0);
    }

    // ── Persistence ──────────────────────────────────────────────────

    void jsonRoundTrips()
    {
        MixChannel c = show.channel(1);
        c.actor       = QStringLiteral("Idina");
        c.backupStrip = 6;
        show.setChannel(c);

        const auto json = show.toJson();
        MixShow other;
        other.fromJson(json);

        QCOMPARE(other.channels().size(), show.channels().size());
        QCOMPARE(other.channel(1).name, QStringLiteral("Elphaba"));
        QCOMPARE(other.channel(1).actor, QStringLiteral("Idina"));
        QCOMPARE(other.channel(1).backupStrip, 6);
        QCOMPARE(other.ensemble(QStringLiteral("Ensemble Women")), Ensemble({4, 5}));
    }

    void jsonOutputIsStableForDiffing()
    {
        // Show files go in version control; a set's iteration order must not
        // leak into the file or every save churns the diff.
        const auto a = QJsonDocument(show.toJson()).toJson();
        MixShow other;
        other.fromJson(show.toJson());
        const auto b = QJsonDocument(other.toJson()).toJson();
        QCOMPARE(a, b);
    }

    void corruptJsonIsSkippedNotFatal()
    {
        QJsonObject bad;
        QJsonArray chans;
        chans.append(QJsonObject{{"strip", 0}});          // invalid
        chans.append(QJsonObject{{"strip", 3}, {"name", "ok"}});
        chans.append(QJsonObject{{"nonsense", true}});    // no strip
        bad["channels"] = chans;

        MixShow other;
        other.fromJson(bad);
        QCOMPARE(other.channels().size(), 1);
        QCOMPARE(other.channel(3).name, QStringLiteral("ok"));
    }
};

// ── MixCue ───────────────────────────────────────────────────────────

class MixCueTests : public QObject {
    Q_OBJECT

    MixShow show;

    void buildShow()
    {
        show.clear();
        for (int i = 1; i <= 6; ++i) {
            MixChannel c;
            c.strip = i;
            c.name  = QStringLiteral("Mic %1").arg(i);
            show.setChannel(c);
        }
        show.setEnsemble(QStringLiteral("Ens"), {4, 5, 6});
    }

private slots:
    void init() { buildShow(); }

    void typeKeyIsStable()
    {
        MixCue cue;
        QCOMPARE(cue.typeKey(), QStringLiteral("mix"));  // persisted — don't change
    }

    // The core inversion: the operator thinks DCA-first, the console needs
    // channel-first.
    void invertsDcaViewIntoChannelView()
    {
        MixCue cue;
        cue.setDcaStrips(1, {1});
        cue.setDcaStrips(2, {2, 3});

        const auto got = cue.channelAssignments(show);
        QCOMPARE(got.value(1), DcaSet{1});
        QCOMPARE(got.value(2), DcaSet{2});
        QCOMPARE(got.value(3), DcaSet{2});
        QVERIFY(!got.contains(4));
    }

    void resolvesEnsemblesAtFireTime()
    {
        MixCue cue;
        cue.setDcaEnsembles(3, {QStringLiteral("Ens")});

        auto got = cue.channelAssignments(show);
        QCOMPARE(got.value(4), DcaSet{3});
        QCOMPARE(got.value(6), DcaSet{3});

        // Editing the ensemble must update every cue that uses it, rather
        // than each cue baking in the membership it had when written.
        show.setEnsemble(QStringLiteral("Ens"), {4});
        got = cue.channelAssignments(show);
        QCOMPARE(got.value(4), DcaSet{3});
        QVERIFY(!got.contains(6));
    }

    void aChannelCanSitOnTwoDcas()
    {
        MixCue cue;
        cue.setDcaStrips(1, {1});
        cue.setDcaStrips(2, {1, 2});

        const auto got = cue.channelAssignments(show);
        QCOMPARE(got.value(1), DcaSet({1, 2}));   // legal: both protocols allow it
    }

    void emptyDcaEntriesAreNotKept()
    {
        MixCue cue;
        cue.setDcaStrips(1, {1});
        QCOMPARE(cue.assignedDcas(), QList<int>{1});

        cue.setDcaStrips(1, {});
        QVERIFY(cue.isEmpty());
        QVERIFY(cue.assignedDcas().isEmpty());
    }

    void assignedDcasAreSorted()
    {
        MixCue cue;
        cue.setDcaStrips(5, {1});
        cue.setDcaStrips(2, {2});
        cue.setDcaStrips(8, {3});
        QCOMPARE(cue.assignedDcas(), (QList<int>{2, 5, 8}));
    }

    void invalidDcaIsRejected()
    {
        MixCue cue;
        cue.setDcaStrips(0, {1});
        cue.setDcaStrips(-3, {1});
        QVERIFY(cue.isEmpty());
    }

    void reassignStripRewritesTheCue()
    {
        MixCue cue;
        cue.setDcaStrips(1, {1, 2});
        QVERIFY(cue.reassignStrip(1, 9));
        QCOMPARE(cue.dcaStrips(1), QSet<int>({2, 9}));
        QVERIFY(!cue.reassignStrip(77, 9));   // absent -> no change
    }

    void payloadRoundTrips()
    {
        MixCue cue;
        cue.setDcaStrips(1, {1, 2});
        cue.setDcaEnsembles(3, {QStringLiteral("Ens")});

        MixCue other;
        other.fromPayload(cue.toPayload());

        QCOMPARE(other.dcaStrips(1), QSet<int>({1, 2}));
        QCOMPARE(other.dcaEnsembles(3), QStringList{QStringLiteral("Ens")});
        QCOMPARE(other.assignedDcas(), (QList<int>{1, 3}));
    }

    void corruptPayloadIsSkipped()
    {
        QJsonObject payload;
        QJsonObject dcas;
        dcas["not a number"] = QJsonObject{};
        dcas["0"]  = QJsonObject{};                                   // invalid dca
        dcas["2"]  = QJsonObject{{"strips", QJsonArray{3, 0, -1}}};   // 0/-1 dropped
        payload["dcas"] = dcas;

        MixCue cue;
        cue.fromPayload(payload);
        QCOMPARE(cue.assignedDcas(), QList<int>{2});
        QCOMPARE(cue.dcaStrips(2), QSet<int>{3});
    }
};

// Two suites in one binary — QTEST_MAIN only handles one, so drive both by hand.
int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    int status = 0;
    { MixShowTests t; status |= QTest::qExec(&t, argc, argv); }
    { MixCueTests  t; status |= QTest::qExec(&t, argc, argv); }
    return status;
}

#include "test_mix_show.moc"
