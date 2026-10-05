#include <QTest>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>

#include "core/CueList.h"
#include "core/MatrixModel.h"
#include "core/Workspace.h"
#include "cues/MemoCue.h"
#include "show/ShowFile.h"

using namespace quewi;
using core::CueList;
using core::Workspace;
using show::ShowFile;
namespace m = core::matrix;

// A Matrix List through a real .quewi (SQLite) file: its settings and hand
// placements come back; shows from before it (no matrix_lists_json) open
// exactly as they did; a damaged entry doesn't stop a show opening.
class MatrixPersistenceTests : public QObject {
    Q_OBJECT

    QTemporaryDir dir;
    QString path(const char *name) const { return dir.filePath(QString::fromLatin1(name)); }

    static QString metaValue(const QString &file, const QString &key)
    {
        QString out;
        {
            auto db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("peek"));
            db.setDatabaseName(file);
            if (db.open()) {
                QSqlQuery q(db);
                q.prepare(QStringLiteral("SELECT value FROM meta WHERE key=?"));
                q.addBindValue(key);
                if (q.exec() && q.next()) out = q.value(0).toString();
                db.close();
            }
        }
        QSqlDatabase::removeDatabase(QStringLiteral("peek"));
        return out;
    }

private slots:
    void initTestCase() { QVERIFY(dir.isValid()); }

    void matrixSurvivesSaveAndLoad()
    {
        QUuid mainId, matrixId, cueId;
        m::Config cfg;
        {
            Workspace ws;
            auto main = std::make_unique<CueList>(QStringLiteral("Main"));
            auto memo = std::make_unique<cues::MemoCue>();
            memo->setField(QStringLiteral("number"), 1.0);
            cueId = memo->id();
            main->insertCue(0, std::move(memo));
            mainId = main->id();
            ws.addCueList(std::move(main));

            auto matrix = std::make_unique<CueList>(QStringLiteral("Running order"));
            matrix->setKind(CueList::Kind::Matrix);
            cfg.sourceList = mainId;
            cfg.deskList = QStringLiteral("2");
            m::DeskCue c;
            c.list = QStringLiteral("2");
            c.number = QStringLiteral("12.5");
            c.part = 1;
            c.uid = QStringLiteral("ABC");
            c.label = QStringLiteral("Sunrise");
            c.scene = QStringLiteral("Act 1");
            c.upSeconds = 5.0;
            cfg.place(c, m::ManualPlacement::Mode::With, cueId);
            cfg.deskCache = {c};
            cfg.deskCachedAt = QDateTime(QDate(2026, 10, 4), QTime(20, 0));
            matrix->setMatrixConfig(cfg);
            matrixId = matrix->id();
            ws.addCueList(std::move(matrix));
            QVERIFY2(ShowFile::save(path("matrix.quewi"), ws), qPrintable(ShowFile::lastError()));
        }
        QVERIFY(!metaValue(path("matrix.quewi"), QStringLiteral("matrix_lists_json")).isEmpty());

        Workspace ws;
        QVERIFY2(ShowFile::load(path("matrix.quewi"), ws), qPrintable(ShowFile::lastError()));
        QCOMPARE(ws.cueLists().size(), size_t(2));
        const auto &matrix = ws.cueLists()[1];
        QCOMPARE(matrix->id(), matrixId);
        QCOMPARE(matrix->kind(), CueList::Kind::Matrix);
        QCOMPARE(matrix->cueCount(), 0);
        QVERIFY(matrix->matrixConfig() == cfg);
        QCOMPARE(matrix->matrixConfig().placements[0].anchor, cueId);
        QCOMPARE(ws.cueLists()[0]->kind(), CueList::Kind::Normal);
        QVERIFY(!ws.isDirty());
    }

    // A show with no Matrix List writes no matrix key at all — byte-for-byte
    // the format 1.1.0 wrote — and reads back with every list Normal.
    void showsWithoutAMatrixAreUnchanged()
    {
        {
            Workspace ws;
            ws.addCueList(std::make_unique<CueList>(QStringLiteral("Main")));
            QVERIFY(ShowFile::save(path("plain.quewi"), ws));
        }
        QVERIFY(metaValue(path("plain.quewi"), QStringLiteral("matrix_lists_json")).isEmpty());
        Workspace ws;
        QVERIFY(ShowFile::load(path("plain.quewi"), ws));
        QCOMPARE(ws.cueLists().front()->kind(), CueList::Kind::Normal);
    }

    // A show file as 1.1.0 (and earlier) wrote it, built by hand: the
    // schema and meta keys of that version only.
    void oldShowFileLoads()
    {
        const QString file = path("old.quewi");
        {
            auto db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("old"));
            db.setDatabaseName(file);
            QVERIFY(db.open());
            QSqlQuery q(db);
            QVERIFY(q.exec(QStringLiteral("CREATE TABLE meta (key TEXT PRIMARY KEY, value TEXT)")));
            QVERIFY(q.exec(QStringLiteral("CREATE TABLE cue_lists (id TEXT PRIMARY KEY, name TEXT NOT NULL, ord INTEGER NOT NULL)")));
            QVERIFY(q.exec(QStringLiteral("CREATE TABLE cues (id TEXT PRIMARY KEY, list_id TEXT NOT NULL, "
                                          "parent_id TEXT, ord INTEGER NOT NULL, type TEXT NOT NULL, payload TEXT NOT NULL)")));
            QVERIFY(q.exec(QStringLiteral("INSERT INTO meta VALUES ('schema_version','1'), ('workspace_name','Old show')")));
            QVERIFY(q.exec(QStringLiteral("INSERT INTO cue_lists VALUES ('{11111111-1111-1111-1111-111111111111}','Main',0),"
                                          "('{22222222-2222-2222-2222-222222222222}','Soundboard',1)")));
            QVERIFY(q.exec(QStringLiteral("INSERT INTO meta VALUES ('soundboard_list_id','{22222222-2222-2222-2222-222222222222}')")));
            QVERIFY(q.exec(QStringLiteral("INSERT INTO cues VALUES ('{33333333-3333-3333-3333-333333333333}',"
                                          "'{11111111-1111-1111-1111-111111111111}',NULL,0,'memo','{\"number\":1,\"name\":\"Hello\"}')")));
            db.close();
        }
        QSqlDatabase::removeDatabase(QStringLiteral("old"));

        Workspace ws;
        QVERIFY2(ShowFile::load(file, ws), qPrintable(ShowFile::lastError()));
        QCOMPARE(ws.name(), QStringLiteral("Old show"));
        QCOMPARE(ws.cueLists().size(), size_t(2));
        QCOMPARE(ws.cueLists()[0]->kind(), CueList::Kind::Normal);
        QCOMPARE(ws.cueLists()[1]->kind(), CueList::Kind::Soundboard);
        QCOMPARE(ws.cueLists()[0]->cueCount(), 1);
        for (const auto &l : ws.cueLists()) QVERIFY(l->matrixConfig() == m::Config{});
    }

    // Garbage in the matrix key, or an entry for a list that's gone, doesn't
    // stop the show opening.
    void damagedMatrixEntryIsHarmless()
    {
        const QString file = path("damaged.quewi");
        {
            Workspace ws;
            ws.addCueList(std::make_unique<CueList>(QStringLiteral("Main")));
            QVERIFY(ShowFile::save(file, ws));
        }
        {
            auto db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("dmg"));
            db.setDatabaseName(file);
            QVERIFY(db.open());
            QSqlQuery q(db);
            QVERIFY(q.exec(QStringLiteral("INSERT INTO meta VALUES ('matrix_lists_json',"
                                          "'{\"{99999999-9999-9999-9999-999999999999}\":{\"version\":1},\"x\":5')")));
            db.close();
        }
        QSqlDatabase::removeDatabase(QStringLiteral("dmg"));
        Workspace ws;
        QVERIFY2(ShowFile::load(file, ws), qPrintable(ShowFile::lastError()));
        QCOMPARE(ws.cueLists().front()->kind(), CueList::Kind::Normal);
    }
};

QTEST_MAIN(MatrixPersistenceTests)
#include "test_matrix_persistence.moc"
