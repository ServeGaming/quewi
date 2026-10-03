#include "JournalLock.h"

#include <QDir>
#include <QFileInfo>
#include <QLockFile>
#include <algorithm>

namespace quewi::show {

namespace {

std::unique_ptr<QLockFile> tryTake(const QString &journalPath)
{
    auto lock = std::make_unique<QLockFile>(journalLockPath(journalPath));
    // Age never makes a lock stale: a show can sit open for days. Only a
    // dead owner does (QLockFile checks the PID it wrote).
    lock->setStaleLockTime(0);
    if (!lock->tryLock(0)) return nullptr;
    return lock;
}

} // namespace

QString journalLockPath(const QString &journalPath)
{
    return journalPath + QStringLiteral(".lock");
}

std::unique_ptr<QLockFile> lockJournal(const QString &journalPath)
{
    return tryTake(journalPath);
}

std::vector<OrphanedJournal> claimOrphanedJournals(const QString &dir)
{
    QDir d(dir);
    std::vector<OrphanedJournal> out;
    for (const auto &name : d.entryList({QStringLiteral("*.journal")}, QDir::Files)) {
        const QString path = d.filePath(name);
        if (auto lock = tryTake(path))
            out.push_back({path, std::move(lock)});
    }
    std::sort(out.begin(), out.end(), [](const OrphanedJournal &a, const OrphanedJournal &b) {
        return QFileInfo(a.path).lastModified() > QFileInfo(b.path).lastModified();
    });

    // A lock with no journal: the owner hadn't written yet (live — leave it)
    // or died before it did (stale — take and drop it, which deletes it).
    const QString suffix = QStringLiteral(".journal.lock");
    for (const auto &name : d.entryList({QStringLiteral("*") + suffix}, QDir::Files)) {
        const QString journal = d.filePath(name.chopped(5)); // drop ".lock"
        if (!QFileInfo::exists(journal)) tryTake(journal);
    }
    return out;
}

} // namespace quewi::show
