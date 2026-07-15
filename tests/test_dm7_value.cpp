#include <QTest>

#include "mix/Dm7Value.h"

#include <cmath>

namespace dm7 = quewi::mix::dm7;

// NB: no `using Reply = dm7::Reply;` here. moc's parser bails on a file-scope
// type alias of a namespace-aliased type and silently emits an EMPTY .moc,
// which surfaces only as three unresolved metaObject symbols at link time.

class Dm7ValueTests : public QObject {
    Q_OBJECT
private slots:
    // ── Tokenizing ───────────────────────────────────────────────────

    void tokenizesPlainLine()
    {
        QCOMPARE(dm7::tokenize(QStringLiteral("get MIXER:Current/InCh/DCA/Assign 0 0")),
                 (QStringList{QStringLiteral("get"),
                              QStringLiteral("MIXER:Current/InCh/DCA/Assign"),
                              QStringLiteral("0"), QStringLiteral("0")}));
    }

    void quotedStringsMayContainSpaces()
    {
        const auto t = dm7::tokenize(
            QStringLiteral(R"(OK set MIXER:Current/DCA/Label/Name 0 0 "Cast Full")"));
        QCOMPARE(t.size(), 6);
        QCOMPARE(t[5], QStringLiteral("Cast Full"));
    }

    void backslashEscapesAreUnwrapped()
    {
        // NOTE: escaped literals, not R"(...)", deliberately. moc processes \"
        // as an escape even INSIDE a raw string, where the standard says it's
        // literal — so a raw string containing \" makes moc think the string
        // never ends. It then swallows the rest of the file, finds no Q_OBJECT,
        // and emits an empty .moc. The only symptom is three unresolved
        // metaObject symbols at link time, pointing nowhere near the cause.
        // Every other raw string in this file is fine; they contain no
        // backslashes.

        // Wire: OK set Addr 0 0 "say \"hi\""   ->  token: say "hi"
        const auto t = dm7::tokenize(QStringLiteral("OK set Addr 0 0 \"say \\\"hi\\\"\""));
        QCOMPARE(t.last(), QStringLiteral("say \"hi\""));

        // Wire: OK set Addr 0 0 "back\\slash"  ->  token: back\slash
        const auto u = dm7::tokenize(QStringLiteral("OK set Addr 0 0 \"back\\\\slash\""));
        QCOMPARE(u.last(), QStringLiteral("back\\slash"));
    }

    void emptyQuotedStringIsARealToken()
    {
        // The unit field is often "" — it must survive as an empty token, not
        // vanish and shift every field after it left by one.
        const auto t = dm7::tokenize(QStringLiteral(R"(OK prminfo 18 "Addr" 120 24 "")"));
        QCOMPARE(t.size(), 7);
        QVERIFY(t.last().isEmpty());
    }

    void quoteRoundTripsThroughTokenize()
    {
        for (const auto &s : {QStringLiteral("simple"),
                              QStringLiteral("with space"),
                              QStringLiteral("quote\"inside"),
                              QStringLiteral("back\\slash"),
                              QStringLiteral("")}) {
            const auto line = QStringLiteral("set A 0 0 ") + dm7::quote(s);
            QCOMPARE(dm7::tokenize(line).last(), s);
        }
    }

    // ── Reply parsing ────────────────────────────────────────────────

    void parsesParameterReply()
    {
        const auto r = dm7::parseReply(
            QStringLiteral("OK set MIXER:Current/InCh/DCA/Assign 0 3 1"));
        QVERIFY(r.has_value());
        QCOMPARE(r->status, dm7::Reply::Status::Ok);
        QCOMPARE(r->action, QStringLiteral("set"));
        QCOMPARE(r->address, QStringLiteral("MIXER:Current/InCh/DCA/Assign"));
        QCOMPARE(r->x, 0);
        QCOMPARE(r->y, 3);
        QCOMPARE(r->value, QStringLiteral("1"));
    }

    // The distinction the whole capture engine rests on: OK is the console
    // echoing OUR write; NOTIFY is somebody touching the surface. Confusing
    // them is how a feedback loop starts.
    void distinguishesOurEchoFromASurfaceChange()
    {
        const auto ours = dm7::parseReply(QStringLiteral("OK set Addr 0 0 1"));
        const auto them = dm7::parseReply(QStringLiteral("NOTIFY set Addr 0 0 1"));
        QVERIFY(ours && them);
        QVERIFY(!ours->isSurfaceChange());
        QVERIFY(them->isSurfaceChange());
        QCOMPARE(them->status, dm7::Reply::Status::Notify);
    }

    void parsesDevinfoReplyWithoutIndices()
    {
        const auto r = dm7::parseReply(QStringLiteral(R"(OK devinfo productname "DM7")"));
        QVERIFY(r.has_value());
        QCOMPARE(r->action, QStringLiteral("devinfo"));
        QCOMPARE(r->address, QStringLiteral("productname"));
        QCOMPARE(r->value, QStringLiteral("DM7"));
        QCOMPARE(r->x, -1);          // no indices on this shape
    }

    void parsesFirmwareReply()
    {
        const auto r = dm7::parseReply(QStringLiteral(R"(OK devinfo version "1.60")"));
        QVERIFY(r.has_value());
        QCOMPARE(r->value, QStringLiteral("1.60"));
    }

    void parsesErrorReply()
    {
        const auto r = dm7::parseReply(QStringLiteral("ERROR get UnknownAddress"));
        QVERIFY(r.has_value());
        QCOMPARE(r->status, dm7::Reply::Status::Error);
        QCOMPARE(r->action, QStringLiteral("get"));
        QCOMPARE(r->value, QStringLiteral("UnknownAddress"));
    }

