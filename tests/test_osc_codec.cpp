#include <QTest>

#include "osc/OscCodec.h"
#include "osc/OscMessage.h"

#include <QColor>

using namespace quewi::osc;

class OscCodecTests : public QObject {
    Q_OBJECT
private slots:
    void roundTripsAllScalarTypes()
    {
        Message m;
        m.address = QStringLiteral("/test/everything");
        m.args.push_back(Argument::i(-42));
        m.args.push_back(Argument::f(3.14159f));
        m.args.push_back(Argument::s(QStringLiteral("hello world")));
        m.args.push_back(Argument::b(QByteArray("\x01\x02\x03\x04\x05", 5)));
        m.args.push_back(Argument::h(0x1122334455667788LL));
        m.args.push_back(Argument::t(TimeTag{0xC0FFEEC0FFEEC0FFULL}));
        m.args.push_back(Argument::d(2.718281828459045));
        m.args.push_back(Argument::symbol(QStringLiteral("symbol-thing")));
        m.args.push_back(Argument::c(0x41)); // 'A'
        m.args.push_back(Argument::r(QColor(10, 20, 30, 40)));
        m.args.push_back(Argument::m(MidiMessage{0, 0x90, 60, 100}));
        m.args.push_back(Argument::T());
        m.args.push_back(Argument::F());
        m.args.push_back(Argument::N());
        m.args.push_back(Argument::I());

        const auto bytes = Codec::encode(m);
        QVERIFY(bytes.size() % 4 == 0); // OSC packet must be 4-byte aligned

        const auto decoded = Codec::decode(bytes);
        QVERIFY(decoded.has_value());
        QVERIFY(std::holds_alternative<Message>(*decoded));
        const auto &back = std::get<Message>(*decoded);

        QCOMPARE(back.address, m.address);
        QCOMPARE(static_cast<int>(back.args.size()), static_cast<int>(m.args.size()));

        QCOMPARE(std::get<qint32>(back.args[0].value),     -42);
        QCOMPARE(std::get<float>(back.args[1].value),      3.14159f);
        QCOMPARE(std::get<QString>(back.args[2].value),    QStringLiteral("hello world"));
        QCOMPARE(std::get<QByteArray>(back.args[3].value), QByteArray("\x01\x02\x03\x04\x05", 5));
        QCOMPARE(std::get<qint64>(back.args[4].value),     0x1122334455667788LL);
        QCOMPARE(std::get<TimeTag>(back.args[5].value).ntp, 0xC0FFEEC0FFEEC0FFULL);
        QCOMPARE(std::get<double>(back.args[6].value),     2.718281828459045);
        QCOMPARE(std::get<QString>(back.args[7].value),    QStringLiteral("symbol-thing"));
        QCOMPARE(std::get<qint32>(back.args[8].value),     0x41);
        const auto col = std::get<QColor>(back.args[9].value);
        QCOMPARE(col.red(),   10);
        QCOMPARE(col.green(), 20);
        QCOMPARE(col.blue(),  30);
        QCOMPARE(col.alpha(), 40);
        const auto midi = std::get<MidiMessage>(back.args[10].value);
        QCOMPARE(midi.portId, quint8(0));
        QCOMPARE(midi.status, quint8(0x90));
        QCOMPARE(midi.data1,  quint8(60));
        QCOMPARE(midi.data2,  quint8(100));
        QCOMPARE(back.args[11].tag, Argument::Tag::True);
        QCOMPARE(back.args[12].tag, Argument::Tag::False);
        QCOMPARE(back.args[13].tag, Argument::Tag::Nil);
        QCOMPARE(back.args[14].tag, Argument::Tag::Infinitum);
    }

