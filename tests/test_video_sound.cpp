#include <QTest>
#include <QEventLoop>
#include <QTimer>

#include "audio/AudioCue.h"
#include "audio/AudioFile.h"
#include "core/CueList.h"
#include "core/UndoCommands.h"
#include "video/CueConvert.h"
#include "video/VideoCue.h"

#include <cmath>

using namespace quewi;

// Video cues' soundtracks (the show's song mixes are .mov / .mp4), and
// turning a video cue into an audio cue and back.
class VideoSoundTests : public QObject {
    Q_OBJECT

    static std::unique_ptr<video::VideoCue> sampleVideo()
    {
        auto vc = std::make_unique<video::VideoCue>();
        vc->setField(QStringLiteral("number"), 12.0);
        vc->setField(QStringLiteral("name"), QStringLiteral("Finale"));
        vc->setField(QStringLiteral("notes"), QStringLiteral("Big one"));
        vc->setField(QStringLiteral("preWait"), 1.5);
        vc->setField(QStringLiteral("filePath"), QStringLiteral("C:/show/finale.mov"));
        vc->setField(QStringLiteral("screenIndex"), 1);
        vc->setField(QStringLiteral("posX"), 0.25);
        vc->setField(QStringLiteral("posW"), 0.5);
        vc->setField(QStringLiteral("opacity"), 0.8);
        vc->setField(QStringLiteral("sound.gainDb"), -6.0);
        vc->setField(QStringLiteral("sound.fadeOutSeconds"), 4.0);
        return vc;
    }

private slots:
    void soundFollowsTheVideo()
    {
        video::VideoCue vc;
        QVERIFY2(vc.soundEnabled(), "new video cues play their sound");
        vc.setField(QStringLiteral("filePath"), QStringLiteral("C:/a/song.mp4"));
        QCOMPARE(vc.sound()->filePath(), QStringLiteral("C:/a/song.mp4"));
        vc.setField(QStringLiteral("loop"), true);
        QVERIFY(vc.sound()->loop());

        // sound.* fields reach the soundtrack, and are undoable like any field.
        core::CueList list(QStringLiteral("L"));
        QUndoStack undo;
        undo.push(new core::EditCueFieldCommand(&vc, QStringLiteral("sound.gainDb"),
                                                vc.field(QStringLiteral("sound.gainDb")), -9.0));
        QCOMPARE(vc.sound()->gainDb(), -9.0);
        undo.undo();
        QCOMPARE(vc.sound()->gainDb(), 0.0);

        QCOMPARE(video::VideoCue::audioOf(&vc), vc.sound());
        vc.setField(QStringLiteral("soundEnabled"), false);
        QVERIFY(!video::VideoCue::audioOf(&vc));
    }

    void savedAndLoaded()
    {
        auto vc = sampleVideo();
        video::VideoCue back;
        back.fromPayload(vc->toPayload());
        QVERIFY(back.soundEnabled());
        QCOMPARE(back.sound()->gainDb(), -6.0);
        QCOMPARE(back.sound()->fadeOutSeconds(), 4.0);
        QCOMPARE(back.sound()->filePath(), QStringLiteral("C:/show/finale.mov"));

        // A show saved before video sound existed stays silent.
        QJsonObject old = vc->toPayload();
        old.remove(QStringLiteral("soundEnabled"));
        old.remove(QStringLiteral("sound"));
        video::VideoCue legacy;
        legacy.fromPayload(old);
        QVERIFY(!legacy.soundEnabled());
        QCOMPARE(legacy.sound()->filePath(), QStringLiteral("C:/show/finale.mov"));
    }

