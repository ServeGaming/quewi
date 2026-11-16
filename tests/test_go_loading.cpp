#include <QTest>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtEndian>

#include "GoEngine.h"
#include "audio/AudioCue.h"
#include "audio/AudioEngine.h"
#include "core/CueList.h"
#include "core/Workspace.h"

using namespace quewi;

// GO on a track whose file hasn't decoded yet used to say "audio still
// decoding" and play nothing: the cue was skipped mid-show. Now GO waits for
// the first audio and plays it.
namespace {

// A plain 16-bit PCM WAV: a second of silence (the test may really play it,
// and it mustn't make a sound on whoever's machine runs it).
QString writeWav(const QString &dir)
{
    const int rate = 48000, chans = 2, frames = rate;
    QByteArray pcm(frames * chans * 2, '\0');
    auto le32 = [](quint32 v) { QByteArray b(4, '\0'); qToLittleEndian(v, b.data()); return b; };
    auto le16 = [](quint16 v) { QByteArray b(2, '\0'); qToLittleEndian(v, b.data()); return b; };
    QByteArray f = "RIFF" + le32(36 + pcm.size()) + "WAVEfmt " + le32(16) + le16(1) + le16(chans)
                 + le32(rate) + le32(rate * chans * 2) + le16(chans * 2) + le16(16)
                 + "data" + le32(pcm.size()) + pcm;
    const QString path = dir + QStringLiteral("/tone.wav");
    QFile out(path);
    if (!out.open(QIODevice::WriteOnly)) return {};
    out.write(f);
    return path;
}

struct Rig {
    core::Workspace ws;
    audio::AudioEngine audio;
    GoEngine go;
    audio::AudioCue *cue = nullptr;
    explicit Rig(const QString &wav)
    {
        auto *list = ws.cueLists().empty()
                   ? ws.addCueList(std::make_unique<core::CueList>(QStringLiteral("Main")))
                   : ws.cueLists().front().get();
        auto c = std::make_unique<audio::AudioCue>();
        c->setField(QStringLiteral("name"), QStringLiteral("Tone"));
        c->setField(QStringLiteral("filePath"), wav);   // not decoded yet
        cue = c.get();
        list->insertCue(0, std::move(c));
        go.setWorkspace(&ws);
        go.setAudioEngine(&audio);
    }
};

bool any(const QSignalSpy &spy, const QString &part)
{
    for (const auto &args : spy)
        if (args.value(0).toString().contains(part)) return true;
    return false;
}

} // namespace

class GoLoadingTests : public QObject {
    Q_OBJECT
private slots:
    void goOnATrackStillLoadingPlaysItWhenReady()
    {
        QTemporaryDir dir;
        const QString wav = writeWav(dir.path());
        QVERIFY(!wav.isEmpty());
        Rig r(wav);
        QSignalSpy status(&r.go, &GoEngine::statusMessage);

        r.go.fire(r.cue);   // the decoder has delivered nothing yet
        QVERIFY2(any(status, QStringLiteral("loading")), "GO should wait for the file");
        QVERIFY(!any(status, QStringLiteral("still decoding")));

        // Once the first audio is in, the same GO plays it: a voice, or (on
        // a machine with no sound device, like CI) the engine saying why.
        QTRY_VERIFY_WITH_TIMEOUT(any(status, QStringLiteral("▶"))
                                     || any(status, QStringLiteral("audio engine failed")), 10000);
        QVERIFY(!any(status, QStringLiteral("didn't load")));
    }

    void twoGosWhileLoadingStartItOnce()
    {
        QTemporaryDir dir;
        Rig r(writeWav(dir.path()));
        QSignalSpy status(&r.go, &GoEngine::statusMessage);
        r.go.fire(r.cue);
        r.go.fire(r.cue);
        QTRY_VERIFY_WITH_TIMEOUT(any(status, QStringLiteral("▶"))
                                     || any(status, QStringLiteral("audio engine failed")), 10000);
        QTest::qWait(200);
        int started = 0;
        for (const auto &args : status) {
            const QString m = args.value(0).toString();
            if (m.contains(QStringLiteral("▶")) || m.contains(QStringLiteral("audio engine failed"))) ++started;
        }
        QCOMPARE(started, 1);
    }

    void panicWhileLoadingCancelsIt()
    {
        QTemporaryDir dir;
        Rig r(writeWav(dir.path()));
        QSignalSpy status(&r.go, &GoEngine::statusMessage);
        r.go.fire(r.cue);
        r.go.cancelAll();
        QTRY_VERIFY_WITH_TIMEOUT(r.cue->audioFile() && r.cue->audioFile()->snapshot(), 10000);
        QTest::qWait(100);
        QVERIFY(!any(status, QStringLiteral("▶")));
        QCOMPARE(r.cue->currentVoiceId(), quint64(0));
    }

    void aStopCueWhileLoadingCancelsIt()
    {
        QTemporaryDir dir;
        Rig r(writeWav(dir.path()));
        QSignalSpy status(&r.go, &GoEngine::statusMessage);
        r.go.fire(r.cue);
        r.go.stopCue(r.cue);
        QTRY_VERIFY_WITH_TIMEOUT(r.cue->audioFile() && r.cue->audioFile()->snapshot(), 10000);
        QTest::qWait(100);
        QVERIFY(!any(status, QStringLiteral("▶")));
    }
};

QTEST_MAIN(GoLoadingTests)
#include "test_go_loading.moc"