    void roundTripsArrays()
    {
        Message m;
        m.address = QStringLiteral("/array/test");
        Array inner;
        inner.push_back(Argument::i(1));
        inner.push_back(Argument::i(2));
        Array outer;
        outer.push_back(Argument::i(0));
        outer.push_back(Argument::array(std::move(inner)));
        outer.push_back(Argument::i(99));
        m.args.push_back(Argument::array(std::move(outer)));
        m.args.push_back(Argument::s(QStringLiteral("after")));

        const auto bytes = Codec::encode(m);
        const auto decoded = Codec::decode(bytes);
        QVERIFY(decoded.has_value());
        const auto &back = std::get<Message>(*decoded);
        QCOMPARE(back.args.size(), size_t(2));
        QCOMPARE(back.args[0].tag, Argument::Tag::Array);
        const auto &lvl1 = std::get<Array>(back.args[0].value);
        QCOMPARE(lvl1.size(), size_t(3));
        QCOMPARE(std::get<qint32>(lvl1[0].value), 0);
        QCOMPARE(lvl1[1].tag, Argument::Tag::Array);
        const auto &lvl2 = std::get<Array>(lvl1[1].value);
        QCOMPARE(lvl2.size(), size_t(2));
        QCOMPARE(std::get<qint32>(lvl2[0].value), 1);
        QCOMPARE(std::get<qint32>(lvl2[1].value), 2);
        QCOMPARE(std::get<qint32>(lvl1[2].value), 99);
        QCOMPARE(std::get<QString>(back.args[1].value), QStringLiteral("after"));
    }

    // Regression: a malformed packet whose type-tag string is one ',' followed
    // by thousands of unmatched '[' recurses the array decoder one level per
    // bracket with no data bytes consumed. Without a depth guard this blows the
    // stack (an unauthenticated remote DoS). It must be rejected, not crash.
    void rejectsDeeplyNestedArrays()
    {
        QByteArray pkt;
        pkt.append("/x", 2);
        pkt.append('\0'); pkt.append('\0');   // "/x\0\0" — 4-byte-aligned address

        QByteArray tags;
        tags.append(',');
        tags.append(QByteArray(20000, '['));  // 20k open brackets, never closed
        tags.append('\0');
        while (tags.size() % 4 != 0) tags.append('\0');   // pad to 4 bytes
        pkt.append(tags);

        // The assertion is almost secondary — the real test is that this call
        // returns at all instead of overflowing the stack.
        const auto decoded = Codec::decode(pkt);
        QVERIFY(!decoded.has_value());
    }

    void roundTripsBundles()
    {
        Bundle b;
        b.timeTag = TimeTag{0xDEADBEEFCAFEBABEULL};

        Message m1;
        m1.address = QStringLiteral("/foo");
        m1.args.push_back(Argument::i(7));

        Message m2;
        m2.address = QStringLiteral("/bar/baz");
        m2.args.push_back(Argument::s(QStringLiteral("x")));

        Bundle inner;
        inner.timeTag = TimeTag::immediate();
        inner.elements.push_back(m2);

        b.elements.push_back(m1);
        b.elements.push_back(inner);

        const auto bytes = Codec::encode(b);
        const auto decoded = Codec::decode(bytes);
        QVERIFY(decoded.has_value());
        QVERIFY(std::holds_alternative<Bundle>(*decoded));
        const auto &back = std::get<Bundle>(*decoded);
        QCOMPARE(back.timeTag.ntp, 0xDEADBEEFCAFEBABEULL);
        QCOMPARE(back.elements.size(), size_t(2));
        QVERIFY(std::holds_alternative<Message>(back.elements[0]));
        QVERIFY(std::holds_alternative<Bundle>(back.elements[1]));
        const auto &innerBack = std::get<Bundle>(back.elements[1]);
        QCOMPARE(innerBack.elements.size(), size_t(1));
        const auto &m2Back = std::get<Message>(innerBack.elements[0]);
        QCOMPARE(m2Back.address, QStringLiteral("/bar/baz"));
    }

    void rejectsTruncatedInput()
    {
        Message m;
        m.address = QStringLiteral("/x");
        m.args.push_back(Argument::i(42));
        const auto bytes = Codec::encode(m);
        for (int n = 0; n < bytes.size(); ++n) {
            QVERIFY2(!Codec::decode(bytes.left(n)).has_value(),
                qPrintable(QStringLiteral("decoded a truncated buffer of length %1").arg(n)));
        }
        // Full buffer still works.
        QVERIFY(Codec::decode(bytes).has_value());
    }

    void encodesAddressIsPaddedToFourBytes()
    {
        Message m;
        m.address = QStringLiteral("/x"); // 2 chars + NUL = 3, pad to 4
        const auto bytes = Codec::encode(m);
        // bytes layout: "/x\0\0,\0\0\0" — 8 bytes total
        QCOMPARE(bytes.size(), 8);
    }
};

QTEST_MAIN(OscCodecTests)
#include "test_osc_codec.moc"
