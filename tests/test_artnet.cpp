// Art-Net ArtDmx packet tests. The header is hand-rolled per the Art-Net 4
// spec, so verify it byte-for-byte — a wrong opcode or universe split sends
// silently to nothing, which is invisible without a node on the bench.
#include "lighting/ArtNet.h"

#include <QtTest/QtTest>

using quewi::lighting::ArtNetSender;
using quewi::lighting::DmxFrame;

class TestArtNet : public QObject {
    Q_OBJECT
private slots:

    void headerLayout() {
        DmxFrame frame{};
        frame[0]   = 255;
        frame[511] = 1;
        const QByteArray p = ArtNetSender::buildPacket(/*universe*/1, /*seq*/7, frame);

        QCOMPARE(p.size(), 18 + 512);
        QCOMPARE(p.left(8), QByteArray("Art-Net\0", 8));  // ID incl. trailing NUL
        // OpCode 0x5000 — little-endian on the wire.
        QCOMPARE(quint8(p[8]),  quint8(0x00));
        QCOMPARE(quint8(p[9]),  quint8(0x50));
        // Protocol version 14 — big-endian.
        QCOMPARE(quint8(p[10]), quint8(0x00));
        QCOMPARE(quint8(p[11]), quint8(0x0E));
        QCOMPARE(quint8(p[12]), quint8(7));     // sequence
        QCOMPARE(quint8(p[13]), quint8(0));     // physical
        QCOMPARE(quint8(p[14]), quint8(1));     // SubUni — universe 1 low byte
        QCOMPARE(quint8(p[15]), quint8(0));     // Net — high 7 bits
        // Length 512 — big-endian.
        QCOMPARE(quint8(p[16]), quint8(0x02));
        QCOMPARE(quint8(p[17]), quint8(0x00));
        // DMX data follows the 18-byte header.
        QCOMPARE(quint8(p[18]),       quint8(255));
        QCOMPARE(quint8(p[18 + 511]), quint8(1));
    }

    void universeSplit() {
        DmxFrame frame{};
        // Universe 260 = 0x0104 → SubUni 0x04 (low byte), Net 0x01 (high 7 bits).
        const QByteArray p = ArtNetSender::buildPacket(260, 1, frame);
        QCOMPARE(quint8(p[14]), quint8(0x04));
        QCOMPARE(quint8(p[15]), quint8(0x01));
    }
};

QTEST_MAIN(TestArtNet)
#include "test_artnet.moc"
