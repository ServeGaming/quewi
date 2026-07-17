#include <QTest>
#include <QSignalSpy>

#include "core/CueList.h"
#include "mix/MixCue.h"
#include "mix/MixShow.h"
#include "ui/MixGridModel.h"

using quewi::core::CueList;
using quewi::mix::MixChannel;
using quewi::mix::MixCue;
using quewi::mix::MixShow;
using quewi::ui::MixGridModel;

// The grid is what the operator actually reads during a show, and the
// change-highlighting is the part they read fastest — "what does GO change?".
// Getting a colour wrong here is a mic opening when nobody expected it.
class MixGridTests : public QObject {
    Q_OBJECT

    MixShow show;
    std::unique_ptr<CueList> list;
    std::unique_ptr<MixGridModel> model;

    MixCue *addCue(double number)
    {
        auto cue = std::make_unique<MixCue>();
        cue->setField(QStringLiteral("number"), number);
        auto *raw = cue.get();
        list->insertCue(list->cueCount(), std::move(cue));
        return raw;
    }

private slots:
    void init()
    {
        show.clear();
        for (int i = 1; i <= 6; ++i) {
            MixChannel c;
            c.strip = i;
            c.name  = QStringLiteral("Mic%1").arg(i);
            show.setChannel(c);
        }
        show.setEnsemble(QStringLiteral("Ens"), {4, 5});

        list  = std::make_unique<CueList>(QStringLiteral("Mix"));
        list->setKind(CueList::Kind::Mix);
        model = std::make_unique<MixGridModel>();
        model->setMixShow(&show);
        model->setCueList(list.get());
    }

    void cleanup() { model.reset(); list.reset(); }

    // ── Shape ────────────────────────────────────────────────────────

    void columnsFollowTheShowsDcaCount()
    {
        QCOMPARE(model->columnCount(), MixGridModel::kFixedCols + 8);   // default
        show.setDcaCount(24);
        QCOMPARE(model->columnCount(), MixGridModel::kFixedCols + 24);
    }

    void dcaColumnMapping()
    {
        QCOMPARE(model->dcaForColumn(MixGridModel::kColNumber), 0);   // not a DCA
        QCOMPARE(model->dcaForColumn(MixGridModel::kColName), 0);
        QCOMPARE(model->dcaForColumn(MixGridModel::kFixedCols), 1);   // first DCA
        QCOMPARE(model->dcaForColumn(MixGridModel::kFixedCols + 7), 8);
        QCOMPARE(model->dcaForColumn(MixGridModel::kFixedCols + 99), 0); // past the end
    }

    void rowsFollowTheCueList()
    {
        QCOMPARE(model->rowCount(), 0);
        addCue(1.0);
        addCue(2.0);
        QCOMPARE(model->rowCount(), 2);
    }

    // ── Cell text ────────────────────────────────────────────────────

    void cellShowsChannelNames()
    {
        auto *cue = addCue(1.0);
        cue->setDcaStrips(1, {1, 2});
        QCOMPARE(model->cellText(0, 1), QStringLiteral("Mic1, Mic2"));
    }

    // An ensemble reads by name, not expanded — twenty channel names would
    // blow the column apart and tell the operator less, not more.
    void cellShowsEnsembleByName()
    {
        auto *cue = addCue(1.0);
        cue->setDcaEnsembles(2, {QStringLiteral("Ens")});
        QCOMPARE(model->cellText(0, 2), QStringLiteral("Ens"));
    }

    // Programming must never silently vanish because a channel was deleted.
    void orphanedStripShowsAsItsNumber()
    {
        auto *cue = addCue(1.0);
        cue->setDcaStrips(1, {1, 99});      // 99 has no channel
        QCOMPARE(model->cellText(0, 1), QStringLiteral("Mic1, 99"));
    }

    // ── Change highlighting — the part that matters ──────────────────

    void firstCueMarksEverythingAsArriving()
    {
        auto *cue = addCue(1.0);
        cue->setDcaStrips(1, {1});
        const auto idx = model->index(0, MixGridModel::kFixedCols);
        QCOMPARE(idx.data(MixGridModel::CellChangeRole).value<MixGridModel::CellChange>(),
                 MixGridModel::CellChange::Assigned);
    }

    void carriedOverAssignmentIsUnchanged()
    {
        auto *a = addCue(1.0); a->setDcaStrips(1, {1});
        auto *b = addCue(2.0); b->setDcaStrips(1, {1});

        const auto idx = model->index(1, MixGridModel::kFixedCols);
        QCOMPARE(idx.data(MixGridModel::CellChangeRole).value<MixGridModel::CellChange>(),
                 MixGridModel::CellChange::Unchanged);
    }

    void newMicArrivingIsAssigned()
    {
        auto *a = addCue(1.0); a->setDcaStrips(1, {1});
        auto *b = addCue(2.0); b->setDcaStrips(1, {1, 2});   // Mic2 arrives

        const auto idx = model->index(1, MixGridModel::kFixedCols);
        QCOMPARE(idx.data(MixGridModel::CellChangeRole).value<MixGridModel::CellChange>(),
                 MixGridModel::CellChange::Assigned);
    }

    void everythingLeavingIsRemoved()
    {
        auto *a = addCue(1.0); a->setDcaStrips(1, {1});
        addCue(2.0);                                         // DCA1 now empty

        const auto idx = model->index(1, MixGridModel::kFixedCols);
        QCOMPARE(idx.data(MixGridModel::CellChangeRole).value<MixGridModel::CellChange>(),
                 MixGridModel::CellChange::Removed);
    }

