#include <QTest>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QProcess>
#include <QTemporaryDir>
#include <cstdlib>
#include <cstring>

#include "show/JournalLock.h"

using namespace quewi::show;

namespace {

void touch(const QString &path, const QByteArray &data = "journal")
{
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(data);
}

QStringList paths(const std::vector<OrphanedJournal> &v)
{
    QStringList out;
    for (const auto &j : v) out << QFileInfo(j.path).fileName();
    return out;
}

} // namespace

// Journal ownership across quewi instances: a journal a running instance is
// writing must never be offered for recovery (or deleted); one a crashed
// instance left behind must be.
class JournalLockTests : public QObject {
    Q_OBJECT
private slots:
    void unlockedJournalIsOrphaned() {
        QTemporaryDir dir;
        touch(dir.filePath("a.journal"));
        auto found = claimOrphanedJournals(dir.path());
        QCOMPARE(paths(found), QStringList{"a.journal"});
        // Claiming holds the lock, so the journal can be adopted safely.
        QVERIFY(found.front().lock && found.front().lock->isLocked());
        QVERIFY(QFile::exists(dir.filePath("a.journal.lock")));
    }

    void liveJournalIsSkipped() {
        QTemporaryDir dir;
        const QString live = dir.filePath("live.journal");
        auto owner = lockJournal(live); // "another instance", still running
        QVERIFY(owner);
        touch(live);
        touch(dir.filePath("dead.journal"));

        auto found = claimOrphanedJournals(dir.path());
        QCOMPARE(paths(found), QStringList{"dead.journal"});
        QVERIFY(QFile::exists(live));
        QVERIFY(owner->isLocked());
    }

    void liveLockIsNotStaleWithAge() {
        // A show left open for days keeps its journal however old the lock
        // file is: only a dead owner makes a lock stale, never age.
        QTemporaryDir dir;
        const QString live = dir.filePath("live.journal");
        auto owner = lockJournal(live);
        QVERIFY(owner);
        touch(live);
#ifdef Q_OS_WIN
        // The owner's QLockFile keeps the lock file open with no sharing, so
        // nothing else can even open it to back-date it — a live lock on
        // Windows can't be aged (or removed) from outside at all.
        QFile probe(journalLockPath(live));
        QVERIFY(!probe.open(QIODevice::ReadWrite));
        QVERIFY(claimOrphanedJournals(dir.path()).empty());
        QSKIP("can't back-date a lock file its owner holds open (Windows)");
#endif
        QFile lockFile(journalLockPath(live));
        QVERIFY(lockFile.open(QIODevice::ReadWrite));
        QVERIFY(lockFile.setFileTime(QDateTime::currentDateTime().addDays(-2),
                                     QFileDevice::FileModificationTime));
        lockFile.close();
        QVERIFY(claimOrphanedJournals(dir.path()).empty());
    }

    void crashedOwnerIsRecoverable() {
        // A child process takes the lock and exits without releasing it —
        // exactly what a crash leaves on disk.
        QTemporaryDir dir;
        const QString journal = dir.filePath("crashed.journal");
        touch(journal);
        QProcess child;
        child.start(QCoreApplication::applicationFilePath(),
                    {QStringLiteral("--hold-lock-and-die"), journal});
        QVERIFY(child.waitForFinished(10000));
        QCOMPARE(child.exitCode(), 0);
        QVERIFY2(QFile::exists(journalLockPath(journal)), "child left no lock behind");

        auto found = claimOrphanedJournals(dir.path());
        QCOMPARE(paths(found), QStringList{"crashed.journal"});
    }

    void secondClaimantGetsNothing() {
        // Two instances starting at once: only one may claim a journal.
        QTemporaryDir dir;
        touch(dir.filePath("a.journal"));
        auto first = claimOrphanedJournals(dir.path());
        QCOMPARE(first.size(), size_t(1));
        QVERIFY(claimOrphanedJournals(dir.path()).empty());
        first.clear(); // released
        QCOMPARE(claimOrphanedJournals(dir.path()).size(), size_t(1));
    }

    void newestFirst() {
        QTemporaryDir dir;
        touch(dir.filePath("old.journal"));
        touch(dir.filePath("new.journal"));
        QFile old(dir.filePath("old.journal"));
        QVERIFY(old.open(QIODevice::ReadWrite));
        QVERIFY(old.setFileTime(QDateTime::currentDateTime().addSecs(-3600),
                                QFileDevice::FileModificationTime));
        old.close();
        QCOMPARE(paths(claimOrphanedJournals(dir.path())),
                 (QStringList{"new.journal", "old.journal"}));
    }

    void releasingRemovesLockFile() {
        QTemporaryDir dir;
        const QString j = dir.filePath("a.journal");
        auto lock = lockJournal(j);
        QVERIFY(lock);
        QVERIFY(QFile::exists(journalLockPath(j)));
        lock.reset();
        QVERIFY(!QFile::exists(journalLockPath(j)));
    }

    void strayLocksAreTidied() {
        QTemporaryDir dir;
        // Stale: a dead owner never wrote its journal.
        {
            QProcess child;
            child.start(QCoreApplication::applicationFilePath(),
                        {QStringLiteral("--hold-lock-and-die"), dir.filePath("gone.journal")});
            QVERIFY(child.waitForFinished(10000));
        }
        QVERIFY(QFile::exists(dir.filePath("gone.journal.lock")));
        // Live: an owner that hasn't written its first journal yet.
        auto pending = lockJournal(dir.filePath("pending.journal"));
        QVERIFY(pending);

        QVERIFY(claimOrphanedJournals(dir.path()).empty());
        QVERIFY(!QFile::exists(dir.filePath("gone.journal.lock")));
        QVERIFY(QFile::exists(dir.filePath("pending.journal.lock")));
        QVERIFY(pending->isLocked());
    }
};

int main(int argc, char **argv)
{
    // Child mode for the crash tests: lock, then exit without unlocking.
    if (argc == 3 && std::strcmp(argv[1], "--hold-lock-and-die") == 0) {
        QCoreApplication app(argc, argv);
        auto lock = lockJournal(QString::fromLocal8Bit(argv[2]));
        if (!lock) return 1;
        std::_Exit(0); // no destructors: the lock file stays, owner is gone
    }
    QCoreApplication app(argc, argv);
    JournalLockTests t;
    return QTest::qExec(&t, argc, argv);
}

#include "test_journal_lock.moc"
