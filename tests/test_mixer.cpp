#include <QTest>

#include "audio/AudioEngine.h"
#include "audio/AudioFile.h"

#include <cmath>
#include <memory>

using namespace quewi::audio;

// The real-time mixer, rendered offline (OfflineRenderer drives the exact
// Mixer the sound card pulls from). Every case pins down a bug the 2026-09
// audit found in this loop, by reading the output samples themselves.
//
// Signals are mono at the output rate (resample ratio exactly 1) and the pan
// is centre, whose constant-power gain is exactly 1 per side — so an output
// sample equals source sample x gain x envelope, and can be checked exactly.
class MixerTests : public QObject {
    Q_OBJECT

    static constexpr int kSr = 48000;

    static std::shared_ptr<AudioFile> dc(double seconds, float level = 0.5f)
    {
        auto f = std::make_shared<AudioFile>();
        f->loadFromSamples(std::vector<float>(size_t(seconds * kSr), level), 1, kSr);
        return f;
    }
    // Every frame a distinct value, so a read position can be identified.
    static std::shared_ptr<AudioFile> ramp(int frames)
    {
        std::vector<float> s(static_cast<size_t>(frames));
        for (int i = 0; i < frames; ++i) s[size_t(i)] = float(i) / float(frames);
        auto f = std::make_shared<AudioFile>();
        f->loadFromSamples(std::move(s), 1, kSr);
        return f;
    }
    static float L(const std::vector<float> &out, int frame) { return out[size_t(frame) * 2]; }

private slots:
    void fadeCueHoldsItsTargetLevel()
    {
        // A Fade cue used to ramp down and then snap straight back to the
        // level the cue was fired at.
        OfflineRenderer r(kSr, 2);
        VoiceParams p;
        const auto id = r.fire(dc(3.0), p);
        QVERIFY(id != 0);
        QVERIFY(std::abs(L(r.render(256), 100) - 0.5f) < 1e-4f);

        r.fadeGain(id, -20.0, 0.05);            // -20 dB = x0.1
        r.render(kSr / 10);                     // past the 50 ms ramp
        const auto after = r.render(4096);      // and well after it
        QVERIFY2(std::abs(L(after, 0)    - 0.05f) < 1e-3f, "level after the fade");
        QVERIFY2(std::abs(L(after, 4000) - 0.05f) < 1e-3f, "…and it STAYS there");
    }

    void zeroLengthFadeStillApplies()
    {
        OfflineRenderer r(kSr, 2);
        const auto id = r.fire(dc(2.0), VoiceParams{});
        r.render(128);
        r.fadeGain(id, -20.0, 0.0);
        const auto out = r.render(1024);
        QVERIFY(std::abs(L(out, 1000) - 0.05f) < 1e-3f);
    }

    void stopEndsAPausedVoice()
    {
        // A paused voice used to ignore Stop/Panic and live forever.
        OfflineRenderer r(kSr, 2);
        const auto id = r.fire(dc(3.0), VoiceParams{});
        r.render(512);
        QVERIFY(r.pause(id));
        r.render(512);
        QCOMPARE(r.activeCount(), 1);
        r.stopAll(0.05);
        r.render(512);
        QCOMPARE(r.activeCount(), 0);
    }

    void fadedStopEndsInSilenceNotAClick()
    {
        // The last frame of a faded stop used to go out at full level.
        OfflineRenderer r(kSr, 2);
        const auto id = r.fire(dc(3.0), VoiceParams{});
        r.render(256);
        r.stop(id, 0.001);                      // 48-frame fade
        const auto out = r.render(256);
        QVERIFY(L(out, 0) > 0.4f);              // starts near full level
        for (int f = 48; f < 256; ++f)
            QVERIFY2(std::abs(L(out, f)) < 1e-6f,
                     qPrintable(QStringLiteral("frame %1 not silent").arg(f)));
        for (int f = 1; f < 48; ++f)            // and the ramp only goes down
            QVERIFY(L(out, f) <= L(out, f - 1) + 1e-6f);
    }

    void fadeOutSettingFadesTheNaturalEnd()
    {
        // The cue's Fade Out used to be stored and never applied.
        OfflineRenderer r(kSr, 2);
        VoiceParams p;
        p.fadeOutSeconds = 0.5;
        r.fire(dc(1.0), p);
        const auto out = r.render(kSr);         // the whole second
        QVERIFY2(std::abs(L(out, kSr / 4)     - 0.5f)  < 1e-3f, "untouched before the fade");
        QVERIFY2(std::abs(L(out, kSr * 3 / 4) - 0.25f) < 1e-2f, "halfway down the fade");
        QVERIFY2(std::abs(L(out, kSr - 2)) < 1e-2f,            "silent at the end");
    }

    void fadeInRunsFromTrimInNotFileStart()
    {
        // Fade-in used to be measured from the start of the FILE: with
        // trim-in >= fade-in there was no fade-in at all.
        OfflineRenderer r(kSr, 2);
        VoiceParams p;
        p.trimInSeconds = 1.0;
        p.fadeInSeconds = 0.1;                  // 4800 frames
        r.fire(dc(3.0), p);
        const auto out = r.render(9600);
        QVERIFY2(std::abs(L(out, 0)) < 1e-3f,              "starts silent");
        QVERIFY2(std::abs(L(out, 2400) - 0.25f) < 1e-3f,   "halfway up at 50 ms");
        QVERIFY2(std::abs(L(out, 6000) - 0.5f) < 1e-3f,    "full level after the fade");
    }

    void loopWrapsToTrimInWithoutGapsOrRefade()
    {
        // Loops used to wrap to frame 0 (replaying trimmed-off audio), read
        // held samples past the end for the rest of the buffer, and re-apply
        // the fade-in on every pass.
        const int N = 8000;
        OfflineRenderer r(kSr, 2);
        VoiceParams p;
        p.loop = true;
        p.trimInSeconds  = 1000.0 / kSr;        // frame 1000
        p.trimOutSeconds = 2000.0 / kSr;        // frame 2000 → loop length 1000
        p.fadeInSeconds  = 480.0 / kSr;
        r.fire(ramp(N), p);
        const auto out = r.render(3500);        // 3.5 passes in one buffer
        auto src = [&](int frame) { return float(frame) / float(N); };
        // Past the first-pass fade-in, output frame k is source 1000 + k%1000.
        for (int k : {600, 999, 1000, 1001, 1999, 2000, 2500, 3499}) {
            QVERIFY2(std::abs(L(out, k) - src(1000 + k % 1000)) < 1e-5f,
                     qPrintable(QStringLiteral("output frame %1").arg(k)));
        }
        // The second pass's first frames are NOT faded in again.
        QVERIFY(std::abs(L(out, 1010) - src(1010)) < 1e-5f);
        // And the next buffer carries on seamlessly (read position kept).
        const auto next = r.render(10);
        QVERIFY(std::abs(L(next, 0) - src(1000 + 3500 % 1000)) < 1e-5f);
    }

    void oneShotFinishesAtTrimOut()
    {
        OfflineRenderer r(kSr, 2);
        VoiceParams p;
        p.trimOutSeconds = 0.01;                // 480 frames
        r.fire(dc(1.0), p);
        const auto out = r.render(1000);
        QVERIFY(std::abs(L(out, 400) - 0.5f) < 1e-4f);
        QVERIFY(std::abs(L(out, 600)) < 1e-6f);
        QCOMPARE(r.activeCount(), 0);
    }
};

QTEST_MAIN(MixerTests)
#include "test_mixer.moc"
