#include <QTest>

#include "mix/X32Value.h"

#include <cmath>

namespace x32 = quewi::mix::x32;

// The X32 rounds out-of-grid values rather than rejecting them, so a wrong
// mapping fails silently on real hardware. These tests are the only thing
// standing between us and a plausible-but-wrong filter at a tech.
class X32ValueTests : public QObject {
    Q_OBJECT

    static bool close(float a, float b, float eps) { return std::fabs(a - b) <= eps; }

private slots:
    // ── DCA membership ───────────────────────────────────────────────

    void dcaBitLayout()
    {
        // DCA1 = bit 0 = value 1. The protocol doc never states this; it was
        // recovered from production scene files. If this test ever "fails"
        // because someone flipped it, read docs/dev/console-protocols.md
        // before touching it.
        QCOMPARE(x32::dcaBit(1), quint8(1));
        QCOMPARE(x32::dcaBit(2), quint8(2));
        QCOMPARE(x32::dcaBit(3), quint8(4));
        QCOMPARE(x32::dcaBit(8), quint8(128));

        // Out of range yields 0 rather than shifting into UB.
        QCOMPARE(x32::dcaBit(0), quint8(0));
        QCOMPARE(x32::dcaBit(9), quint8(0));
        QCOMPARE(x32::dcaBit(-1), quint8(0));
    }

    void maskEditing()
    {
        x32::DcaMask m = 0;
        m = x32::dcaMaskWith(m, 1);
        m = x32::dcaMaskWith(m, 3);
        QCOMPARE(m, quint8(0b00000101));
        QVERIFY(x32::dcaMaskContains(m, 1));
        QVERIFY(x32::dcaMaskContains(m, 3));
        QVERIFY(!x32::dcaMaskContains(m, 2));

        m = x32::dcaMaskWithout(m, 1);
        QCOMPARE(m, quint8(0b00000100));

        // Idempotent both ways.
        QCOMPARE(x32::dcaMaskWith(m, 3), m);
        QCOMPARE(x32::dcaMaskWithout(m, 7), m);
    }

    void maskListRoundTrip()
    {
        QCOMPARE(x32::dcaMaskToList(0b00000101), (QVector<int>{1, 3}));
        QCOMPARE(x32::dcaMaskToList(0), QVector<int>{});
        QCOMPARE(x32::dcaMaskToList(255), (QVector<int>{1, 2, 3, 4, 5, 6, 7, 8}));

        QCOMPARE(x32::dcaListToMask({1, 3}), quint8(0b00000101));
        QCOMPARE(x32::dcaListToMask({}), quint8(0));

        // A bad list can't corrupt the mask.
        QCOMPARE(x32::dcaListToMask({0, 9, 2}), quint8(2));
    }

    // Regression test built from real production data (Games Done Quick's
    // published X32 scene files). That show declares /dca/2/config "Run" and
    // /dca/3/config "Interview"; its channels named "Runner 1..4" carry
    // %00000010 and "InterviewLav" carries %00000100.
    //
    // Reading MSB-first makes those names line up with their DCAs. Reading
    // LSB-first would put the runners on DCA7 and the interview on DCA6 —
    // nonsense. This test pins the bit order to observed reality.
    void parsesProductionSceneData()
    {
        const auto runner = x32::parseMaskText(u"%00000010");
        QVERIFY(runner.has_value());
        QCOMPARE(x32::dcaMaskToList(*runner), QVector<int>{2});   // "Run", not DCA7

        const auto interview = x32::parseMaskText(u"%00000100");
        QVERIFY(interview.has_value());
        QCOMPARE(x32::dcaMaskToList(*interview), QVector<int>{3}); // "Interview", not DCA6

        const auto music = x32::parseMaskText(u"%00000001");
        QVERIFY(music.has_value());
        QCOMPARE(x32::dcaMaskToList(*music), QVector<int>{1});     // "Break Music"

        QCOMPARE(*x32::parseMaskText(u"%10000000"), quint8(128));  // DCA8
    }

    void parsesEmulatorStrippedForm()
    {
        // The emulator strips leading zeros; the real console pads to 8.
        // Both must parse to the same thing.
        QCOMPARE(*x32::parseMaskText(u"%10"), quint8(2));
        QCOMPARE(*x32::parseMaskText(u"%00000010"), quint8(2));
    }

    void rejectsBadMaskText()
    {
        QVERIFY(!x32::parseMaskText(u"00000010").has_value());   // no %
        QVERIFY(!x32::parseMaskText(u"%").has_value());          // empty
        QVERIFY(!x32::parseMaskText(u"%00000002").has_value());  // not binary
        QVERIFY(!x32::parseMaskText(u"%000000001").has_value()); // >8 bits
        QVERIFY(!x32::parseMaskText(u"").has_value());
    }

