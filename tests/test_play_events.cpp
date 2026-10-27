#include <QTest>

#include "audio/AudioEngine.h"
#include "audio/AudioFile.h"
#include "audio/PlayEvents.h"

#include <QJsonDocument>

#include <cmath>
#include <memory>

using namespace quewi::audio;

// Vamps, repeats, holds and skips (Stage|Tracks-style), played by the real
// mixer offline. The source is a ramp — every frame a distinct value — so
// the output says exactly where in the song playback is; positions are read
// back from the voice, and Advance is the engine's.
class PlayEventsTests : public QObject {
    Q_OBJECT

    static constexpr int kSr = 48000;
    static constexpr double kLen = 6.0;

    static std::shared_ptr<AudioFile> ramp(double seconds = kLen)
    {
        const int frames = int(seconds * kSr);
        std::vector<float> s(static_cast<size_t>(frames));
        for (int i = 0; i < frames; ++i) s[size_t(i)] = float(i) / float(frames);
        auto f = std::make_shared<AudioFile>();
        f->loadFromSamples(std::move(s), 1, kSr);
        return f;
    }
    static PlayEvent vamp(double a, double b, int count = 0, PlayEvent::Exit exit = PlayEvent::Exit::EndOfPass)
    {
        PlayEvent e;
        e.id = QUuid::createUuid();
        e.kind = PlayEvent::Kind::Vamp;
        e.start = a; e.end = b; e.count = count; e.exit = exit;
        return e;
    }
    static PlayEvent ev(PlayEvent::Kind k, double a, double b = -1)
    {
        PlayEvent e;
        e.id = QUuid::createUuid();
        e.kind = k;
        e.start = a; e.end = b < 0 ? a : b;
        return e;
    }
    static ActiveVoice state(OfflineRenderer &r)
    {
        const auto v = r.activeVoices();
        return v.isEmpty() ? ActiveVoice{} : v.first();
    }
    // Render `seconds` in 10 ms buffers; returns the left channel.
    static std::vector<float> play(OfflineRenderer &r, double seconds)
    {
        std::vector<float> left;
        const int blocks = int(std::round(seconds * 100));
        for (int b = 0; b < blocks; ++b) {
            const auto out = r.render(kSr / 100);
            for (size_t i = 0; i < out.size(); i += 2) left.push_back(out[i]);
        }
        return left;
    }
    static double pos(OfflineRenderer &r) { return state(r).positionSeconds; }

private slots:
    void vampLoopsUntilAdvanceThenFinishesThePass()
    {
        OfflineRenderer r(kSr, 2);
        VoiceParams p;
        p.playEvents = {vamp(1.0, 2.0)};
        const auto id = r.fire(ramp(), p);
        play(r, 3.55);                                    // 1 s in, then 2.55 passes
        QCOMPARE(state(r).playState, ActiveVoice::PlayState::Vamping);
        QCOMPARE(state(r).playEventIndex, 0);
        QVERIFY(state(r).playPass >= 2);
        QVERIFY2(std::abs(pos(r) - 1.55) < 0.02, qPrintable(QString::number(pos(r))));

        QVERIFY(r.advance(id));
        play(r, 0.1);
        QCOMPARE(state(r).playState, ActiveVoice::PlayState::Leaving);
        // End of pass: it plays on to 2.0 and through, without looping back.
        const auto out = play(r, 0.6);
        QVERIFY2(std::abs(pos(r) - 2.25) < 0.02, qPrintable(QString::number(pos(r))));
        QCOMPARE(state(r).playState, ActiveVoice::PlayState::Normal);
        for (size_t i = 1; i < out.size(); ++i)
            QVERIFY2(out[i] >= out[i - 1] - 1e-6f, "plays straight through the vamp's end once Advance is pressed");
    }

