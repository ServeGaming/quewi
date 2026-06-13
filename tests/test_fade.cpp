#include <QTest>

#include "audio/Db.h"

#include <cmath>

using namespace quewi::audio;

// Unit tests for the dB ↔ linear conversions that drive the entire
// fade engine. These had ZERO direct coverage before — a silent
// regression in the constant would park every level at the wrong
// gain mid-show.
class FadeTests : public QObject {
    Q_OBJECT
private slots:
    void dbToLinearKnownPoints()
    {
        // 0 dB is unity.
        QVERIFY(qFuzzyCompare(dbToLinear(0.0) + 1.0, 1.0 + 1.0));
        // −6 dB ≈ 0.5012 (amplitude, not power).
        QVERIFY(std::abs(dbToLinear(-6.0) - 0.50119) < 1e-4);
        // +6 dB ≈ 1.9953.
        QVERIFY(std::abs(dbToLinear(6.0) - 1.99526) < 1e-4);
        // −20 dB = exactly 0.1.
        QVERIFY(std::abs(dbToLinear(-20.0) - 0.1) < 1e-9);
        // −infinity-ish: a very low dB is ~0.
        QVERIFY(dbToLinear(-120.0) < 1e-5);
    }

    void linearToDbKnownPoints()
    {
        QVERIFY(std::abs(linearToDb(1.0) - 0.0) < 1e-9);
        QVERIFY(std::abs(linearToDb(0.1) - (-20.0)) < 1e-6);
        QVERIFY(std::abs(linearToDb(0.5) - (-6.0206)) < 1e-3);
        // Digital silence floors at −90, never −inf.
        QCOMPARE(linearToDb(0.0), -90.0);
        QCOMPARE(linearToDb(1e-12), -90.0);
    }

    void roundTrip()
    {
        // db → linear → db is identity across the useful range.
        for (double db : {-60.0, -24.0, -12.0, -6.0, -3.0, 0.0, 3.0, 6.0}) {
            const double back = linearToDb(dbToLinear(db));
            QVERIFY2(std::abs(back - db) < 1e-6,
                     qPrintable(QStringLiteral("round-trip failed at %1 dB (got %2)")
                                    .arg(db).arg(back)));
        }
    }

    void monotonic()
    {
        // More dB always means more linear gain.
        double prev = dbToLinear(-90.0);
        for (double db = -89.0; db <= 12.0; db += 1.0) {
            const double cur = dbToLinear(db);
            QVERIFY(cur > prev);
            prev = cur;
        }
    }
};

QTEST_MAIN(FadeTests)
#include "test_fade.moc"
