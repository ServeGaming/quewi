#include <QTest>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtEndian>

#include "osc/EosFeedback.h"
#include "osc/OscCodec.h"
#include "osc/OscMessage.h"

using namespace quewi;

// Reading an Eos desk's state back: the cue-text parser, and the TCP link
// (OSC 1.0 length-prefixed) against a fake desk on localhost.
class EosFeedbackTests : public QObject {
    Q_OBJECT

    static QByteArray framed(const osc::Message &m)
    {
        const QByteArray pkt = osc::Codec::encode(m);
        QByteArray out(4, Qt::Uninitialized);
        qToBigEndian<quint32>(quint32(pkt.size()), out.data());
        return out + pkt;
    }
    static osc::Message text(const char *address, const char *s)
    {
        return {QString::fromLatin1(address), {osc::Argument::s(QString::fromUtf8(s))}};
    }

private slots:
    void parsesCueText()
    {
        auto t = osc::parseEosCueText(QStringLiteral("1/12 Chorus wash 5.00 100%"));
        QCOMPARE(t.list, QStringLiteral("1"));
        QCOMPARE(t.cue, QStringLiteral("12"));
        QCOMPARE(t.label, QStringLiteral("Chorus wash"));
        QCOMPARE(t.time, QStringLiteral("5.00"));
        QCOMPARE(t.percent, 100);

        // A label with numbers in it keeps them (HeliOSC took "2" as the time).
        t = osc::parseEosCueText(QStringLiteral("2/3.5 Scene 2 Act 1 1:30 45%"));
        QCOMPARE(t.cue, QStringLiteral("3.5"));
        QCOMPARE(t.label, QStringLiteral("Scene 2 Act 1"));
        QCOMPARE(t.time, QStringLiteral("1:30"));
        QCOMPARE(t.percent, 45);

        t = osc::parseEosCueText(QStringLiteral("1/7 3 0%"));      // no label
        QCOMPARE(t.label, QString());
        QCOMPARE(t.time, QStringLiteral("3"));
        QVERIFY(osc::parseEosCueText(QString()).isEmpty());
    }

    void handleUpdatesState()
    {
        osc::EosFeedback fb;
        QSignalSpy changed(&fb, &osc::EosFeedback::stateChanged);
        fb.handle(text("/eos/out/active/cue/text", "1/4 Sunrise 10.00 25%"));
        fb.handle(text("/eos/out/pending/cue/text", "1/5 Noon 3.00 0%"));
        fb.handle({QStringLiteral("/eos/out/active/cue"), {osc::Argument::f(0.5f)}});
        fb.handle(text("/eos/out/show/name", "Into the Woods"));
        fb.handle({QStringLiteral("/eos/out/event/state"), {osc::Argument::i(0)}});
        QCOMPARE(fb.active().cue, QStringLiteral("4"));
        QCOMPARE(fb.pending().label, QStringLiteral("Noon"));
        QCOMPARE(fb.reportedProgress(), 0.5);
        QCOMPARE(fb.showName(), QStringLiteral("Into the Woods"));
        QVERIFY(fb.blind());
        QCOMPARE(changed.count(), 5);
        fb.handle(text("/eos/out/pending/cue/text", "1/5 Noon 3.00 0%"));   // same: no signal
        QCOMPARE(changed.count(), 5);
    }

