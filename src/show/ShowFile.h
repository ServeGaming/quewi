#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <memory>

namespace quewi::core { class Workspace; }
namespace quewi::cues { class Cue; }

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

    // Last non-fatal warning string from a successful load — e.g.
    // "Recovered N cues with corrupt payloads". Empty if none.
    // Cleared at the start of each load() / save() call.
    static QString lastWarning();

    // Construct a Cue subclass from a stored type-key + payload pair.
    // Used by the show file loader and by clipboard paste in CueListView.
    // Unknown type strings produce a Memo cue with a note explaining
    // the type — same fallback the loader uses, so paste-from-future
    // never destroys data.
    static std::unique_ptr<cues::Cue> cueFromTypeAndPayload(
        const QString &type, const QJsonObject &payload);
};

} // namespace quewi::show
