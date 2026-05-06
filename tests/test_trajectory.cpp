// AudioTrajectory tests — keyframe sampling, interpolation, mode wrap,
// JSON round-trip. The trajectory is the v0.5 marquee feature; a quiet
// regression here would manifest as objects parking in the wrong place
// during a show, which is the exact failure object-audio is supposed to
// avoid. Worth the test surface.
#include "audio/AudioTrajectory.h"

#include <QtTest/QtTest>
#include <cmath>

using quewi::audio::AudioTrajectory;

class TestTrajectory : public QObject {
    Q_OBJECT
private slots:

    void emptyReturnsZeros() {
        AudioTrajectory t;
        const auto s = t.sampleAt(1.0);
        QCOMPARE(s.azimuthDeg, 0.0);
        QCOMPARE(s.elevationDeg, 0.0);
        QCOMPARE(s.spread, 0.0);
        QVERIFY(t.isEmpty());
    }

    void singleKeyframeHolds() {
        AudioTrajectory t;
        t.addKeyframe({0.0, 45.0, 10.0, 0.5});
        const auto s = t.sampleAt(99.0);
        QCOMPARE(s.azimuthDeg, 45.0);
        QCOMPARE(s.elevationDeg, 10.0);
        QCOMPARE(s.spread, 0.5);
    }

    void linearInterp() {
        AudioTrajectory t;
        t.addKeyframe({0.0,  0.0,  0.0, 0.0});
        t.addKeyframe({2.0, 90.0, 30.0, 1.0});
        const auto s = t.sampleAt(1.0);
        QVERIFY(std::abs(s.azimuthDeg   - 45.0) < 1e-6);
        QVERIFY(std::abs(s.elevationDeg - 15.0) < 1e-6);
        QVERIFY(std::abs(s.spread       -  0.5) < 1e-6);
    }

    void azimuthShortestArc() {
        // Going from 170° to -170° (across the rear) should cross 180°
        // at the midpoint, NOT 0° (which would be the long way round).
        AudioTrajectory t;
        t.addKeyframe({0.0,  170.0, 0.0, 0.0});
        t.addKeyframe({2.0, -170.0, 0.0, 0.0});
        const auto mid = t.sampleAt(1.0);
        // Either 180 or -180 is correct; both represent the rear.
        QVERIFY(std::abs(mid.azimuthDeg) > 175.0);
    }

    void oneShotClampsAtBoundaries() {
        AudioTrajectory t;
        t.addKeyframe({0.0,  0.0, 0.0, 0.0});
        t.addKeyframe({2.0, 90.0, 0.0, 0.0});
        QCOMPARE(t.sampleAt(-5.0).azimuthDeg, 0.0);
        QCOMPARE(t.sampleAt(99.0).azimuthDeg, 90.0);
    }

    void loopWraps() {
        AudioTrajectory t;
        t.setMode(AudioTrajectory::Mode::Loop);
        t.addKeyframe({0.0,  0.0, 0.0, 0.0});
        t.addKeyframe({2.0, 60.0, 0.0, 0.0});
        // t=2.5 is 0.5 into the next cycle → 25% between kf0 and kf1.
        const auto s = t.sampleAt(2.5);
        QVERIFY(std::abs(s.azimuthDeg - 15.0) < 1e-6);
    }

    void jsonRoundTrip() {
        AudioTrajectory t;
        t.setMode(AudioTrajectory::Mode::Loop);
        t.addKeyframe({0.0,  10.0, -5.0, 0.2});
        t.addKeyframe({3.0, -90.0,  5.0, 0.7});
        const auto json = t.toJson();
        const auto t2 = AudioTrajectory::fromJson(json);
        QCOMPARE(t2.mode(), AudioTrajectory::Mode::Loop);
        QCOMPARE(t2.keyframeCount(), 2);
        QCOMPARE(t2.keyframes().last().azimuthDeg, -90.0);
        QCOMPARE(t2.keyframes().last().spread, 0.7);
    }

    void addKeyframeKeepsSorted() {
        AudioTrajectory t;
        t.addKeyframe({2.0,  0.0, 0.0, 0.0});
        t.addKeyframe({1.0, 30.0, 0.0, 0.0});
        t.addKeyframe({0.0, 60.0, 0.0, 0.0});
        const auto &kfs = t.keyframes();
        QCOMPARE(kfs.size(), 3);
        QCOMPARE(kfs[0].timeSeconds, 0.0);
        QCOMPARE(kfs[1].timeSeconds, 1.0);
        QCOMPARE(kfs[2].timeSeconds, 2.0);
    }
};

QTEST_GUILESS_MAIN(TestTrajectory)
#include "test_trajectory.moc"
