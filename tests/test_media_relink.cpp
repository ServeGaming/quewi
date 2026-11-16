#include <QTest>

#include "audio/AudioCue.h"
#include "core/CueList.h"
#include "core/MediaRelink.h"
#include "core/Workspace.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QUndoStack>

using namespace quewi;

// A show opened on another computer: the cues point at where the tracks
// were. Relink finds them by name in a folder (Matthew, 2026-11-15).
class MediaRelinkTests : public QObject {
    Q_OBJECT

    static void touch(const QString &path)
    {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("x");
    }
    static audio::AudioCue *add(core::CueList *list, const QString &path)
    {
        auto cue = std::make_unique<audio::AudioCue>();
        cue->setField(QStringLiteral("filePath"), path);
        auto *raw = cue.get();
        list->insertCue(list->cueCount(), std::move(cue));
        return raw;
    }

private slots:
    void findsTracksByNameInAFolderAndItsSubfolders()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString root = dir.path();
        touch(root + "/Tracks/01 Overture.wav");
        touch(root + "/Tracks/Act 2/14 Finale.WAV");
        touch(root + "/here.wav");

        core::Workspace ws;
        auto *list = ws.addCueList(std::make_unique<core::CueList>());
        // Paths from the old computer (a Mac, and a Windows drive that isn't here).
        auto *a = add(list, QStringLiteral("/Users/sm/Show/Tracks/01 Overture.wav"));
        auto *b = add(list, QStringLiteral("Q:\\Show\\Tracks\\Act 2\\14 finale.wav"));
        auto *c = add(list, QStringLiteral("Q:\\Show\\Tracks\\99 Not Here.wav"));
        auto *d = add(list, root + "/here.wav");                 // already fine
        auto *e = add(list, QString());                          // no file chosen
        auto *a2 = add(list, QStringLiteral("/Users/sm/Show/Tracks/01 Overture.wav"));

        const auto missing = core::missingMedia(&ws);
        QCOMPARE(missing.count, 4);
        QCOMPARE(missing.paths.size(), 3);

        const auto r = core::relinkMissingMedia(&ws, root + "/Tracks");
        QCOMPARE(r.relinked, 3);
        QCOMPARE(r.stillMissing, QStringList{QStringLiteral("Q:\\Show\\Tracks\\99 Not Here.wav")});
        QVERIFY(r.ambiguous.isEmpty());
        QVERIFY(QFileInfo::exists(a->filePath()));
        QVERIFY(QFileInfo::exists(a2->filePath()));
        QVERIFY(QFileInfo::exists(b->filePath()));
        QCOMPARE(QFileInfo(b->filePath()).fileName(), QStringLiteral("14 Finale.WAV"));
        QCOMPARE(c->filePath(), QStringLiteral("Q:\\Show\\Tracks\\99 Not Here.wav"));
        QCOMPARE(QDir::fromNativeSeparators(d->filePath()), root + "/here.wav");
        QVERIFY(e->filePath().isEmpty());
        QCOMPARE(core::missingMedia(&ws).count, 1);

        // One undo step puts every old location back.
        ws.undoStack()->undo();
        QCOMPARE(a->filePath(), QStringLiteral("/Users/sm/Show/Tracks/01 Overture.wav"));
        QCOMPARE(core::missingMedia(&ws).count, 4);
        QVERIFY(!ws.undoStack()->canUndo());
    }

    void theSameNameTwiceUsesTheOneInTheMatchingFolder()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString root = dir.path();
        touch(root + "/Act 1/Bows.wav");
        touch(root + "/Act 2/Bows.wav");

        core::Workspace ws;
        auto *list = ws.addCueList(std::make_unique<core::CueList>());
        auto *c = add(list, QStringLiteral("D:\\Old\\Act 2\\Bows.wav"));
        const auto r = core::relinkMissingMedia(&ws, root);
        QCOMPARE(r.relinked, 1);
        QCOMPARE(r.ambiguous, QStringList{QStringLiteral("Bows.wav")});
        QCOMPARE(QDir::fromNativeSeparators(c->filePath()), root + "/Act 2/Bows.wav");
    }

    void nothingMissingOrABadFolderChangesNothing()
    {
        core::Workspace ws;
        auto *list = ws.addCueList(std::make_unique<core::CueList>());
        add(list, QStringLiteral("Q:\\nowhere\\a.wav"));
        const auto r = core::relinkMissingMedia(&ws, QStringLiteral("Q:\\no such folder"));
        QCOMPARE(r.relinked, 0);
        QCOMPARE(r.stillMissing.size(), 1);
        QVERIFY(!ws.undoStack()->canUndo());
    }
};

QTEST_MAIN(MediaRelinkTests)
#include "test_media_relink.moc"