    void theLoopPointIsCrossfaded()
    {
        OfflineRenderer r(kSr, 2);
        VoiceParams p;
        p.playEvents = {vamp(1.0, 2.0)};
        r.fire(ramp(), p);
        const auto out = play(r, 3.5);                    // two seams
        // Without the pre-roll the ramp would jump down by 1 s's worth
        // (1/6 = 0.167) at each seam; crossfaded it never steps more than a
        // hair from one sample to the next.
        float worst = 0.f;
        for (size_t i = 1; i < out.size(); ++i) worst = std::max(worst, std::abs(out[i] - out[i - 1]));
        QVERIFY2(worst < 0.005f, qPrintable(QString::number(worst)));
    }

    // Loop points anywhere, not just on a buffer edge (a vamp ending exactly
    // on one was once missed and played straight through).
    void loopsWhereverTheBufferEdgesFall()
    {
        for (const double end : {2.0, 2.0137, 2.0052, 1.9999}) {
            OfflineRenderer r(kSr, 2);
            VoiceParams p;
            p.playEvents = {vamp(1.0, end)};
            r.fire(ramp(), p);
            play(r, 4.0);
            QVERIFY2(state(r).playState == ActiveVoice::PlayState::Vamping,
                     qPrintable(QStringLiteral("vamp to %1").arg(end)));
            QVERIFY(pos(r) >= 1.0 && pos(r) < end);
        }
    }

    void aRepeatPlaysItsCountThenGoesOnByItself()
    {
        OfflineRenderer r(kSr, 2);
        VoiceParams p;
        p.playEvents = {vamp(1.0, 2.0, 3)};
        const auto id = r.fire(ramp(), p);
        play(r, 2.5);
        QCOMPARE(state(r).playState, ActiveVoice::PlayState::Repeating);
        r.advance(id);                                    // repeats ignore Advance
        play(r, 1.9);                                     // 4.4 s: 1 + 3 passes + 0.4
        QVERIFY2(std::abs(pos(r) - 2.4) < 0.02, qPrintable(QString::number(pos(r))));
        QCOMPARE(state(r).playState, ActiveVoice::PlayState::Normal);
    }

    void aHoldWaitsSilentlyForAdvance()
    {
        OfflineRenderer r(kSr, 2);
        VoiceParams p;
        p.playEvents = {ev(PlayEvent::Kind::Hold, 1.0)};
        const auto id = r.fire(ramp(), p);
        play(r, 1.5);
        const auto held = play(r, 1.0);
        QCOMPARE(state(r).playState, ActiveVoice::PlayState::Holding);
        QVERIFY2(std::abs(pos(r) - 1.0) < 0.01, qPrintable(QString::number(pos(r))));
        float peak = 0.f;
        for (float s : held) peak = std::max(peak, std::abs(s));
        QVERIFY2(peak < 1e-6f, "silent while held");
        r.advance(id);
        play(r, 0.5);
        QVERIFY2(std::abs(pos(r) - 1.5) < 0.02, qPrintable(QString::number(pos(r))));
        QCOMPARE(state(r).playState, ActiveVoice::PlayState::Normal);
    }

    void aSkipPlaysThroughUnlessAdvanced()
    {
        {   // Left alone: plays through.
            OfflineRenderer r(kSr, 2);
            VoiceParams p;
            p.playEvents = {ev(PlayEvent::Kind::Skip, 1.0, 3.0)};
            r.fire(ramp(), p);
            play(r, 2.0);
            QCOMPARE(state(r).playState, ActiveVoice::PlayState::InSkip);
            QVERIFY(std::abs(pos(r) - 2.0) < 0.02);
        }
        {   // Advance inside it: straight to its end.
            OfflineRenderer r(kSr, 2);
            VoiceParams p;
            p.playEvents = {ev(PlayEvent::Kind::Skip, 1.0, 3.0)};
            const auto id = r.fire(ramp(), p);
            play(r, 1.2);
            r.advance(id);
            play(r, 0.3);
            QVERIFY2(std::abs(pos(r) - 3.29) < 0.03, qPrintable(QString::number(pos(r))));
        }
    }

