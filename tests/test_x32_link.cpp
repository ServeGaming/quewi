#include <QTest>
#include <QSignalSpy>
#include <QUdpSocket>

#include "mix/X32Link.h"
#include "mix/X32Value.h"
#include "osc/OscCodec.h"

using namespace quewi::osc;
using quewi::mix::ConsoleLink;
using quewi::mix::DcaSet;
using quewi::mix::X32Link;
namespace x32 = quewi::mix::x32;

// A minimal stand-in for a real desk: binds a UDP port, records what arrives
// (including the SOURCE PORT, which is the whole point of the two-socket
// design), and answers /info like the console does.
class FakeX32 : public QObject {
    Q_OBJECT
public:
    struct Rx {
        Message msg;
        quint16 fromPort;
    };
    QVector<Rx> received;

    FakeX32()
    {
        sock.bind(QHostAddress::LocalHost, 0);
        connect(&sock, &QUdpSocket::readyRead, this, &FakeX32::onReadyRead);
    }

    quint16 port() const { return sock.localPort(); }

    QVector<Rx> matching(const QString &address) const
    {
        QVector<Rx> out;
        for (const auto &r : received)
            if (r.msg.address == address) out.push_back(r);
        return out;
    }

    // Push an unsolicited change, the way the console does to an /xremote
    // client when someone touches the surface.
    void pushToLastXremote(const Message &m)
    {
        if (!xremotePort) return;
        sock.writeDatagram(Codec::encode(m), QHostAddress::LocalHost, *xremotePort);
    }

    std::optional<quint16> xremotePort;

private slots:
    void onReadyRead()
    {
        while (sock.hasPendingDatagrams()) {
            QByteArray b(int(sock.pendingDatagramSize()), Qt::Uninitialized);
            QHostAddress from;
            quint16 fromPort = 0;
            sock.readDatagram(b.data(), b.size(), &from, &fromPort);

            const auto decoded = Codec::decode(b);
            if (!decoded) continue;
            const auto *m = std::get_if<Message>(&*decoded);
            if (!m) continue;

            received.push_back({*m, fromPort});

            if (m->address == QLatin1String("/xremote"))
                xremotePort = fromPort;

            if (m->address == QLatin1String("/info")) {
                const Message reply{QStringLiteral("/info"),
                                    {Argument::s(QStringLiteral("V2.05")),
                                     Argument::s(QStringLiteral("osc-server")),
                                     Argument::s(QStringLiteral("X32C")),
                                     Argument::s(QStringLiteral("4.06"))}};
                sock.writeDatagram(Codec::encode(reply), from, fromPort);
            }
        }
    }

private:
    QUdpSocket sock;
};

class X32LinkTests : public QObject {
    Q_OBJECT

    std::unique_ptr<FakeX32> desk;
    std::unique_ptr<X32Link> link;

    void connectAndSettle()
    {
        link->connectToConsole(QStringLiteral("127.0.0.1"), desk->port());
        QTRY_COMPARE_WITH_TIMEOUT(link->state(), ConsoleLink::State::Connected, 2000);
        QTest::qWait(50);   // let the initial state queries flush
    }

private slots:
    void init()
    {
        desk = std::make_unique<FakeX32>();
        link = std::make_unique<X32Link>();
    }

    void cleanup()
    {
        link.reset();
        desk.reset();
    }

    void connectsAndReadsCapabilitiesFromInfo()
    {
        connectAndSettle();

        QCOMPARE(link->capabilities().model, QStringLiteral("X32C"));
        QCOMPARE(link->capabilities().firmware, QStringLiteral("4.06"));
        // Constants on X32 — every variant exposes the same OSC surface.
        QCOMPARE(link->capabilities().channelCount, 32);
        QCOMPARE(link->capabilities().dcaCount, 8);
        QVERIFY(link->capabilities().inputMetering);
        QVERIFY(link->capabilities().liveCapture);
    }

    void registersXremote()
    {
        connectAndSettle();
        QVERIFY(!desk->matching(QStringLiteral("/xremote")).isEmpty());
    }

    // THE architectural test. The console does not echo a client's own sets
    // back to it, so our only confirmation that a UDP set landed is to send
    // sets from a second socket the console mistakes for another client.
    // If these ports ever collapse to one, confirmation silently dies and a
    // dropped mute becomes undetectable.
    void setsAreSentFromADifferentPortThanXremote()
    {
        connectAndSettle();
        link->setDcaAssignment(3, {1, 2});
        QTest::qWait(50);

        const auto xremote = desk->matching(QStringLiteral("/xremote"));
        const auto sets    = desk->matching(x32::chAddr(3, QLatin1String("grp/dca")));
        QVERIFY(!xremote.isEmpty());
        QVERIFY(!sets.isEmpty());

        // The set that carries an argument is ours; the bare one is a query.
        quint16 setPort = 0;
        for (const auto &s : sets)
            if (!s.msg.args.empty()) setPort = s.fromPort;
        QVERIFY(setPort != 0);
        QVERIFY2(setPort != xremote[0].fromPort,
                 "sets must not share the /xremote socket, or the console "
                 "will not echo them back and we lose all confirmation");
    }

    void dcaAssignmentIsSentAsABitmask()
    {
        connectAndSettle();
        link->setDcaAssignment(3, {1, 3});
        QTest::qWait(50);

        const auto sets = desk->matching(x32::chAddr(3, QLatin1String("grp/dca")));
        std::optional<qint32> mask;
        for (const auto &s : sets)
            if (!s.msg.args.empty()) mask = std::get<qint32>(s.msg.args[0].value);

        QVERIFY(mask.has_value());
        QCOMPARE(*mask, 0b00000101);   // DCA1 = bit 0, DCA3 = bit 2
    }