    // The bar runs on between the desk's updates instead of stepping.
    void progressRunsOnSmoothlyBetweenUpdates()
    {
        QCOMPARE(osc::eosTimeSeconds(QStringLiteral("8.00")), 8.0);
        QCOMPARE(osc::eosTimeSeconds(QStringLiteral("1:30")), 90.0);
        QCOMPARE(osc::eosTimeSeconds(QStringLiteral("Label")), -1.0);

        qint64 t = 0;
        osc::EosFeedback fb;
        fb.setClock([&t] { return t; });
        // An 8 s cue at 0 %: until the desk says more, it runs at 1/8 per s.
        fb.handle(text("/eos/out/active/cue/text", "1/5 Fade 8.00 0%"));
        t = 1000;
        QVERIFY(qAbs(fb.activeProgress() - 0.125) < 1e-9);
        // The desk reports 20 % at 1.6 s: measured pace takes over.
        t = 1600;
        fb.handle({QStringLiteral("/eos/out/active/cue"), {osc::Argument::f(0.2f)}});
        QVERIFY(qAbs(fb.activeProgress() - 0.2) < 1e-6);
        t = 2100;
        QVERIFY(fb.activeProgress() > 0.25 && fb.activeProgress() < 0.3);
        // A stalled desk: it stops 2 s past the last update.
        t = 60000;
        const double capped = fb.activeProgress();
        QVERIFY(capped < 0.5);
        t = 90000;
        QCOMPARE(fb.activeProgress(), capped);
        QVERIFY(qAbs(fb.reportedProgress() - 0.2) < 1e-6);
        // A new cue starts again from its own report.
        fb.handle(text("/eos/out/active/cue/text", "1/6 Snap 0.00 100%"));
        QCOMPARE(fb.activeProgress(), 1.0);
    }

    void talksToAFakeDeskOverTcp()
    {
        QTcpServer desk;
        QVERIFY(desk.listen(QHostAddress::LocalHost, 0));
        osc::EosFeedback fb;
        fb.setTimings(200, 100, 600);
        fb.start(QStringLiteral("127.0.0.1"), desk.serverPort());
        QTRY_VERIFY(desk.hasPendingConnections());
        QTcpSocket *peer = desk.nextPendingConnection();

        // It subscribes first thing (framed OSC 1.0).
        QTRY_VERIFY(peer->bytesAvailable() >= 4);
        QByteArray in;
        QTRY_VERIFY((in += peer->readAll(), in.contains("/eos/subscribe")));
        QCOMPARE(fb.link(), osc::EosFeedback::Link::Connecting);   // nothing heard yet

        // Two packets in one write, split mid-way through a second write.
        const QByteArray both = framed(text("/eos/out/active/cue/text", "1/9 Storm 2.00 10%"))
                              + framed(text("/eos/out/pending/cue/text", "1/10 Calm 4.00 0%"));
        peer->write(both.left(both.size() - 5));
        peer->flush();
        QTRY_COMPARE(fb.active().cue, QStringLiteral("9"));
        QCOMPARE(fb.link(), osc::EosFeedback::Link::Live);
        peer->write(both.right(5));
        QTRY_COMPARE(fb.pending().cue, QStringLiteral("10"));

        // The desk goes away: state clears and it reconnects by itself.
        peer->abort();
        QTRY_VERIFY(fb.link() != osc::EosFeedback::Link::Live);
        QVERIFY(fb.active().isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(desk.hasPendingConnections(), 3000);
        QTcpSocket *again = desk.nextPendingConnection();
        again->write(framed(text("/eos/out/active/cue/text", "1/11 Dawn 1.00 0%")));
        QTRY_COMPARE(fb.active().cue, QStringLiteral("11"));

        // Silence for longer than the watchdog: it drops and redials.
        QTRY_VERIFY_WITH_TIMEOUT(desk.hasPendingConnections(), 3000);

        fb.stop();
        QCOMPARE(fb.link(), osc::EosFeedback::Link::Off);
    }

    void failsVisiblyWhenNothingListens()
    {
        QTcpServer probe;
        QVERIFY(probe.listen(QHostAddress::LocalHost, 0));
        const quint16 port = probe.serverPort();
        probe.close();                                  // now nothing's there
        osc::EosFeedback fb;
        fb.setTimings(5000, 1000, 5000);
        fb.start(QStringLiteral("127.0.0.1"), port);
        QTRY_COMPARE(fb.link(), osc::EosFeedback::Link::Failed);
        QVERIFY(fb.detail().contains(QString::number(port)));
    }
};

QTEST_MAIN(EosFeedbackTests)
#include "test_eos_feedback.moc"
