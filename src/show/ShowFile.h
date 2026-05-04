#pragma once

#include <QObject>
#include <QString>

namespace quewi::core { class Workspace; }

namespace quewi::show {

// SQLite-backed show file (.quewi). Companion JSON sidecar for
// diff-friendly version control (Phase 6+). See structure.md §8.
//
// Phase 1 schema (see implementation for DDL): a single document
// per file. Patches are persisted as empty placeholders for now;
// they fill in as Phase 2-6 add output subsystems.
class ShowFile {
public:
    // Reads `path` into the (empty) workspace. Returns false on failure
    // and sets an error string accessible via lastError().
    static bool load(const QString &path, core::Workspace &workspace);

    // Writes workspace to `path`. Overwrites if it exists.
    static bool save(const QString &path, const core::Workspace &workspace);

    static QString lastError();
};

} // namespace quewi::show
