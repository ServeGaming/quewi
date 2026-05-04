#include <QTest>

#include "osc/OscSlip.h"

using quewi::osc::SlipDecoder;
using quewi::osc::SlipEncoder;

class OscSlipTests : public QObject {
    Q_OBJECT
private slots:
    void roundTripsClean()
    {
        const QByteArray packet("hello world", 11);
        const auto framed = SlipEncoder::encode(packet);
        QCOMPARE(framed.front(), static_cast<char>(quint8(0xC0)));
        QCOMPARE(framed.back(),  static_cast<char>(quint8(0xC0)));

        SlipDecoder dec;
        const auto out = dec.feed(framed);
        QCOMPARE(out.size(), size_t(1));
        QCOMPARE(out[0], packet);
    }

    void escapesEndAndEsc()
    {
        QByteArray packet;
        packet.append(static_cast<char>(quint8(0xC0)));
        packet.append(static_cast<char>(quint8(0x42)));
        packet.append(static_cast<char>(quint8(0xDB)));
        packet.append(static_cast<char>(quint8(0x99)));
        const auto framed = SlipEncoder::encode(packet);

        // Body must not contain any literal END except the brackets.
        for (int i = 1; i < framed.size() - 1; ++i)
            QVERIFY(static_cast<quint8>(framed[i]) != 0xC0);

        SlipDecoder dec;
        const auto out = dec.feed(framed);
        QCOMPARE(out.size(), size_t(1));
        QCOMPARE(out[0], packet);
    }

    void reassemblesAcrossChunks()
    {
        const QByteArray a("alpha", 5);
        const QByteArray b("bravo", 5);
        QByteArray stream = SlipEncoder::encode(a) + SlipEncoder::encode(b);

        SlipDecoder dec;
        std::vector<QByteArray> all;
        // Feed one byte at a time — worst case for streaming decoder.
        for (int i = 0; i < stream.size(); ++i) {
            auto chunk = dec.feed(stream.mid(i, 1));
            for (auto &f : chunk) all.push_back(std::move(f));
        }
        QCOMPARE(all.size(), size_t(2));
        QCOMPARE(all[0], a);
        QCOMPARE(all[1], b);
    }

    void leadingEndsResyncCleanly()
    {
        // Many transports send an extra END before each frame.
        QByteArray stream;
        stream.append(static_cast<char>(quint8(0xC0)));
        stream.append(static_cast<char>(quint8(0xC0)));
        stream.append(SlipEncoder::encode(QByteArray("x", 1)));
        SlipDecoder dec;
        const auto out = dec.feed(stream);
        QCOMPARE(out.size(), size_t(1));
        QCOMPARE(out[0], QByteArray("x", 1));
    }
};

QTEST_MAIN(OscSlipTests)
#include "test_osc_slip.moc"
