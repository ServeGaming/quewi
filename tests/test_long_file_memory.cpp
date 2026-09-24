#include <QTest>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTimer>

#include <limits>

#include "audio/AudioFile.h"
#include "audio/SampleStore.h"

#ifdef Q_OS_WIN
#  include <qt_windows.h>
#  include <psapi.h>
#endif

using namespace quewi::audio;

// Manual benchmark, skipped unless QUEWI_MEM_PROBE_FILE names an audio file:
// decode it the way a cue does and report what it costs in RAM. It exists
// because a 3-hour MP3 on a soundboard pad used to hold ~4 GB of decoded
// float in RAM; long files now decode into a memory-mapped cache file that
// the OS pages (see SampleStore).
//
//   set QUEWI_MEM_PROBE_FILE=C:\path\to\long.mp3
//   set QT_MEDIA_BACKEND=ffmpeg
//   test_long_file_memory.exe
class LongFileMemory : public QObject {
    Q_OBJECT

    static qint64 workingSetMB()
    {
#ifdef Q_OS_WIN
        PROCESS_MEMORY_COUNTERS_EX pmc{};
        pmc.cb = sizeof(pmc);
        if (GetProcessMemoryInfo(GetCurrentProcess(),
                                 reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&pmc), sizeof(pmc)))
            return qint64(pmc.WorkingSetSize / (1024 * 1024));
#endif
        return -1;
    }
    static qint64 privateMB()
    {
#ifdef Q_OS_WIN
        PROCESS_MEMORY_COUNTERS_EX pmc{};
        pmc.cb = sizeof(pmc);
        if (GetProcessMemoryInfo(GetCurrentProcess(),
                                 reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&pmc), sizeof(pmc)))
            return qint64(pmc.PrivateUsage / (1024 * 1024));
#endif
        return -1;
    }

private slots:
    void decodeAndMeasure()
    {
        const QString path = qEnvironmentVariable("QUEWI_MEM_PROBE_FILE");
        if (path.isEmpty()) QSKIP("Set QUEWI_MEM_PROBE_FILE to run this benchmark.");

        // QUEWI_MEM_PROBE_RAM=1 forces the old all-in-RAM path for comparison;
        // QUEWI_MEM_PROBE_SECONDS stops after that long and reports the rate.
        const bool forceRam = qEnvironmentVariableIntValue("QUEWI_MEM_PROBE_RAM") == 1;
        const int  capSecs  = qEnvironmentVariableIntValue("QUEWI_MEM_PROBE_SECONDS");
        const qint64 threshold = SampleStore::diskThresholdBytes();
        if (forceRam) SampleStore::setDiskThresholdBytes(std::numeric_limits<qint64>::max());

        const qint64 wsBefore = workingSetMB();
        AudioFile file;
        QElapsedTimer t; t.start();
        qint64 peakWs = 0;
        // A real event loop, like the app's. (QTest::qWait sleeps 10 ms between
        // event batches, which throttled decode to ~3x real-time here and
        // made it look far slower than it is.)
        QEventLoop loop;
        connect(&file, &AudioFile::stateChanged, &loop, [&] {
            if (file.state() != AudioFile::State::Loading) loop.quit();
        });
        QTimer sampler;
        sampler.setInterval(250);
        connect(&sampler, &QTimer::timeout, &loop, [&] {
            peakWs = std::max(peakWs, workingSetMB());
            if (capSecs > 0 && t.elapsed() > capSecs * 1000) loop.quit();
        });
        sampler.start();
        file.load(path);
        if (file.state() == AudioFile::State::Loading) loop.exec();
        peakWs = std::max(peakWs, workingSetMB());
        SampleStore::setDiskThresholdBytes(threshold);
        const double audioSecs = file.sampleRate() > 0
            ? double(file.frameCount()) / file.sampleRate() : 0.0;
        qInfo().noquote() << QStringLiteral(
            "\n  mode        : %1\n  decoded     : %2 min of audio in %3 s wall (%4x realtime)\n"
            "  working set : before %5 MB, peak %6 MB, now %7 MB (private %8 MB)\n"
            "  store       : capacity %9 MB, threshold %10 MB, state %11")
            .arg(file.samples().onDisk() ? QStringLiteral("disk-backed") : QStringLiteral("RAM"))
            .arg(audioSecs / 60.0, 0, 'f', 1)
            .arg(t.elapsed() / 1000.0, 0, 'f', 1)
            .arg(audioSecs / std::max(0.001, t.elapsed() / 1000.0), 0, 'f', 1)
            .arg(wsBefore).arg(peakWs).arg(workingSetMB()).arg(privateMB())
            .arg(qint64(file.samples().capacity() * sizeof(float) / (1024 * 1024)))
            .arg(SampleStore::diskThresholdBytes() / (1024 * 1024))
            .arg(int(file.state()));
        if (capSecs > 0 && file.state() == AudioFile::State::Loading) return;   // rate probe only
        QCOMPARE(file.state(), AudioFile::State::Loaded);

        const double seconds  = file.durationSeconds();
        const qint64 pcmMB    = qint64(file.samples().size() * sizeof(float) / (1024 * 1024));
        qInfo().noquote() << QStringLiteral(
            "\n  file        : %1\n  duration    : %2 min\n  decoded PCM : %3 MB (%4)\n"
            "  decode time : %5 s\n  working set : before %6 MB, peak during decode %7 MB, "
            "after %8 MB\n  private     : %9 MB\n  bytesUsed() : %10 MB")
            .arg(path)
            .arg(seconds / 60.0, 0, 'f', 1)
            .arg(pcmMB)
            .arg(file.samples().onDisk() ? QStringLiteral("disk-backed") : QStringLiteral("RAM"))
            .arg(t.elapsed() / 1000.0, 0, 'f', 1)
            .arg(wsBefore).arg(peakWs).arg(workingSetMB())
            .arg(privateMB())
            .arg(file.bytesUsed() / (1024 * 1024));

        if (qint64(file.samples().size() * sizeof(float)) > SampleStore::diskThresholdBytes()) {
            QVERIFY(file.samples().onDisk());
            // The point: nowhere near the decoded size stays resident.
            QVERIFY2(workingSetMB() - wsBefore < std::max<qint64>(256, pcmMB / 8),
                     "a disk-backed file should not keep its decoded audio resident");
        }
    }
};

QTEST_MAIN(LongFileMemory)
#include "test_long_file_memory.moc"