    void parsesOkModified()
    {
        const auto r = dm7::parseReply(QStringLiteral("OKm set Addr 0 0 1"));
        QVERIFY(r.has_value());
        QCOMPARE(r->status, dm7::Reply::Status::OkModified);
        // Semantics are undocumented; we only need to not choke on it, and to
        // not mistake it for a surface change.
        QVERIFY(!r->isSurfaceChange());
    }

    void parsesTextValueField()
    {
        const auto r = dm7::parseReply(
            QStringLiteral(R"(OK get MIXER:Current/InCh/Label/Color 0 0 4 "Blue")"));
        QVERIFY(r.has_value());
        QCOMPARE(r->value, QStringLiteral("4"));
        QCOMPARE(r->textValue, QStringLiteral("Blue"));
    }

    void rejectsNonReplyLines()
    {
        QVERIFY(!dm7::parseReply(QStringLiteral("")).has_value());
        QVERIFY(!dm7::parseReply(QStringLiteral("garbage")).has_value());
        QVERIFY(!dm7::parseReply(QStringLiteral("set Addr 0 0 1")).has_value()); // no status
    }

    // ── Commands ─────────────────────────────────────────────────────

    void buildsWireCommands()
    {
        QCOMPARE(dm7::setCommand(QLatin1String(dm7::kDcaAssign), 0, 3, QStringLiteral("1")),
                 QStringLiteral("set MIXER:Current/InCh/DCA/Assign 0 3 1"));
        QCOMPARE(dm7::getCommand(QLatin1String(dm7::kChannelOn), 5, 0),
                 QStringLiteral("get MIXER:Current/InCh/Fader/On 5 0"));
    }

    void muteGroupAddressIsNotMuteMaster()
    {
        // DM7 renamed it. Using CL/QL's MuteMaster here would silently no-op.
        QCOMPARE(QLatin1String(dm7::kMuteGrpOn),
                 QLatin1String("MIXER:Current/MuteGrpCtrl/On"));
    }

    // ── Mute ─────────────────────────────────────────────────────────

    void mutePolarityIsInverted()
    {
        QCOMPARE(dm7::onValueForMuted(true), 0);
        QCOMPARE(dm7::onValueForMuted(false), 1);
        QVERIFY(dm7::mutedFromOnValue(0));
        QVERIFY(!dm7::mutedFromOnValue(1));
    }

    // ── dB ───────────────────────────────────────────────────────────

    void dbScalesByOneHundred()
    {
        QCOMPARE(dm7::dbToRaw(0.0f), 0);
        QCOMPARE(dm7::dbToRaw(10.0f), 1000);        // +10.00 dB ceiling
        QCOMPARE(dm7::dbToRaw(-10.0f), -1000);
        QCOMPARE(dm7::dbToRaw(-6.5f), -650);
        QVERIFY(qFuzzyCompare(dm7::rawToDb(-1000) + 1.0f, -10.0f + 1.0f));
    }

    void negativeInfinityUsesTheSentinel()
    {
        QCOMPARE(dm7::dbToRaw(-std::numeric_limits<float>::infinity()), dm7::kLevelNegInf);
        QVERIFY(std::isinf(dm7::rawToDb(dm7::kLevelNegInf)));
        // Anything at or below the lowest real detent is a hard off.
        QCOMPARE(dm7::dbToRaw(-138.0f), dm7::kLevelNegInf);
        QCOMPARE(dm7::dbToRaw(-200.0f), dm7::kLevelNegInf);
    }

    void levelClampsAtCeiling()
    {
        QCOMPARE(dm7::dbToRaw(20.0f), dm7::kLevelMax);
    }

    // ── Pan ──────────────────────────────────────────────────────────

    // Pan has only 27 legal values. Sending an illegal one gets rejected or
    // snapped by the console; snapping ourselves keeps behaviour predictable.
    void panSnapsToLegalValues()
    {
        QVERIFY(dm7::isLegalPan(0));
        QVERIFY(dm7::isLegalPan(63));
        QVERIFY(dm7::isLegalPan(-63));
        QVERIFY(dm7::isLegalPan(5));
        QVERIFY(!dm7::isLegalPan(1));
        QVERIFY(!dm7::isLegalPan(62));      // the gap between 60 and 63
        QVERIFY(!dm7::isLegalPan(-62));

        QCOMPARE(dm7::snapPan(0), 0);
        QCOMPARE(dm7::snapPan(1), 0);
        QCOMPARE(dm7::snapPan(3), 5);
        QCOMPARE(dm7::snapPan(62), 63);
        QCOMPARE(dm7::snapPan(-62), -63);
        QCOMPARE(dm7::snapPan(61), 60);     // nearer 60 than 63
    }

    void panClampsOutOfRange()
    {
        QCOMPARE(dm7::snapPan(999), 63);
        QCOMPARE(dm7::snapPan(-999), -63);
    }

    void snapPanIsIdempotentAndAlwaysLegal()
    {
        for (int p = -70; p <= 70; ++p) {
            const int snapped = dm7::snapPan(p);
            QVERIFY2(dm7::isLegalPan(snapped), qPrintable(QStringLiteral("pan %1").arg(p)));
            QCOMPARE(dm7::snapPan(snapped), snapped);
        }
    }
};

QTEST_MAIN(Dm7ValueTests)
#include "test_dm7_value.moc"
