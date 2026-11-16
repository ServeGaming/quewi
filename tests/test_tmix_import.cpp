#include <QTest>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QUndoStack>

#include "core/CueList.h"
#include "core/Workspace.h"
#include "mix/MixCue.h"
#include "mix/MixShow.h"
#include "mix/TmixImport.h"

using namespace quewi;

namespace {

// A .tmix laid out the way TheatreMix 3 writes one (the columns are the ones
// seen in a real file; 12 DCAs, the later ones added on the end).
QString makeTmix(const QString &dir)
{
    const QString path = dir + QStringLiteral("/show.tmix");
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("make"));
        db.setDatabaseName(path);
        if (!db.open()) return {};
        QSqlQuery q(db);
        QString cols = QStringLiteral("number INTEGER NOT NULL DEFAULT 999, point INTEGER NOT NULL DEFAULT 0, name TEXT");
        for (int i = 1; i <= 8; ++i) cols += QStringLiteral(", dca%1Channels TEXT").arg(i, 2, 10, QLatin1Char('0'));
        for (int i = 1; i <= 8; ++i) cols += QStringLiteral(", dca%1Label TEXT").arg(i, 2, 10, QLatin1Char('0'));
        cols += QStringLiteral(", channelPositions TEXT, skip INTEGER");
        for (int i = 9; i <= 12; ++i)
            cols += QStringLiteral(", dca%1Channels TEXT, dca%1Label TEXT").arg(i, 2, 10, QLatin1Char('0'));
        q.exec(QStringLiteral("CREATE TABLE cues (%1)").arg(cols));
        // Out of order on purpose: the reader sorts by number.
        q.exec(QStringLiteral("INSERT INTO cues (number, point, name, dca01Channels, dca02Channels, dca01Label) "
                              "VALUES (2, 5, 'Elphaba enters', '1', '8,2', 'ELPH')"));
        q.exec(QStringLiteral("INSERT INTO cues (number, point, name, dca01Channels, dca02Channels, dca03Channels) "
                              "VALUES (1, 0, 'Top of show', '1', '8, 2', '3')"));
        q.exec(QStringLiteral("INSERT INTO cues (number, point, name, dca01Channels, dca10Channels) "
                              "VALUES (3, 0, 'Big number', '1', '20;21')"));
        q.exec(QStringLiteral("CREATE TABLE config (param TEXT PRIMARY KEY, value TEXT)"));
        q.exec(QStringLiteral("INSERT INTO config VALUES ('targetConsole', 'X32')"));
        q.exec(QStringLiteral("INSERT INTO config VALUES ('channels', '1,2,3,8')"));
        q.exec(QStringLiteral("CREATE TABLE profiles (id INTEGER PRIMARY KEY AUTOINCREMENT, channel INTEGER, "
                              "name TEXT, label TEXT, \"default\" INTEGER DEFAULT 0, data TEXT)"));
        q.exec(QStringLiteral("INSERT INTO profiles (channel, name, label, \"default\") VALUES (1, 'Elphaba', NULL, 1)"));
        q.exec(QStringLiteral("INSERT INTO profiles (channel, name, label, \"default\") VALUES (1, 'Elphaba belt', 'Belt', 0)"));
        q.exec(QStringLiteral("INSERT INTO profiles (channel, name, label, \"default\") VALUES (2, 'Channel 2', NULL, 1)"));
        q.exec(QStringLiteral("INSERT INTO profiles (channel, name, label, \"default\") VALUES (8, 'x', 'Glinda', 1)"));
        q.exec(QStringLiteral("CREATE TABLE actors (id INTEGER PRIMARY KEY AUTOINCREMENT, channel INTEGER, name TEXT, "
                              "\"order\" INTEGER DEFAULT 0, active INTEGER DEFAULT 0)"));
        q.exec(QStringLiteral("INSERT INTO actors (channel, name) VALUES (1, 'Sam Smith')"));
        q.exec(QStringLiteral("CREATE TABLE ensembles (id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT, channels TEXT)"));
        q.exec(QStringLiteral("INSERT INTO ensembles (name, channels) VALUES ('Ozians', '20,21')"));
        db.close();
    }
    QSqlDatabase::removeDatabase(QStringLiteral("make"));
    return path;
}

} // namespace

class TmixImportTests : public QObject {
    Q_OBJECT
private slots:
    void readsCuesChannelsAndEnsembles()
    {
        QTemporaryDir dir;
        const QString path = makeTmix(dir.path());
        QVERIFY(!path.isEmpty());

        mix::TmixShow show;
        QString error;
        QVERIFY2(mix::readTmix(path, &show, &error), qPrintable(error));
        QCOMPARE(show.console, QStringLiteral("X32"));
        QCOMPARE(show.cues.size(), 3);
        QCOMPARE(show.cues[0].number, 1.0);
        QCOMPARE(show.cues[0].name, QStringLiteral("Top of show"));
        QCOMPARE(show.cues[0].dcas.value(2), (QSet<int>{2, 8}));
        QCOMPARE(show.cues[1].number, 2.5);                 // number 2, point 5
        QCOMPARE(show.cues[1].labels.value(1), QStringLiteral("ELPH"));
        QCOMPARE(show.cues[2].dcas.value(10), (QSet<int>{20, 21}));
        QCOMPARE(show.highestDca, 10);
        QCOMPARE(show.strips, (QSet<int>{1, 2, 3, 8, 20, 21}));
        QCOMPARE(show.channelNames.value(1), QStringLiteral("Elphaba"));   // the default row, not "Belt"
        QCOMPARE(show.channelNames.value(8), QStringLiteral("Glinda"));    // label beats name
        QVERIFY(!show.channelNames.contains(2));                           // "Channel 2" is no name
        QCOMPARE(show.actors.value(1), QStringLiteral("Sam Smith"));
        QCOMPARE(show.ensembles.value(QStringLiteral("Ozians")), (QSet<int>{20, 21}));
    }