    void maskTextRoundTrip()
    {
        QCOMPARE(x32::maskText(2), QStringLiteral("%00000010"));
        QCOMPARE(x32::maskText(1), QStringLiteral("%00000001"));
        QCOMPARE(x32::maskText(128), QStringLiteral("%10000000"));
        QCOMPARE(x32::maskText(0), QStringLiteral("%00000000"));

        for (int m = 0; m <= 255; ++m)
            QCOMPARE(*x32::parseMaskText(x32::maskText(quint8(m))), quint8(m));
    }

    // ── Addresses ────────────────────────────────────────────────────

    void addressPaddingWidthsDiffer()
    {
        // Three different widths in one protocol. This is the documented
        // trap these builders exist to eliminate.
        QCOMPARE(x32::chAddr(1, QLatin1String("grp/dca")), QStringLiteral("/ch/01/grp/dca"));
        QCOMPARE(x32::chAddr(32, QLatin1String("mix/on")), QStringLiteral("/ch/32/mix/on"));

        // DCA is NOT zero-padded.
        QCOMPARE(x32::dcaAddr(1, QLatin1String("fader")), QStringLiteral("/dca/1/fader"));
        QCOMPARE(x32::dcaAddr(8, QLatin1String("config/name")), QStringLiteral("/dca/8/config/name"));

        // Headamp is three digits.
        QCOMPARE(x32::headampAddr(0, QLatin1String("gain")), QStringLiteral("/headamp/000/gain"));
        QCOMPARE(x32::headampAddr(127, QLatin1String("gain")), QStringLiteral("/headamp/127/gain"));
    }

    void bareStripAddresses()
    {
        QCOMPARE(x32::chAddr(7), QStringLiteral("/ch/07"));
        QCOMPARE(x32::dcaAddr(3), QStringLiteral("/dca/3"));
    }

    // ── Mute ─────────────────────────────────────────────────────────

    void mutePolarityIsInverted()
    {
        // mix/on is ON-not-mute: 1 = unmuted. Getting this backwards mutes
        // the cast mid-show.
        QCOMPARE(x32::onValueForMuted(true), 0);
        QCOMPARE(x32::onValueForMuted(false), 1);
        QVERIFY(x32::mutedFromOnValue(0));
        QVERIFY(!x32::mutedFromOnValue(1));
    }

    // ── level curve ──────────────────────────────────────────────────

    void levelAnchors()
    {
        QVERIFY(close(x32::levelToDb(0.75f), 0.0f, 0.001f));   // 0 dB <-> 0.75
        QVERIFY(close(x32::levelToDb(1.0f), 10.0f, 0.001f));   // +10 dB ceiling
        QVERIFY(close(x32::dbToLevel(0.0f), 0.75f, 0.001f));
        QVERIFY(close(x32::dbToLevel(10.0f), 1.0f, 0.001f));
    }

    void levelZeroIsNegativeInfinityNotMinus90()
    {
        // f == 0.0 is a hard off, not -90 dB. The curve's own formula would
        // give -90 at f=0; the console says -inf. Honour the console.
        QVERIFY(std::isinf(x32::levelToDb(0.0f)));
        QVERIFY(x32::levelToDb(0.0f) < 0.0f);

        // Deliberately not perfectly invertible at the bottom: both -inf and
        // -90 map to a hard off. That asymmetry is the console's.
        QCOMPARE(x32::dbToLevel(-std::numeric_limits<float>::infinity()), 0.0f);
        QCOMPARE(x32::dbToLevel(-90.0f), 0.0f);
        QCOMPARE(x32::dbToLevel(-200.0f), 0.0f);
    }

    void levelSegmentBoundaries()
    {
        // The four segments meet at these points; verified against the
        // protocol doc's published curve.
        QVERIFY(close(x32::levelToDb(0.5f), -10.0f, 0.001f));
        QVERIFY(close(x32::levelToDb(0.25f), -30.0f, 0.001f));
        QVERIFY(close(x32::levelToDb(0.0625f), -60.0f, 0.001f));

        QVERIFY(close(x32::dbToLevel(-10.0f), 0.5f, 0.001f));
        QVERIFY(close(x32::dbToLevel(-30.0f), 0.25f, 0.001f));
        QVERIFY(close(x32::dbToLevel(-60.0f), 0.0625f, 0.001f));
    }

    void levelRoundTripsAcrossRange()
    {
        for (float db : {10.0f, 5.0f, 0.0f, -5.0f, -10.0f, -20.0f, -30.0f,
                         -45.0f, -60.0f, -75.0f, -89.0f}) {
            const float f = x32::dbToLevel(db);
            QVERIFY(f >= 0.0f && f <= 1.0f);
            QVERIFY2(close(x32::levelToDb(f), db, 0.01f),
                     qPrintable(QStringLiteral("round trip failed at %1 dB").arg(db)));
        }
    }

