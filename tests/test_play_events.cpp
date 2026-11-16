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

    // Matthew, 2026-11-15: "if I delete a vamp in the editor while the main
    // track is playing it is phantom ... I can't bypass it and have to
    // panic out of the track". Edits now reach the playing voice.
    void aVampDeletedWhilePlayingLetsGo()
    {
        OfflineRenderer r(kSr, 2);
        VoiceParams p;
        const auto marker = ev(PlayEvent::Kind::Marker, 0.5);
        const auto v = vamp(1.0, 2.0);
        const auto later = vamp(4.0, 5.0);
        p.playEvents = {marker, v, later};
        const auto id = r.fire(ramp(), p);
        play(r, 2.5);                                     // 1.5 passes in
        QCOMPARE(state(r).playState, ActiveVoice::PlayState::Vamping);
        QCOMPARE(state(r).playEventIndex, 1);

        QVERIFY(r.setPlayEvents(id, {marker, later}));    // the vamp is deleted
        QCOMPARE(state(r).playState, ActiveVoice::PlayState::Normal);
        const auto out = play(r, 1.0);                    // on through 2.0, no loop back
        QVERIFY2(std::abs(pos(r) - 2.5) < 0.02, qPrintable(QString::number(pos(r))));
        for (size_t i = 1; i < out.size(); ++i)
            QVERIFY2(out[i] >= out[i - 1] - 1e-6f, "plays straight on once the vamp is gone");
        // The vamp further on still happens, under its new index.
        play(r, 2.0);
        QCOMPARE(state(r).playState, ActiveVoice::PlayState::Vamping);
        QCOMPARE(state(r).playEventIndex, 1);
        QVERIFY(r.advance(id));
        play(r, 1.2);
        QCOMPARE(state(r).playState, ActiveVoice::PlayState::Normal);
    }

    void editsWhilePlayingKeepWhatTheVoiceIsIn()
    {
        OfflineRenderer r(kSr, 2);
        VoiceParams p;
        const auto v = vamp(1.0, 2.0);
        p.playEvents = {v};
        const auto id = r.fire(ramp(), p);
        play(r, 3.5);
        const int pass = state(r).playPass;
        QVERIFY(pass >= 2);
        // An event added before it shifts its index; the vamp carries on.
        QVERIFY(r.setPlayEvents(id, {ev(PlayEvent::Kind::Marker, 0.2), v, ev(PlayEvent::Kind::Hold, 3.0)}));
        QCOMPARE(state(r).playState, ActiveVoice::PlayState::Vamping);
        QCOMPARE(state(r).playEventIndex, 1);
        QCOMPARE(state(r).playPass, pass);
        play(r, 1.0);
        QCOMPARE(state(r).playState, ActiveVoice::PlayState::Vamping);
        QVERIFY(r.advance(id));
        play(r, 1.0);                                     // out at 2.0, on to the new hold at 3.0
        play(r, 1.0);
        QCOMPARE(state(r).playState, ActiveVoice::PlayState::Holding);
        QCOMPARE(state(r).playEventIndex, 2);
        // A hold deleted while the song is stopped at it lets go too.
        QVERIFY(r.setPlayEvents(id, {v}));
        QCOMPARE(state(r).playState, ActiveVoice::PlayState::Normal);
        play(r, 0.5);
        QVERIFY2(std::abs(pos(r) - 3.5) < 0.02, qPrintable(QString::number(pos(r))));
        // Everything deleted: plain playback, nothing reported.
        QVERIFY(r.setPlayEvents(id, {}));
        play(r, 0.2);
        QCOMPARE(state(r).playState, ActiveVoice::PlayState::Normal);
        QCOMPARE(state(r).playEventIndex, -1);
    }

    // ── The 2026-11-15 audit (docs/dev/session-handoff.md 0a10) ──────────

    // A hold at a vamp's end used to stop the first pass (and spin the pass
    // count while parked); it now waits until the vamp is left.
    void aHoldAtAVampsEndWaitsForTheVampToBeLeft()
    {
        OfflineRenderer r(kSr, 2);
        VoiceParams p;
        p.playEvents = {vamp(1.0, 2.0), ev(PlayEvent::Kind::Hold, 2.0)};
        const auto id = r.fire(ramp(), p);
        const auto out = play(r, 3.5);
        QCOMPARE(state(r).playState, ActiveVoice::PlayState::Vamping);
        QCOMPARE(state(r).playPass, 2);
        float worst = 0.f;                                // and no dip towards the hold at each loop
        for (size_t i = 1; i < out.size(); ++i) worst = std::max(worst, std::abs(out[i] - out[i - 1]));
        QVERIFY2(worst < 0.005f, qPrintable(QString::number(worst)));
        r.advance(id);
        play(r, 1.0);
        QCOMPARE(state(r).playState, ActiveVoice::PlayState::Holding);
        QVERIFY2(std::abs(pos(r) - 2.0) < 0.01, qPrintable(QString::number(pos(r))));
        r.advance(id);
        play(r, 0.5);
        QCOMPARE(state(r).playState, ActiveVoice::PlayState::Normal);
        QVERIFY2(std::abs(pos(r) - 2.5) < 0.02, qPrintable(QString::number(pos(r))));

        // A repeat ×3 then the hold: three passes, then it stops.
        OfflineRenderer r2(kSr, 2);
        p.playEvents = {vamp(1.0, 2.0, 3), ev(PlayEvent::Kind::Hold, 2.0)};
        r2.fire(ramp(), p);
        play(r2, 3.5);
        QCOMPARE(state(r2).playState, ActiveVoice::PlayState::Repeating);
        play(r2, 1.0);
        QCOMPARE(state(r2).playState, ActiveVoice::PlayState::Holding);
    }

    // A hold inside a vamp stops the first pass once, and the vamp goes on
    // looping after it (the pass count stays honest).
    void aHoldInsideAVampStopsOnce()
    {
        OfflineRenderer r(kSr, 2);
        VoiceParams p;
        p.playEvents = {vamp(1.0, 2.0), ev(PlayEvent::Kind::Hold, 1.5)};
        const auto id = r.fire(ramp(), p);
        play(r, 3.0);
        QCOMPARE(state(r).playState, ActiveVoice::PlayState::Holding);
        r.advance(id);
        play(r, 1.75);                                    // 1.5 → 2.0, loop, → 2.0, loop, 1.25
        QCOMPARE(state(r).playState, ActiveVoice::PlayState::Vamping);
        QCOMPARE(state(r).playPass, 2);
        QVERIFY2(std::abs(pos(r) - 1.25) < 0.02, qPrintable(QString::number(pos(r))));
    }

    // Advance in a skip jumps to its end — past a hold inside it, not into it.
    void aHoldThatIsJumpedOverDoesNotStop()
    {
        OfflineRenderer r(kSr, 2);
        VoiceParams p;
        p.playEvents = {ev(PlayEvent::Kind::Skip, 1.0, 3.0), ev(PlayEvent::Kind::Hold, 2.0)};
        const auto id = r.fire(ramp(), p);
        play(r, 1.2);
        r.advance(id);
        play(r, 0.5);
        QCOMPARE(state(r).playState, ActiveVoice::PlayState::Normal);
        QVERIFY2(pos(r) > 3.3 && pos(r) < 3.6, qPrintable(QString::number(pos(r))));
    }

    // No click at a seam: a vamp at the top of the song has no pre-roll (it
    // dips instead), and Advance landing inside the pre-roll fades the blend
    // back out instead of dropping it.
    void seamsNeverStep()
    {
        {
            OfflineRenderer r(kSr, 2);
            VoiceParams p;
            p.playEvents = {vamp(0.0, 1.0)};
            r.fire(ramp(), p);
            const auto out = play(r, 2.5);
            QCOMPARE(state(r).playState, ActiveVoice::PlayState::Vamping);
            float worst = 0.f;
            for (size_t i = 1; i < out.size(); ++i) worst = std::max(worst, std::abs(out[i] - out[i - 1]));
            QVERIFY2(worst < 0.005f, qPrintable(QStringLiteral("vamp from 0: %1").arg(worst)));
        }
        {
            OfflineRenderer r(kSr, 2);
            VoiceParams p;
            p.playEvents = {vamp(1.0, 2.0)};
            const auto id = r.fire(ramp(), p);
            auto out = play(r, 2.99);                     // 10 ms from the end of pass 2: in the pre-roll
            QVERIFY2(std::abs(pos(r) - 1.99) < 0.001, qPrintable(QString::number(pos(r), 'f', 4)));
            r.advance(id);
            const auto more = play(r, 0.3);
            out.insert(out.end(), more.begin(), more.end());
            QCOMPARE(state(r).playState, ActiveVoice::PlayState::Normal);
            QVERIFY2(std::abs(pos(r) - 2.29) < 0.02, qPrintable(QString::number(pos(r))));
            float worst = 0.f;
            for (size_t i = 1; i < out.size(); ++i) worst = std::max(worst, std::abs(out[i] - out[i - 1]));
            QVERIFY2(worst < 0.005f, qPrintable(QStringLiteral("advance in the pre-roll: %1").arg(worst)));
        }
        {
            // Right on the loop point: too late for this pass, so it goes
            // round once more and leaves at the end of that one.
            OfflineRenderer r(kSr, 2);
            VoiceParams p;
            p.playEvents = {vamp(1.0, 2.0)};
            const auto id = r.fire(ramp(), p);
            auto out = play(r, 3.0);
            r.advance(id);
            auto more = play(r, 0.5);
            out.insert(out.end(), more.begin(), more.end());
            QCOMPARE(state(r).playState, ActiveVoice::PlayState::Leaving);
            more = play(r, 1.0);
            out.insert(out.end(), more.begin(), more.end());
            QCOMPARE(state(r).playState, ActiveVoice::PlayState::Normal);
            QVERIFY2(std::abs(pos(r) - 2.5) < 0.02, qPrintable(QString::number(pos(r))));
            float worst = 0.f;
            for (size_t i = 1; i < out.size(); ++i) worst = std::max(worst, std::abs(out[i] - out[i - 1]));
            QVERIFY2(worst < 0.005f, qPrintable(QStringLiteral("advance on the loop point: %1").arg(worst)));
        }
    }

    // The cue's fade-out is for the end of the song, not for a vamp that
    // happens to sit in its last seconds.
    void theFadeOutWaitsForAVampNearTheEnd()
    {
        OfflineRenderer r(kSr, 2);
        VoiceParams p;
        p.fadeOutSeconds = 2.0;
        p.playEvents = {vamp(4.5, 5.5)};
        const auto id = r.fire(ramp(), p);
        const auto out = play(r, 5.0);
        QCOMPARE(state(r).playState, ActiveVoice::PlayState::Vamping);
        QVERIFY2(std::abs(out.back() - 5.0f / 6.0f) < 0.02f, qPrintable(QString::number(out.back())));
        r.advance(id);                                    // left: now the fade-out applies
        const auto tail = play(r, 0.9);                   // at 5.9 of 6.0: nearly out
        QVERIFY2(tail.back() < 0.1f, qPrintable(QString::number(tail.back())));
    }

    void aVampPastTheTrimOutLoopsAtTheEnd()
    {
        OfflineRenderer r(kSr, 2);
        VoiceParams p;
        p.trimOutSeconds = 3.0;
        p.playEvents = {vamp(2.0, 4.0)};
        r.fire(ramp(), p);
        play(r, 5.2);
        QCOMPARE(r.activeCount(), 1);
        QCOMPARE(state(r).playState, ActiveVoice::PlayState::Vamping);
        QVERIFY2(pos(r) >= 2.0 && pos(r) < 3.0, qPrintable(QString::number(pos(r))));
    }

    // A cut across a vamp's end: the loop is the vamp minus the cut part,
    // every pass (it used to come back short by the cut's length).
    void aCutAcrossAVampsEndKeepsTheLoopLength()
    {
        OfflineRenderer r(kSr, 2);
        VoiceParams p;
        p.cuts = {{2.8, 3.2}};
        p.playEvents = {vamp(1.0, 3.0)};
        r.fire(ramp(), p);
        play(r, 1.0 + 1.8 + 1.8 + 0.5);
        QCOMPARE(state(r).playState, ActiveVoice::PlayState::Vamping);
        QCOMPARE(state(r).playPass, 2);
        QVERIFY2(std::abs(pos(r) - 1.5) < 0.03, qPrintable(QString::number(pos(r))));
    }

    // Rehearsal: seek away from a hold (it used to be undone), and a hold
    // you go back before stops again.
    void seekingLetsGoOfAHoldAndRearmsTheOnesAhead()
    {
        OfflineRenderer r(kSr, 2);
        VoiceParams p;
        p.playEvents = {ev(PlayEvent::Kind::Hold, 1.0)};
        const auto id = r.fire(ramp(), p);
        play(r, 1.5);
        QCOMPARE(state(r).playState, ActiveVoice::PlayState::Holding);
        QVERIFY(r.seek(id, 3.0));
        play(r, 0.2);
        QCOMPARE(state(r).playState, ActiveVoice::PlayState::Normal);
        QVERIFY2(std::abs(pos(r) - 3.2) < 0.02, qPrintable(QString::number(pos(r))));
        QVERIFY(r.seek(id, 0.5));
        play(r, 1.0);
        QCOMPARE(state(r).playState, ActiveVoice::PlayState::Holding);
        QVERIFY2(std::abs(pos(r) - 1.0) < 0.01, qPrintable(QString::number(pos(r))));
    }

    void aLoopingCueStopsAtItsHoldEveryTimeRound()
    {
        OfflineRenderer r(kSr, 2);
        VoiceParams p;
        p.loop = true;
        p.playEvents = {ev(PlayEvent::Kind::Hold, 1.0)};
        const auto id = r.fire(ramp(2.0), p);
        play(r, 1.2);
        QCOMPARE(state(r).playState, ActiveVoice::PlayState::Holding);
        r.advance(id);
        play(r, 2.5);                                     // on to the end, round, and to 1.0 again
        QCOMPARE(state(r).playState, ActiveVoice::PlayState::Holding);
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
