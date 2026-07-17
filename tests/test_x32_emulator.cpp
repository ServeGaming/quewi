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

// Integration test against pmaillot's X32 emulator (or a real X32/M32).
//
// Unlike test_x32_link — which drives X32Link against a FakeX32 written to our
// own understanding of the protocol — this exercises the REAL production link
// against an INDEPENDENT implementation. If our reading of the wire format is
// wrong, the fake would agree with us and hide it; the emulator won't.
//
// It SKIPS cleanly when no emulator answers on 127.0.0.1:10023, so CI (where
// nothing is listening) stays green. To run the real thing:
//   1. build pmaillot's emulator (see docs/dev/session-handoff.md)
//   2. X32.exe -i 127.0.0.1
//   3. ctest -R x32_emulator   (or set QUEWI_X32_HOST to point elsewhere)
class X32EmulatorTests : public QObject {
    Q_OBJECT

    QString host() const
    {
        const QByteArray env = qgetenv("QUEWI_X32_HOST");
        return env.isEmpty() ? QStringLiteral("127.0.0.1") : QString::fromLocal8Bit(env);
    }

    // A direct probe socket, independent of X32Link, so we can read the
    // emulator's state back and check X32Link actually changed it.
    bool emulatorReachable(quint16 &outPort)
    {
        QUdpSocket probe;
        if (!probe.bind(QHostAddress::AnyIPv4, 0)) return false;
        outPort = probe.localPort();
        const Message info{QStringLiteral("/info"), {}};
        probe.writeDatagram(Codec::encode(info), QHostAddress(host()), 10023);
        return probe.waitForReadyRead(1500);
    }

    // Read one parameter's current value straight from the console.
    std::optional<qint32> readInt(const QString &address)
    {
        QUdpSocket probe;
        if (!probe.bind(QHostAddress::AnyIPv4, 0)) return std::nullopt;
        probe.writeDatagram(Codec::encode(Message{address, {}}), QHostAddress(host()), 10023);
        if (!probe.waitForReadyRead(1500)) return std::nullopt;
        QByteArray buf(int(probe.pendingDatagramSize()), Qt::Uninitialized);
        probe.readDatagram(buf.data(), buf.size());
        const auto decoded = Codec::decode(buf);
        if (!decoded) return std::nullopt;
        if (const auto *m = std::get_if<Message>(&*decoded))
            if (!m->args.empty())
                if (const auto n = firstNumber(*m)) return qint32(*n);
        return std::nullopt;
    }

    std::unique_ptr<X32Link> link;
    bool skipAll = false;

private slots:
    void initTestCase()
    {
        quint16 p = 0;
        if (!emulatorReachable(p)) {
            skipAll = true;
            QSKIP("No X32 emulator on 127.0.0.1:10023 — run X32.exe -i 127.0.0.1 to "
                  "exercise this. Skipping (this is expected in CI).");
        }
    }

    void init()
    {
        if (skipAll) QSKIP("no emulator");
        link = std::make_unique<X32Link>();
    }
    void cleanup() { link.reset(); }

    // The connect handshake against a real /info reply.
    void connectsAndIdentifies()
    {
        link->connectToConsole(host());
        QTRY_COMPARE_WITH_TIMEOUT(link->state(), ConsoleLink::State::Connected, 4000);

        // The emulator reports itself as an X32 at FW 4.06 — the model string
        // must be parsed from the real ,ssss /info reply, not assumed.
        QCOMPARE(link->capabilities().model, QStringLiteral("X32"));
        QVERIFY(!link->capabilities().firmware.isEmpty());
        QCOMPARE(link->capabilities().dcaCount, 8);
        QCOMPARE(link->capabilities().channelCount, 32);
    }

    // The one that matters: a DCA assignment made through the production link
    // must land as the correct BITMASK on a real console. If our DCA1=bit0
    // reading were wrong, this is where it would show.
    void dcaAssignmentLandsAsTheRightMask()
    {
        link->connectToConsole(host());
        QTRY_COMPARE_WITH_TIMEOUT(link->state(), ConsoleLink::State::Connected, 4000);
        QTest::qWait(100);   // let the initial state sync settle

        // Put channel 5 on DCA1 and DCA3 -> mask 0b00000101 = 5.
        link->setDcaAssignment(5, {1, 3});
        QTRY_COMPARE_WITH_TIMEOUT(readInt(x32::chAddr(5, QLatin1String("grp/dca"))),
                                  std::optional<qint32>(5), 2000);

        // Move it to DCA2 only -> mask 0b00000010 = 2. Proves the replace (not
        // toggle) semantics survive a real round trip.
        link->setDcaAssignment(5, {2});
        QTRY_COMPARE_WITH_TIMEOUT(readInt(x32::chAddr(5, QLatin1String("grp/dca"))),
                                  std::optional<qint32>(2), 2000);

        // Clear it.
        link->setDcaAssignment(5, {});
        QTRY_COMPARE_WITH_TIMEOUT(readInt(x32::chAddr(5, QLatin1String("grp/dca"))),
                                  std::optional<qint32>(0), 2000);
    }

    // Mute is mix/on inverted: muting sends 0. Verify against the real desk.
    void muteSendsInvertedOn()
    {
        link->connectToConsole(host());
        QTRY_COMPARE_WITH_TIMEOUT(link->state(), ConsoleLink::State::Connected, 4000);
        QTest::qWait(100);

        link->setChannelMuted(7, true);
        QTRY_COMPARE_WITH_TIMEOUT(readInt(x32::chAddr(7, QLatin1String("mix/on"))),
                                  std::optional<qint32>(0), 2000);   // 0 = muted
        link->setChannelMuted(7, false);
        QTRY_COMPARE_WITH_TIMEOUT(readInt(x32::chAddr(7, QLatin1String("mix/on"))),
                                  std::optional<qint32>(1), 2000);   // 1 = unmuted
    }

    // applyCue is the whole show-fire operation: named channels assigned,
    // everything else muted. Verify a couple of channels the cue does and
    // doesn't name.
    void applyCueAssignsAndMutes()
    {
        link->connectToConsole(host());
        QTRY_COMPARE_WITH_TIMEOUT(link->state(), ConsoleLink::State::Connected, 4000);
        QTest::qWait(100);

        // Cue: ch2 on DCA1, ch4 on DCA2. Everything else muted.
        link->applyCue({{2, DcaSet{1}}, {4, DcaSet{2}}});

        QTRY_COMPARE_WITH_TIMEOUT(readInt(x32::chAddr(2, QLatin1String("grp/dca"))),
                                  std::optional<qint32>(1), 2000);   // DCA1
        QTRY_COMPARE_WITH_TIMEOUT(readInt(x32::chAddr(4, QLatin1String("grp/dca"))),
                                  std::optional<qint32>(2), 2000);   // DCA2
        // ch2 and ch4 are named -> unmuted; ch1 (not named) -> muted.
        QTRY_COMPARE_WITH_TIMEOUT(readInt(x32::chAddr(2, QLatin1String("mix/on"))),
                                  std::optional<qint32>(1), 2000);
        QCOMPARE(readInt(x32::chAddr(1, QLatin1String("mix/on"))), std::optional<qint32>(0));
    }
};

QTEST_MAIN(X32EmulatorTests)
#include "test_x32_emulator.moc"
