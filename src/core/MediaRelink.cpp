#include "core/MediaRelink.h"

#include "core/CueList.h"
#include "core/UndoCommands.h"
#include "core/Workspace.h"
#include "cues/Cue.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QHash>
#include <QUndoStack>

namespace quewi::core {

namespace {

const QString kField = QStringLiteral("filePath");

template <typename Fn>
void forEachMissing(const Workspace *ws, Fn fn)
{
    if (!ws) return;
    for (const auto &list : ws->cueLists()) {
        for (int row = 0; row < list->cueCount(); ++row) {
            auto *c = list->cueAt(row);
            if (!c) continue;
            const QString path = c->field(kField).toString();
            if (path.isEmpty() || QFileInfo::exists(path)) continue;
            fn(c, path);
        }
    }
}

// File name of a path written on any system (a show from a Mac has '/',
// from Windows '\').
QString baseName(const QString &path)
{
    const int cut = int(std::max(path.lastIndexOf(QLatin1Char('/')), path.lastIndexOf(QLatin1Char('\\'))));
    return path.mid(cut + 1);
}

QStringList parts(const QString &path)
{
    QString p = path;
    p.replace(QLatin1Char('\\'), QLatin1Char('/'));
    return p.split(QLatin1Char('/'), Qt::SkipEmptyParts);
}

// How many trailing folders two paths share, below the file name.
int sharedTail(const QString &a, const QString &b)
{
    const QStringList pa = parts(a), pb = parts(b);
    int n = 0;
    for (int i = int(pa.size()) - 2, j = int(pb.size()) - 2; i >= 0 && j >= 0; --i, --j) {
        if (pa[i].compare(pb[j], Qt::CaseInsensitive) != 0) break;
        ++n;
    }
    return n;
}

} // namespace

MissingMedia missingMedia(const Workspace *ws)
{
    MissingMedia out;
    forEachMissing(ws, [&](cues::Cue *, const QString &path) {
        ++out.count;
        if (!out.paths.contains(path)) out.paths.append(path);
    });
    return out;
}

RelinkResult relinkMissingMedia(Workspace *ws, const QString &folder)
{
    RelinkResult out;
    if (!ws || folder.isEmpty() || !QFileInfo(folder).isDir()) {
        out.stillMissing = missingMedia(ws).paths;
        return out;
    }

    // Every file under the folder, by lower-cased name.
    QHash<QString, QStringList> found;
    QDirIterator it(folder, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString p = it.next();
        found[QFileInfo(p).fileName().toLower()].append(p);
    }
    for (auto &list : found) list.sort();

    struct Change { cues::Cue *cue; QString from, to; };
    QList<Change> changes;
    forEachMissing(ws, [&](cues::Cue *c, const QString &path) {
        const QString name = baseName(path);
        const auto hit = found.constFind(name.toLower());
        if (hit == found.constEnd()) {
            if (!out.stillMissing.contains(path)) out.stillMissing.append(path);
            return;
        }
        QString best = hit->first();
        if (hit->size() > 1) {
            int bestScore = -1;
            for (const QString &cand : *hit) {
                const int s = sharedTail(path, cand);
                if (s > bestScore) { bestScore = s; best = cand; }
            }
            if (!out.ambiguous.contains(name)) out.ambiguous.append(name);
        }
        changes.append({c, path, QDir::toNativeSeparators(best)});
    });
    if (changes.isEmpty()) return out;

    auto *stack = ws->undoStack();
    stack->beginMacro(QCoreApplication::translate("MediaRelink", "Relink %n file(s)", nullptr,
                                                  int(changes.size())));
    for (const auto &ch : changes)
        stack->push(new EditCueFieldCommand(ch.cue, kField, ch.from, ch.to));
    stack->endMacro();
    out.relinked = int(changes.size());
    return out;
}

} // namespace quewi::core
