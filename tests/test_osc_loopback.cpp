#include <QCoreApplication>
#include <QSignalSpy>
#include <QTest>

#include "osc/OscEngine.h"

using namespace quewi::osc;

// End-to-end test: bind a UDP listener, fire a packet at it, verify the
// inbound dispatcher matches the address pattern and invokes the handler.
class OscLoopbackTests : public QObject {
    Q_OBJECT
private slots:
    void udpRoundTripDispatchesByPattern()
    {
        OscEngine engine;
        QVERIFY(engine.listenUdp(0)); // 0 = pick any free port
        // QUdpSocket bound on AnyIPv4 with port 0 — we need to know the
        // actual port. We can't get it from the engine API yet, so for
        // this smoke test we'll use a fixed port and accept failure if
        // the port is busy (extremely unlikely).
        // Re-bind to a known port.
        engine.stopAllListeners();
        const quint16 kPort = 53891;
        QVERIFY(engine.listenUdp(kPort));

        bool received = false;
        QString gotAddress;
        engine.subscribe(QStringLiteral("/cue/{go,stop}"), [&](const Message &m) {
            received = true;
            gotAddress = m.address;
        });

        Destination dest;
        dest.host = QStringLiteral("127.0.0.1");
        dest.port = kPort;
        dest.transport = Destination::Udp;

        Message msg;
        msg.address = QStringLiteral("/cue/go");
        msg.args.push_back(Argument::i(42));

        QVERIFY(engine.send(dest, msg));

        // Pump the event loop until the datagram arrives.
        QSignalSpy spy(&engine, &OscEngine::packetSeen);
        QVERIFY(spy.wait(2000));

        QTRY_VERIFY_WITH_TIMEOUT(received, 2000);
        QCOMPARE(gotAddress, QStringLiteral("/cue/go"));
    }
};

QTEST_MAIN(OscLoopbackTests)
#include "test_osc_loopback.moc"
