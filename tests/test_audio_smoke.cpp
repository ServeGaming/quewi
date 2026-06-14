#include <QSignalSpy>
#include <QTest>
#include <QTemporaryFile>
#include <QFile>
#include <QJsonObject>

#include "audio/AudioEditorModel.h"
#include "audio/AudioEngine.h"
#include "audio/AudioFile.h"

#include <cmath>

using namespace quewi::audio;

namespace {

// Write a minimal 16-bit PCM WAV file: 1 second of 440 Hz at 48 kHz, mono.
QString writeSineWav()
{
    auto *f = new QTemporaryFile(QStringLiteral("quewi-test-XXXXXX.wav"));
    f->setAutoRemove(false);
    if (!f->open()) return {};
    const int sr = 48000;
    const int frames = sr;
    const int chans = 1;
    const int bytesPerSample = 2;
    const int byteRate = sr * chans * bytesPerSample;
    const int blockAlign = chans * bytesPerSample;
    const int dataSize = frames * blockAlign;

    auto write32 = [&](quint32 v) { f->write(reinterpret_cast<const char *>(&v), 4); };
    auto write16 = [&](quint16 v) { f->write(reinterpret_cast<const char *>(&v), 2); };

    f->write("RIFF", 4);
    write32(36 + dataSize);
    f->write("WAVE", 4);
    f->write("fmt ", 4);
    write32(16);                  // fmt chunk size
    write16(1);                   // PCM
    write16(static_cast<quint16>(chans));
    write32(sr);
    write32(byteRate);
    write16(static_cast<quint16>(blockAlign));
    write16(16);                  // bits per sample
    f->write("data", 4);
    write32(dataSize);
    for (int i = 0; i < frames; ++i) {
        const double t = static_cast<double>(i) / sr;
        const qint16 s = static_cast<qint16>(std::sin(2.0 * M_PI * 440.0 * t) * 16000);
        f->write(reinterpret_cast<const char *>(&s), 2);
    }
    const QString path = f->fileName();
    f->close();
    delete f;
    return path;
}

} // namespace

class AudioSmokeTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase()
    {
        m_wav = writeSineWav();
        QVERIFY(!m_wav.isEmpty());
    }
    void cleanupTestCase()
    {
        QFile::remove(m_wav);
    }

    void decodesSineFile()
    {
        AudioFile f;
        QSignalSpy spy(&f, &AudioFile::stateChanged);
        f.load(m_wav);
        QVERIFY(spy.wait(5000));
        // Wait until Loaded or Failed.
        while (f.state() == AudioFile::State::Loading) {
            QVERIFY(spy.wait(5000));
        }
        QCOMPARE(f.state(), AudioFile::State::Loaded);
        QCOMPARE(f.sampleRate(), 48000);
        QCOMPARE(f.channelCount(), 1);
        QVERIFY(f.frameCount() > 47000); // some Qt backends round
        QVERIFY(!f.peaks().empty());
    }

    void splitRegionIsUndoable()
    {
        AudioEditorModel model;
        model.initFromFile(m_wav, 48000);
        QCOMPARE(model.trackCount(), 1);
        auto *tr = model.track(0);
        QVERIFY(tr);
        QCOMPARE(int(tr->regions().size()), 1);

        // The decode is async; wait until the region's source file is Loaded
        // so durationSamples()/timelineEndSamples() are meaningful.
        auto *file = tr->regions()[0].sourceFile.get();
        QVERIFY(file);
        QSignalSpy spy(file, &AudioFile::stateChanged);
        while (file->state() == AudioFile::State::Loading)
            QVERIFY(spy.wait(5000));
        QCOMPARE(file->state(), AudioFile::State::Loaded);

        const QUuid   origId  = tr->regions()[0].id;
        const qint64  origOut = tr->regions()[0].srcOutSamples; // -1 = end of file
        const qint64  end     = tr->regions()[0].timelineEndSamples();
        QVERIFY(end > 0);
        const qint64  splitAt = end / 2;

        // Split: one region becomes two, original now ends at the split point.
        model.splitRegion(origId, splitAt);
        QCOMPARE(int(tr->regions().size()), 2);
        {
            auto [ti, ri] = model.findRegion(origId);
            QVERIFY(ti >= 0);
            // srcIn was 0 and timelinePos 0, so the cut offset equals splitAt.
            QCOMPARE(tr->regions()[ri].srcOutSamples, splitAt);
        }
        QVERIFY(model.undoStack()->canUndo());

        // Undo merges the two halves back into the single original region.
        model.undoStack()->undo();
        QCOMPARE(int(tr->regions().size()), 1);
        {
            auto [ti, ri] = model.findRegion(origId);
            QVERIFY(ti >= 0);
            QCOMPARE(tr->regions()[ri].srcOutSamples, origOut);
        }

        // Redo re-splits to the same state.
        model.undoStack()->redo();
        QCOMPARE(int(tr->regions().size()), 2);
        {
            auto [ti, ri] = model.findRegion(origId);
            QVERIFY(ti >= 0);
            QCOMPARE(tr->regions()[ri].srcOutSamples, splitAt);
        }
    }

    // Regions/tracks/gains/fades must survive toJson → fromJson so the
    // editor's working state persists in the cue payload across reload.
    void editorModelRoundTripsThroughJson()
    {
        AudioEditorModel model;
        model.initFromFile(m_wav, 48000);
        auto *tr = model.track(0);
        QVERIFY(tr);

        auto *file = tr->regions()[0].sourceFile.get();
        QVERIFY(file);
        QSignalSpy spy(file, &AudioFile::stateChanged);
        while (file->state() == AudioFile::State::Loading)
            QVERIFY(spy.wait(5000));
        QCOMPARE(file->state(), AudioFile::State::Loaded);

        // Representative edits: split, per-region gain + fade, and
        // track-level name / volume / mute.
        const QUuid  origId  = tr->regions()[0].id;
        const qint64 splitAt = tr->regions()[0].timelineEndSamples() / 2;
        model.splitRegion(origId, splitAt);
        QCOMPARE(int(tr->regions().size()), 2);

        QUuid rightId;
        for (auto &r : tr->regions()) if (r.id != origId) rightId = r.id;
        QVERIFY(!rightId.isNull());
        model.setRegionGain(rightId, -6.0f);
        {
            auto [ti, ri] = model.findRegion(rightId);
            QVERIFY(ti >= 0);
            tr->regions()[ri].fadeIn.durationSamples = 1024;
            tr->regions()[ri].fadeIn.type = FadeCurve::EqualPower;
        }
        tr->setName(QStringLiteral("Music"));
        tr->setVolume(0.5f);
        tr->setMuted(true);

        // Round-trip through JSON into a fresh model.
        const QJsonObject json = model.toJson();
        AudioEditorModel restored;
        restored.fromJson(json);

        QCOMPARE(restored.sampleRate(), 48000);
        QCOMPARE(restored.trackCount(), 1);
        auto *rtr = restored.track(0);
        QVERIFY(rtr);
        QCOMPARE(rtr->name(), QStringLiteral("Music"));
        QCOMPARE(rtr->volume(), 0.5f);
        QVERIFY(rtr->isMuted());
        QCOMPARE(int(rtr->regions().size()), 2);

        // Left half kept its id + cut point; right half kept gain + fade.
        {
            auto [ti, ri] = restored.findRegion(origId);
            QVERIFY(ti >= 0);
            QCOMPARE(rtr->regions()[ri].srcOutSamples, splitAt);
        }
        {
            auto [ti, ri] = restored.findRegion(rightId);
            QVERIFY(ti >= 0);
            QCOMPARE(rtr->regions()[ri].gainDb, -6.0f);
            QCOMPARE(rtr->regions()[ri].fadeIn.durationSamples, qint64(1024));
            QCOMPARE(int(rtr->regions()[ri].fadeIn.type), int(FadeCurve::EqualPower));
        }
    }

private:
    QString m_wav;
};

QTEST_MAIN(AudioSmokeTests)
#include "test_audio_smoke.moc"
