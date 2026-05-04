#include <QSignalSpy>
#include <QTest>
#include <QTemporaryFile>
#include <QFile>

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

private:
    QString m_wav;
};

QTEST_MAIN(AudioSmokeTests)
#include "test_audio_smoke.moc"