    void exitNowAndAtTheNextBar()
    {
        {   // Now: from where it is to the vamp's end.
            OfflineRenderer r(kSr, 2);
            VoiceParams p;
            p.playEvents = {vamp(1.0, 3.0, 0, PlayEvent::Exit::Now)};
            const auto id = r.fire(ramp(), p);
            play(r, 1.5);
            r.advance(id);
            play(r, 0.2);
            QVERIFY2(std::abs(pos(r) - 3.2) < 0.03, qPrintable(QString::number(pos(r))));
        }
        {   // Next bar (bars every 0.5 s): at 1.6 it waits for 2.0, then 3.0.
            OfflineRenderer r(kSr, 2);
            VoiceParams p;
            p.playEvents = {vamp(1.0, 3.0, 0, PlayEvent::Exit::NextBar)};
            for (double t = 0.0; t < kLen; t += 0.5) p.barTimes.push_back(t);
            const auto id = r.fire(ramp(), p);
            play(r, 1.6);
            r.advance(id);
            play(r, 0.3);                                 // 1.9: still before the bar
            QVERIFY2(std::abs(pos(r) - 1.9) < 0.02, qPrintable(QString::number(pos(r))));
            play(r, 0.3);                                 // 2.0 -> 3.0, then 0.2 on
            QVERIFY2(std::abs(pos(r) - 3.2) < 0.03, qPrintable(QString::number(pos(r))));
        }
    }

    void advanceWithNothingPendingDoesNothing()
    {
        OfflineRenderer r(kSr, 2);
        VoiceParams p;
        p.playEvents = {vamp(3.0, 4.0)};
        const auto id = r.fire(ramp(), p);
        play(r, 0.5);
        r.advance(id);                                    // nothing to advance yet
        play(r, 0.5);
        QVERIFY(std::abs(pos(r) - 1.0) < 0.02);
        play(r, 3.5);                                     // the early Advance didn't pre-empt the vamp
        QCOMPARE(state(r).playState, ActiveVoice::PlayState::Vamping);
    }

    void savedAndTidied()
    {
        PlayEvents in = {vamp(2.0, 3.0, 4, PlayEvent::Exit::NextBar), vamp(1.0, 2.5),
                         ev(PlayEvent::Kind::Hold, 0.5), ev(PlayEvent::Kind::Skip, 4.0, 4.01),
                         ev(PlayEvent::Kind::Marker, 5.0)};
        in[4].name = QStringLiteral("Bar 32");
        const auto norm = normalizePlayEvents(in);
        // Sorted; the overlapping vamp (starting at 2.0, inside 1.0-2.5) and
        // the 10 ms skip are gone.
        QCOMPARE(norm.size(), size_t(3));
        QCOMPARE(norm[0].kind, PlayEvent::Kind::Hold);
        QCOMPARE(norm[1].start, 1.0);
        QCOMPARE(norm[2].name, QStringLiteral("Bar 32"));
        const auto json = QJsonDocument(playEventsToJson(norm)).toJson();
        bool ok = false;
        const auto back = playEventsFromVariant(QString::fromUtf8(json), &ok);
        QVERIFY(ok);
        QVERIFY(back == norm);
        const auto rep = playEventsFromVariant(QString::fromUtf8(QJsonDocument(playEventsToJson({vamp(1, 2, 4, PlayEvent::Exit::NextBar)})).toJson()), &ok);
        QCOMPARE(rep[0].count, 4);
        QCOMPARE(rep[0].exit, PlayEvent::Exit::NextBar);
        QCOMPARE(rep[0].kindName(), QStringLiteral("Repeat ×4"));
        // Musicians' words load too.
        playEventsFromVariant(QStringLiteral(R"([{"kind":"caesura","start":2}])"), &ok);
        QVERIFY(ok);
        playEventsFromVariant(QStringLiteral("not json"), &ok);
        QVERIFY(!ok);
    }
};

QTEST_MAIN(PlayEventsTests)
#include "test_play_events.moc"