    void appliesAsOneUndoAndKeepsWhatsThere()
    {
        QTemporaryDir dir;
        mix::TmixShow show;
        QString error;
        QVERIFY(mix::readTmix(makeTmix(dir.path()), &show, &error));

        core::Workspace ws;
        auto *mixShow = ws.mixShow();
        mixShow->setChannel({8, QStringLiteral("Galinda"), {}, 0});    // the operator's own name stays
        auto created = std::make_unique<core::CueList>(QStringLiteral("Mix"));
        created->setKind(core::CueList::Kind::Mix);
        auto *list = ws.addCueList(std::move(created));

        const auto done = mix::applyTmix(show, &ws, list);
        QCOMPARE(done.cues, 3);
        QCOMPARE(list->cueCount(), 3);
        QCOMPARE(mixShow->dcaCount(), 10);
        QCOMPARE(mixShow->channel(1).name, QStringLiteral("Elphaba"));
        QCOMPARE(mixShow->channel(1).actor, QStringLiteral("Sam Smith"));
        QCOMPARE(mixShow->channel(8).name, QStringLiteral("Galinda"));
        QVERIFY(mixShow->hasChannel(21));
        QCOMPARE(mixShow->ensemble(QStringLiteral("Ozians")), (QSet<int>{20, 21}));

        auto *c = qobject_cast<mix::MixCue *>(list->cueAt(1));
        QVERIFY(c);
        QCOMPARE(c->number(), 2.5);
        QCOMPARE(c->name(), QStringLiteral("Elphaba enters"));
        QCOMPARE(c->dcaStrips(2), (QSet<int>{2, 8}));
        QCOMPARE(c->assignedDcas(), (QList<int>{1, 2}));

        ws.undoStack()->undo();                 // one step takes all the cues back out
        QCOMPARE(list->cueCount(), 0);
    }

    // A real TheatreMix file, when one is pointed at (not checked in: they're
    // other people's shows). QUEWI_TMIX_SAMPLE=path/to/show.tmix
    void readsARealFileWhenGivenOne()
    {
        const QString path = qEnvironmentVariable("QUEWI_TMIX_SAMPLE");
        if (path.isEmpty()) QSKIP("set QUEWI_TMIX_SAMPLE to a .tmix to check a real file");
        mix::TmixShow show;
        QString error;
        QVERIFY2(mix::readTmix(path, &show, &error), qPrintable(error));
        QVERIFY(!show.cues.isEmpty());
        qInfo("%s: %d cues, %d strips, up to DCA %d, console %s", qPrintable(path),
              int(show.cues.size()), int(show.strips.size()), show.highestDca, qPrintable(show.console));
        for (const auto &c : show.cues) {
            QStringList d;
            for (int k = 1; k <= show.highestDca; ++k)
                if (c.dcas.contains(k)) {
                    QList<int> s(c.dcas[k].cbegin(), c.dcas[k].cend());
                    std::sort(s.begin(), s.end());
                    QStringList n;
                    for (int x : s) n << QString::number(x);
                    d << QStringLiteral("DCA%1=%2").arg(k).arg(n.join(QLatin1Char(',')));
                }
            qInfo("  cue %g %s  %s", c.number, qPrintable(c.name), qPrintable(d.join(QLatin1Char(' '))));
        }
    }

    void refusesWhatIsntATheatreMixFile()
    {
        QTemporaryDir dir;
        const QString notSql = dir.path() + QStringLiteral("/song.tmix");
        QFile f(notSql);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("ID3 this is an mp3");
        f.close();
        mix::TmixShow show;
        QString error;
        QVERIFY(!mix::readTmix(notSql, &show, &error));
        QVERIFY2(error.contains(QStringLiteral("isn't a TheatreMix")), qPrintable(error));
        QVERIFY(!mix::readTmix(dir.path() + QStringLiteral("/missing.tmix"), &show, &error));

        // An SQLite file with no cues table.
        const QString other = dir.path() + QStringLiteral("/other.tmix");
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("other"));
            db.setDatabaseName(other);
            QVERIFY(db.open());
            QSqlQuery(db).exec(QStringLiteral("CREATE TABLE t (x)"));
            db.close();
        }
        QSqlDatabase::removeDatabase(QStringLiteral("other"));
        QVERIFY(!mix::readTmix(other, &show, &error));
        QVERIFY2(error.contains(QStringLiteral("no cues")), qPrintable(error));
    }
};

QTEST_MAIN(TmixImportTests)
#include "test_tmix_import.moc"
