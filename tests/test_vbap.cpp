// VBAP unit tests. The renderer is hand-written so it gets a real
// test suite — easy to regress and the failure mode (sources panning
// to the wrong speaker) is silent unless someone listens.
#include "audio/Vbap.h"

#include <QtTest/QtTest>
#include <cmath>

using quewi::audio::Speaker;
using quewi::audio::Vbap;

namespace {
// Helper: assert constant-power (sum of squared gains == 1) within eps.
bool isConstantPower(const QList<float> &g, float eps = 1e-3f) {
    float s = 0.0f;
    for (float x : g) s += x * x;
    return std::abs(s - 1.0f) < eps;
}
// Helper: count non-zero gains.
int nonZero(const QList<float> &g, float eps = 1e-5f) {
    int n = 0;
    for (float x : g) if (std::abs(x) > eps) ++n;
    return n;
}
} // namespace

class TestVbap : public QObject {
    Q_OBJECT
private slots:

    void stereoCenter() {
        // Stereo pair at ±30°. Source dead-centre should give equal L/R.
        Vbap v({ Speaker{0, -30, 0, 1}, Speaker{1, 30, 0, 1} });
        auto g = v.gains(0, 0, 0, 2);
        QVERIFY(isConstantPower(g));
        QVERIFY(std::abs(g[0] - g[1]) < 1e-3f);
    }

    void stereoHardLeft() {
        // Source pinned to the left speaker → all energy on channel 0.
        Vbap v({ Speaker{0, -30, 0, 1}, Speaker{1, 30, 0, 1} });
        auto g = v.gains(-30, 0, 0, 2);
        QVERIFY(isConstantPower(g));
        QVERIFY(g[0] > 0.99f);
        QVERIFY(g[1] < 0.05f);
    }

    void surround5_1() {
        // Standard 5.1 (L, R, C, LFE, LS, RS). LFE skipped — VBAP
        // doesn't pan to it. Source at 90° (right side) should activate
        // R and RS roughly equally.
        const QList<Speaker> spk = {
            Speaker{0, -30,   0, 1},  // L
            Speaker{1,  30,   0, 1},  // R
            Speaker{2,   0,   0, 1},  // C
            // ch 3 = LFE (omitted)
            Speaker{4, -110,  0, 1},  // LS
            Speaker{5,  110,  0, 1},  // RS
        };
        Vbap v(spk);
        auto g = v.gains(90, 0, 0, 6);
        QVERIFY(isConstantPower(g));
        QCOMPARE(nonZero(g), 2);
        QVERIFY(g[1] > 0);   // R
        QVERIFY(g[5] > 0);   // RS
    }

    void atmos7_1_4HasHeight() {
        // A layout with elevated speakers should engage the 3D
        // triangulation path. Drop a source overhead → height speakers
        // dominate.
        const QList<Speaker> spk = {
            Speaker{0, -30,   0, 1}, Speaker{1, 30,   0, 1},
            Speaker{2,   0,   0, 1}, Speaker{4, -90,  0, 1},
            Speaker{5,  90,   0, 1}, Speaker{6, -150, 0, 1},
            Speaker{7, 150,   0, 1},
            Speaker{8, -45,  45, 1}, Speaker{9, 45,   45, 1},
            Speaker{10,-135, 45, 1}, Speaker{11,135,  45, 1},
        };
        Vbap v(spk);
        QVERIFY(v.hasHeight());
        auto g = v.gains(0, 80, 0, 12); // nearly directly overhead, front
        QVERIFY(isConstantPower(g, 5e-3f));
        // Height speakers (8..11) should carry the bulk of the energy.
        float earSq = 0, hghtSq = 0;
        for (int c = 0; c <= 7; ++c) earSq += g[c] * g[c];
        for (int c = 8; c <= 11; ++c) hghtSq += g[c] * g[c];
        QVERIFY(hghtSq > earSq);
    }

    void spreadOmni() {
        // spread=1 should distribute energy across every speaker.
        Vbap v({ Speaker{0, -30, 0, 1}, Speaker{1, 30, 0, 1},
                 Speaker{2, -110, 0, 1}, Speaker{3, 110, 0, 1} });
        auto g = v.gains(0, 0, 1.0f, 4);
        QVERIFY(isConstantPower(g));
        QCOMPARE(nonZero(g), 4);
        // Equal-power distribution: each channel gets sqrt(1/N).
        const float expected = std::sqrt(0.25f);
        for (int c = 0; c < 4; ++c)
            QVERIFY(std::abs(g[c] - expected) < 1e-3f);
    }

    void emptyLayout() {
        // Empty speaker list: gains() returns all-zero, no crash.
        Vbap v({});
        auto g = v.gains(0, 0, 0, 8);
        QCOMPARE(g.size(), 8);
        for (float x : g) QVERIFY(x == 0.0f);
    }
};

QTEST_APPLESS_MAIN(TestVbap)
#include "test_vbap.moc"
