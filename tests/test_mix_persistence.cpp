#include <QTest>
#include <QTemporaryDir>

#include "core/CueList.h"
#include "core/Workspace.h"
#include "mix/MixCue.h"
#include "mix/MixShow.h"
#include "show/ShowFile.h"

using quewi::core::CueList;
using quewi::core::Workspace;
using quewi::mix::MixChannel;
using quewi::mix::MixCue;
using quewi::show::ShowFile;

// Round-trips mix data through a real .quewi (SQLite) file. The model tests
// cover toJson/fromJson; this covers the part that actually ships — that a
// saved show reopens with its mix intact.
class MixPersistenceTests : public QObject {
    Q_OBJECT

    QTemporaryDir dir;
    QString path() const { return dir.filePath(QStringLiteral("test.quewi")); }

    static void buildShow(Workspace &ws)
    {
        auto *mix = ws.mixShow();
        MixChannel a; a.strip = 1; a.name = QStringLiteral("Elphaba"); a.actor = QStringLiteral("Idina");
        MixChannel b; b.strip = 2; b.name = QStringLiteral("Glinda"); b.backupStrip = 9;
        mix->setChannel(a);
        mix->setChannel(b);
        mix->setEnsemble(QStringLiteral("Ensemble"), {4, 5, 6});

        auto list = std::make_unique<CueList>(QStringLiteral("Mix"));
        list->setKind(CueList::Kind::Mix);

        auto cue = std::make_unique<MixCue>();
        cue->setField(QStringLiteral("number"), 1.0);
        cue->setField(QStringLiteral("name"), QStringLiteral("Act 1 opening"));
        cue->setDcaStrips(1, {1});
        cue->setDcaStrips(2, {2});
        cue->setDcaEnsembles(3, {QStringLiteral("Ensemble")});
        list->insertCue(0, std::move(cue));
        ws.addCueList(std::move(list));
    }

private slots:
    void initTestCase() { QVERIFY(dir.isValid()); }

    void mixShowSurvivesSaveAndLoad()
    {
        {
            Workspace ws;
            buildShow(ws);
            QVERIFY2(ShowFile::save(path(), ws), qPrintable(ShowFile::lastError()));
        }

        Workspace ws;
        QVERIFY2(ShowFile::load(path(), ws), qPrintable(ShowFile::lastError()));

        auto *mix = ws.mixShow();
        QVERIFY(mix);
        QCOMPARE(mix->channels().size(), 2);
        QCOMPARE(mix->channel(1).name, QStringLiteral("Elphaba"));
        QCOMPARE(mix->channel(1).actor, QStringLiteral("Idina"));
        QCOMPARE(mix->channel(2).backupStrip, 9);
        QCOMPARE(mix->ensemble(QStringLiteral("Ensemble")), QSet<int>({4, 5, 6}));
    }

    // A mix list must reopen AS a mix list, or its tab renders the wrong view
    // and its cues rejoin the set list.
    void mixListKindSurvivesSaveAndLoad()
    {
        {
            Workspace ws;
            buildShow(ws);
            QVERIFY(ShowFile::save(path(), ws));
        }

        Workspace ws;
        QVERIFY(ShowFile::load(path(), ws));

        int mixLists = 0;
        for (const auto &l : ws.cueLists())
            if (l->kind() == CueList::Kind::Mix) ++mixLists;
        QCOMPARE(mixLists, 1);
    }

    void mixCueRoundTripsThroughTheCueRegistry()
    {
        {
            Workspace ws;
            buildShow(ws);
            QVERIFY(ShowFile::save(path(), ws));
        }

        Workspace ws;
        QVERIFY(ShowFile::load(path(), ws));

        MixCue *cue = nullptr;
        for (const auto &l : ws.cueLists())
            for (int i = 0; i < l->cueCount(); ++i)
                if (auto *m = qobject_cast<MixCue *>(l->cueAt(i))) cue = m;

        // If the "mix" type key weren't registered in makeCue, the loader
        // would silently substitute a Memo cue and this would be null —
        // which is exactly the failure this test exists to catch.
        QVERIFY2(cue, "mix cue came back as the wrong type; is \"mix\" registered?");
        QCOMPARE(cue->name(), QStringLiteral("Act 1 opening"));
        QCOMPARE(cue->dcaStrips(1), QSet<int>{1});
        QCOMPARE(cue->dcaStrips(2), QSet<int>{2});
        QCOMPARE(cue->dcaEnsembles(3), QStringList{QStringLiteral("Ensemble")});
        QCOMPARE(cue->assignedDcas(), (QList<int>{1, 2, 3}));
    }

    void assignmentsResolveAfterReload()
    {
        {
            Workspace ws;
            buildShow(ws);
            QVERIFY(ShowFile::save(path(), ws));
        }

        Workspace ws;
        QVERIFY(ShowFile::load(path(), ws));

        MixCue *cue = nullptr;
        for (const auto &l : ws.cueLists())
            for (int i = 0; i < l->cueCount(); ++i)
                if (auto *m = qobject_cast<MixCue *>(l->cueAt(i))) cue = m;
        QVERIFY(cue);

        // The ensemble names strips 4-6, but only channels 1 and 2 exist in
        // this show, so resolve drops them: "fewer mics open", not a crash.
        const auto assigned = cue->channelAssignments(*ws.mixShow());
        QCOMPARE(assigned.value(1), QSet<int>{1});
        QCOMPARE(assigned.value(2), QSet<int>{2});
        QVERIFY(!assigned.contains(4));
    }

    // A show with no mix data must not gain any, and must not break.
    void showWithoutMixDataLoadsClean()
    {
        {
            Workspace ws;
            auto list = std::make_unique<CueList>(QStringLiteral("Main"));
            ws.addCueList(std::move(list));
            QVERIFY(ShowFile::save(path(), ws));
        }

        Workspace ws;
        QVERIFY2(ShowFile::load(path(), ws), qPrintable(ShowFile::lastError()));
        QVERIFY(ws.mixShow());
        QVERIFY(ws.mixShow()->isEmpty());
        for (const auto &l : ws.cueLists())
            QVERIFY(l->kind() == CueList::Kind::Normal);
    }
};

QTEST_MAIN(MixPersistenceTests)
#include "test_mix_persistence.moc"
