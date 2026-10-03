#pragma once

#include <QString>
#include <memory>
#include <vector>

class QLockFile;

namespace quewi::show {

// Crash-journal ownership. Every running quewi holds "<journal>.lock" for as
// long as its journal exists, so another instance starting up can tell a live
// journal (someone is still writing it) from one a crashed session left
// behind. Without this, a second quewi offered to "recover" the first one's
// live journal — and "No" deleted it out from under the running show.
//
// Locks are QLockFiles with stale-by-age turned off: a lock only goes stale
// when its owning process is gone (crashed, killed, power loss), never just
// because the show has been open a long time.

// The lock file that guards `journalPath`.
QString journalLockPath(const QString &journalPath);

// Takes the lock for a journal this process is about to write. Returns null
// if another live process already holds it (shouldn't happen for a fresh
// UUID name). Destroying the returned lock releases it and removes the file.
std::unique_ptr<QLockFile> lockJournal(const QString &journalPath);

struct OrphanedJournal {
    QString path;
    std::unique_ptr<QLockFile> lock; // held — keep it to adopt the journal
};

// Every *.journal in `dir` that no live process owns, newest first, each
// with its lock now held by the caller (so two instances starting at once
// can't both claim the same one). Journals locked by a running instance are
// left out entirely — never recover or delete those. Lock files whose
// journal is already gone are tidied away if nobody holds them.
std::vector<OrphanedJournal> claimOrphanedJournals(const QString &dir);

} // namespace quewi::show