    // A mic going live outranks a mic going away: the operator must not miss
    // an open mic, and can survive missing a closed one.
    void arrivalOutranksDepartureInASwap()
    {
        auto *a = addCue(1.0); a->setDcaStrips(1, {1});
        auto *b = addCue(2.0); b->setDcaStrips(1, {2});      // Mic1 out, Mic2 in

        const auto idx = model->index(1, MixGridModel::kFixedCols);
        QCOMPARE(idx.data(MixGridModel::CellChangeRole).value<MixGridModel::CellChange>(),
                 MixGridModel::CellChange::Assigned);
    }

    void partialDepartureIsModified()
    {
        auto *a = addCue(1.0); a->setDcaStrips(1, {1, 2});
        auto *b = addCue(2.0); b->setDcaStrips(1, {1});      // Mic2 leaves, nothing arrives

        const auto idx = model->index(1, MixGridModel::kFixedCols);
        QCOMPARE(idx.data(MixGridModel::CellChangeRole).value<MixGridModel::CellChange>(),
                 MixGridModel::CellChange::Modified);
    }

    // Change is computed on RESOLVED strips, so an ensemble that expands to
    // the same mics reads as unchanged even if it's written differently.
    void changeComparesResolvedStripsNotSyntax()
    {
        auto *a = addCue(1.0); a->setDcaStrips(1, {4, 5});
        auto *b = addCue(2.0); b->setDcaEnsembles(1, {QStringLiteral("Ens")});  // == {4,5}

        const auto idx = model->index(1, MixGridModel::kFixedCols);
        QCOMPARE(idx.data(MixGridModel::CellChangeRole).value<MixGridModel::CellChange>(),
                 MixGridModel::CellChange::Unchanged);
    }

    // ── Editing ──────────────────────────────────────────────────────

    void editAcceptsChannelNames()
    {
        auto *cue = addCue(1.0);
        QVERIFY(model->setData(model->index(0, MixGridModel::kFixedCols),
                               QStringLiteral("Mic1, Mic3"), Qt::EditRole));
        QCOMPARE(cue->dcaStrips(1), QSet<int>({1, 3}));
    }

    void editAcceptsStripNumbers()
    {
        auto *cue = addCue(1.0);
        QVERIFY(model->setData(model->index(0, MixGridModel::kFixedCols),
                               QStringLiteral("2, 4"), Qt::EditRole));
        QCOMPARE(cue->dcaStrips(1), QSet<int>({2, 4}));
    }

    void editAcceptsEnsembleNames()
    {
        auto *cue = addCue(1.0);
        QVERIFY(model->setData(model->index(0, MixGridModel::kFixedCols),
                               QStringLiteral("Ens"), Qt::EditRole));
        QCOMPARE(cue->dcaEnsembles(1), QStringList{QStringLiteral("Ens")});
    }

    void editIsCaseInsensitive()
    {
        auto *cue = addCue(1.0);
        QVERIFY(model->setData(model->index(0, MixGridModel::kFixedCols),
                               QStringLiteral("mic1, ENS"), Qt::EditRole));
        QCOMPARE(cue->dcaStrips(1), QSet<int>{1});
        QCOMPARE(cue->dcaEnsembles(1), QStringList{QStringLiteral("Ens")});
    }

    // A typo must not silently open a mic. Dropping the token is the safe
    // failure; inventing a strip number is not.
    void editDropsUnknownTokens()
    {
        auto *cue = addCue(1.0);
        QVERIFY(model->setData(model->index(0, MixGridModel::kFixedCols),
                               QStringLiteral("Mic1, Elphba"), Qt::EditRole));
        QCOMPARE(cue->dcaStrips(1), QSet<int>{1});   // the typo is gone, not guessed
        QVERIFY(cue->dcaEnsembles(1).isEmpty());
    }

    void editClearsWhenEmptied()
    {
        auto *cue = addCue(1.0);
        cue->setDcaStrips(1, {1, 2});
        QVERIFY(model->setData(model->index(0, MixGridModel::kFixedCols),
                               QString(), Qt::EditRole));
        QVERIFY(cue->dcaStrips(1).isEmpty());
    }

    void editEmitsCueEdited()
    {
        addCue(1.0);
        QSignalSpy spy(model.get(), &MixGridModel::cueEdited);
        model->setData(model->index(0, MixGridModel::kFixedCols),
                       QStringLiteral("Mic1"), Qt::EditRole);
        QCOMPARE(spy.count(), 1);
    }

    void editingNameAndNumberWorks()
    {
        auto *cue = addCue(1.0);
        QVERIFY(model->setData(model->index(0, MixGridModel::kColName),
                               QStringLiteral("Act 1 Sc 2"), Qt::EditRole));
        QCOMPARE(cue->name(), QStringLiteral("Act 1 Sc 2"));

        QVERIFY(model->setData(model->index(0, MixGridModel::kColNumber),
                               QStringLiteral("12.5"), Qt::EditRole));
        QCOMPARE(cue->number(), 12.5);

        // Non-numeric cue number is refused, not silently zeroed.
        QVERIFY(!model->setData(model->index(0, MixGridModel::kColNumber),
                                QStringLiteral("banana"), Qt::EditRole));
        QCOMPARE(cue->number(), 12.5);
    }

    void headersNameTheDcas()
    {
        QCOMPARE(model->headerData(MixGridModel::kColNumber, Qt::Horizontal).toString(),
                 QStringLiteral("Cue"));
        QCOMPARE(model->headerData(MixGridModel::kFixedCols, Qt::Horizontal).toString(),
                 QStringLiteral("DCA 1"));
        QCOMPARE(model->headerData(MixGridModel::kFixedCols + 7, Qt::Horizontal).toString(),
                 QStringLiteral("DCA 8"));
    }
};

QTEST_MAIN(MixGridTests)
#include "test_mix_grid.moc"