    void muteUsesInvertedOnSemantics()
    {
        connectAndSettle();
        link->setChannelMuted(7, true);
        QTest::qWait(50);

        const auto sets = desk->matching(x32::chAddr(7, QLatin1String("mix/on")));
        std::optional<qint32> on;
        for (const auto &s : sets)
            if (!s.msg.args.empty()) on = std::get<qint32>(s.msg.args[0].value);

        QVERIFY(on.has_value());
        QCOMPARE(*on, 0);              // muted == mix/on 0
    }

    void dcaLabelIsTruncatedToTwelveChars()
    {
        connectAndSettle();
        link->setDcaLabel(2, QStringLiteral("a-very-long-dca-name-indeed"));
        QTest::qWait(50);

        // Note the single-digit /dca/2, not /dca/02.
        const auto sets = desk->matching(QStringLiteral("/dca/2/config/name"));
        QVERIFY(!sets.isEmpty());
        const auto name = std::get<QString>(sets.last().msg.args[0].value);
        QCOMPARE(name.size(), 12);
    }

    void queriesInitialStateOnConnect()
    {
        connectAndSettle();

        // Scene Safe decides whether anything we do survives a scene recall,
        // so it must be asked for.
        QVERIFY(!desk->matching(QStringLiteral("/-show/showfile/show/inputs")).isEmpty());
        // Channel links, or we produce unexplainable double-moves.
        QVERIFY(!desk->matching(QStringLiteral("/config/chlink/1-2")).isEmpty());
        // And current membership.
        QVERIFY(!desk->matching(x32::chAddr(1, QLatin1String("grp/dca"))).isEmpty());
    }

    void surfaceDcaChangeIsCapturedFromConsolePush()
    {
        connectAndSettle();
        QSignalSpy spy(link.get(), &ConsoleLink::surfaceDcaAssignmentChanged);

        desk->pushToLastXremote({x32::chAddr(5, QLatin1String("grp/dca")),
                                 {Argument::i(0b00000110)}});   // DCA2 + DCA3
        QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 1000);

        QCOMPARE(spy[0][0].toInt(), 5);
        QCOMPARE(link->dcaAssignment(5), DcaSet({2, 3}));
    }

    void sceneSafeGroupsBitIsDecoded()
    {
        connectAndSettle();
        QSignalSpy spy(link.get(), &X32Link::sceneSafeGroupsChanged);

        QVERIFY(!link->sceneSafeGroupsEnabled());

        // bit 5 = Groups. Without it, a scene recall silently reverts every
        // assignment we make.
        desk->pushToLastXremote({QStringLiteral("/-show/showfile/show/inputs"),
                                 {Argument::i(0b00100000)}});
        QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 1000);
        QVERIFY(link->sceneSafeGroupsEnabled());

        // Any other bit set, but not 5 -> still unsafe.
        desk->pushToLastXremote({QStringLiteral("/-show/showfile/show/inputs"),
                                 {Argument::i(0b11011111)}});
        QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 2, 1000);
        QVERIFY(!link->sceneSafeGroupsEnabled());
    }

    void channelLinkStateIsTracked()
    {
        connectAndSettle();

        desk->pushToLastXremote({QStringLiteral("/config/chlink/1-2"), {Argument::i(1)}});
        QTRY_VERIFY_WITH_TIMEOUT(link->isChannelLinked(1), 1000);
        QVERIFY(link->isChannelLinked(2));
        QVERIFY(!link->isChannelLinked(3));

        desk->pushToLastXremote({QStringLiteral("/config/chlink/1-2"), {Argument::i(0)}});
        QTRY_VERIFY_WITH_TIMEOUT(!link->isChannelLinked(1), 1000);
    }

    // A recall changes vast state and the console does not enumerate what
    // changed, so the only correct response is to drop our cached view.
    void sceneRecallForcesResync()
    {
        connectAndSettle();
        link->setDcaAssignment(4, {1});
        QCOMPARE(link->dcaAssignment(4), DcaSet{1});

        QSignalSpy spy(link.get(), &ConsoleLink::resyncRequired);
        desk->pushToLastXremote({QStringLiteral("/-action/goscene"), {Argument::i(3)}});
        QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 1000);

        QCOMPARE(link->dcaAssignment(4), DcaSet{});   // cache dropped
        // Groups isn't safed in this test, so the message must say so.
        QVERIFY(spy[0][0].toString().contains(QStringLiteral("Scene Safe")));
    }

    void malformedDatagramIsIgnoredNotFatal()
    {
        connectAndSettle();
        QUdpSocket junk;
        junk.writeDatagram(QByteArray("not an osc packet at all"),
                           QHostAddress::LocalHost, *desk->xremotePort);
        QTest::qWait(50);
        QCOMPARE(link->state(), ConsoleLink::State::Connected);
    }

    void badHostFailsCleanly()
    {
        QSignalSpy spy(link.get(), &ConsoleLink::errorOccurred);
        link->connectToConsole(QStringLiteral("definitely not an ip"));
        QCOMPARE(link->state(), ConsoleLink::State::Failed);
        QCOMPARE(spy.count(), 1);
    }

    void disconnectStopsTraffic()
    {
        connectAndSettle();
        link->disconnectFromConsole();
        QCOMPARE(link->state(), ConsoleLink::State::Disconnected);

        const int before = desk->received.size();
        QTest::qWait(200);   // longer than the 1.5 s keepalive? no — just check quiet
        QCOMPARE(desk->received.size(), before);
    }
};

QTEST_MAIN(X32LinkTests)
#include "test_x32_link.moc"