    void levelClampsAboveCeiling()
    {
        QCOMPARE(x32::dbToLevel(20.0f), 1.0f);
        QVERIFY(close(x32::levelToDb(1.5f), 10.0f, 0.001f));  // clamped to 1.0
    }

    void faderAndSendUseDifferentGrids()
    {
        // Faders are 1024-step, sends 161-step. Crossing them puts values on
        // the wrong grid and the console silently rounds.
        for (float f : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
            QVERIFY(x32::quantizeFader(f) >= 0.0f && x32::quantizeFader(f) <= 1.0f);
            QVERIFY(x32::quantizeSend(f) >= 0.0f && x32::quantizeSend(f) <= 1.0f);
        }
        QCOMPARE(x32::quantizeSend(0.5f), 0.5f);      // 80/160 lands exactly
        QCOMPARE(x32::quantizeFader(0.0f), 0.0f);
        QCOMPARE(x32::quantizeSend(0.0f), 0.0f);

        // Quantising is stable — re-quantising a gridded value is a no-op.
        const float q = x32::quantizeSend(0.333f);
        QCOMPARE(x32::quantizeSend(q), q);
    }

    // ── linf / logf ──────────────────────────────────────────────────

    void linfWorkedExample()
    {
        // Pan is linf[-100, 100]. The doc's own worked example: f=0.75 -> +50.
        QVERIFY(close(x32::linfToValue(0.75f, -100.0f, 100.0f), 50.0f, 0.001f));
        QVERIFY(close(x32::linfToValue(0.5f, -100.0f, 100.0f), 0.0f, 0.001f));
        QVERIFY(close(x32::valueToLinf(50.0f, -100.0f, 100.0f), 0.75f, 0.001f));
    }

    void logfWorkedExamples()
    {
        // EQ freq is logf[20, 20000, 201]. Doc's tables: f=0.25 -> ~112.5 Hz,
        // f=0.465 -> ~496.6 Hz.
        QVERIFY(close(x32::floatToEqFreq(0.25f), 112.47f, 0.5f));
        QVERIFY(close(x32::floatToEqFreq(0.465f), 496.6f, 1.0f));

        // Endpoints.
        QVERIFY(close(x32::floatToEqFreq(0.0f), 20.0f, 0.01f));
        QVERIFY(close(x32::floatToEqFreq(1.0f), 20000.0f, 1.0f));
    }

    void eqFreqRoundTrips()
    {
        for (float hz : {20.0f, 100.0f, 440.0f, 1000.0f, 5000.0f, 20000.0f}) {
            const float f = x32::eqFreqToFloat(hz);
            QVERIFY(f >= 0.0f && f <= 1.0f);
            // Quantised to 201 steps, so allow the grid's own error.
            QVERIFY2(close(x32::floatToEqFreq(f), hz, hz * 0.03f),
                     qPrintable(QStringLiteral("round trip failed at %1 Hz").arg(hz)));
        }
    }

    void eqGainRoundTrips()
    {
        QVERIFY(close(x32::floatToEqGain(0.5f), 0.0f, 0.001f));
        QVERIFY(close(x32::eqGainToFloat(0.0f), 0.5f, 0.001f));
        QVERIFY(close(x32::floatToEqGain(0.0f), -15.0f, 0.001f));
        QVERIFY(close(x32::floatToEqGain(1.0f), 15.0f, 0.001f));
    }

    // The one most likely to be "fixed" by a well-meaning reader.
    void eqQIsInverted()
    {
        // logf[10.0, 0.3] — min > max. f=0 is the NARROWEST filter.
        QVERIFY(close(x32::floatToEqQ(0.0f), 10.0f, 0.01f));
        QVERIFY(close(x32::floatToEqQ(1.0f), 0.3f, 0.01f));

        // Doc's published Q table.
        QVERIFY(close(x32::floatToEqQ(0.4648f), 2.0f, 0.05f));
        QVERIFY(close(x32::floatToEqQ(0.1972f), 5.0f, 0.05f));

        // Monotonically DECREASING in f — this is the whole point.
        QVERIFY(x32::floatToEqQ(0.0f) > x32::floatToEqQ(0.5f));
        QVERIFY(x32::floatToEqQ(0.5f) > x32::floatToEqQ(1.0f));

        QVERIFY(close(x32::eqQToFloat(2.0f), 0.4648f, 0.02f));
    }
};

QTEST_MAIN(X32ValueTests)
#include "test_x32_value.moc"
