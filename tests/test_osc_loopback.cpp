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
        // Bind ANY free port and ask the engine which one it got. A hardcoded
        // port can land in a Windows-reserved/excluded range (Hyper-V/WSL/
        // Docker grab dynamic ranges) and fail to bind — which has nothing to
        // do with the dispatch path this test exercises.
        QVERIFY(engine.listenUdp(0));
        const quint16 port = engine.udpPort();
        QVERIFY(port != 0);

        bool received = false;
        QString gotAddress;
        engine.subscribe(QStringLiteral("/cue/{go,stop}"), [&](const Message &m) {
            received = true;
            gotAddress = m.address;
        });

        Destination dest;
        dest.host = QStringLiteral("127.0.0.1");
        dest.port = port;
        dest.transport = Destination::Udp;

        Message msg;
        msg.address = QStringLiteral("/cue/go");
        msg.args.push_back(Argument::i(42));

        QVERIFY(engine.send(dest, msg));

        // Pump the event loop until the datagram arrives and the pattern
        // "/cue/{go,stop}" matches "/cue/go", invoking the handler.
        QTRY_VERIFY_WITH_TIMEOUT(received, 2000);
        QCOMPARE(gotAddress, QStringLiteral("/cue/go"));
    }
};

QTEST_MAIN(OscLoopbackTests)
#include "test_osc_loopback.moc"