    void videoToAudioAndBack()
    {
        auto vc = sampleVideo();
        const auto id = vc->id();
        auto ac = video::CueConvert::videoToAudio(*vc);
        QCOMPARE(ac->id(), id);                              // targets still find it
        QCOMPARE(ac->number(), 12.0);
        QCOMPARE(ac->name(), QStringLiteral("Finale"));
        QCOMPARE(ac->notes(), QStringLiteral("Big one"));
        QCOMPARE(ac->preWait(), 1.5);
        QCOMPARE(ac->filePath(), QStringLiteral("C:/show/finale.mov"));
        QCOMPARE(ac->gainDb(), -6.0);                        // the sound settings
        QCOMPARE(ac->fadeOutSeconds(), 4.0);
        QVERIFY(!ac->videoOrigin().isEmpty());
        QVERIFY(video::CueConvert::canConvertToVideo(*ac));

        // Edited while it's an audio cue: kept when it goes back.
        ac->setField(QStringLiteral("gainDb"), -3.0);
        ac->setField(QStringLiteral("name"), QStringLiteral("Finale (mix)"));

        auto again = video::CueConvert::audioToVideo(*ac);
        QCOMPARE(again->id(), id);
        QCOMPARE(again->name(), QStringLiteral("Finale (mix)"));
        QCOMPARE(again->filePath(), QStringLiteral("C:/show/finale.mov"));
        QCOMPARE(again->screenIndex(), 1);                   // picture settings restored
        QCOMPARE(again->posX(), 0.25);
        QCOMPARE(again->posW(), 0.5);
        QCOMPARE(again->opacity(), 0.8);
        QVERIFY(again->soundEnabled());
        QCOMPARE(again->sound()->gainDb(), -3.0);
        QVERIFY2(again->sound()->videoOrigin().isEmpty(), "no stale origin inside the sound");
    }

    void plainAudioCuesOnlyConvertWhenTheFileIsAVideo()
    {
        audio::AudioCue wav;
        wav.setField(QStringLiteral("filePath"), QStringLiteral("C:/a/door.wav"));
        QVERIFY(!video::CueConvert::canConvertToVideo(wav));
        audio::AudioCue mov;
        mov.setField(QStringLiteral("filePath"), QStringLiteral("C:/a/Song.MOV"));
        QVERIFY(video::CueConvert::canConvertToVideo(mov));
        auto vc = video::CueConvert::audioToVideo(mov);
        QCOMPARE(vc->filePath(), QStringLiteral("C:/a/Song.MOV"));
        QCOMPARE(vc->posW(), 1.0);                           // fresh full-screen picture
    }

    void replaceCommandSwapsInPlaceAndUndoes()
    {
        core::CueList list(QStringLiteral("Main"));
        list.insertCue(0, std::make_unique<audio::AudioCue>());
        list.insertCue(1, sampleVideo());
        list.insertCue(2, std::make_unique<audio::AudioCue>());
        auto *vc = qobject_cast<video::VideoCue *>(list.cueAt(1));
        const auto id = vc->id();

        QUndoStack undo;
        undo.push(new core::ReplaceCueCommand(&list, 1, video::CueConvert::videoToAudio(*vc),
                                              QStringLiteral("Convert")));
        QCOMPARE(list.cueCount(), 3);
        QVERIFY(qobject_cast<audio::AudioCue *>(list.cueAt(1)));
        QCOMPARE(list.cueAt(1)->id(), id);
        undo.undo();
        QCOMPARE(list.cueAt(1), vc);                          // the very same object back
        undo.redo();
        QVERIFY(qobject_cast<audio::AudioCue *>(list.cueAt(1)));
    }

    // The soundtrack really decodes out of a video container. Point
    // QUEWI_TEST_VIDEO_FILE at an .mp4/.mov with audio (CI has none: skips).
    void soundtrackDecodesFromAVideoFile()
    {
        const QString path = qEnvironmentVariable("QUEWI_TEST_VIDEO_FILE");
        if (path.isEmpty()) QSKIP("set QUEWI_TEST_VIDEO_FILE to a video with audio");
        video::VideoCue vc;
        vc.setField(QStringLiteral("filePath"), path);
        vc.sound()->prepare();
        auto file = vc.sound()->audioFile();
        QVERIFY(file);
        QEventLoop loop;
        connect(file.get(), &audio::AudioFile::stateChanged, &loop, [&] {
            if (file->state() != audio::AudioFile::State::Loading) loop.quit();
        });
        QTimer::singleShot(15000, &loop, &QEventLoop::quit);
        if (file->state() == audio::AudioFile::State::Loading
            || file->state() == audio::AudioFile::State::Empty) loop.exec();
        QCOMPARE(file->state(), audio::AudioFile::State::Loaded);
        QVERIFY2(file->durationSeconds() > 1.0,
                 qPrintable(QStringLiteral("decoded %1 s").arg(file->durationSeconds())));
        QVERIFY(file->sampleRate() > 0);
    }
};

QTEST_MAIN(VideoSoundTests)
#include "test_video_sound.moc"
