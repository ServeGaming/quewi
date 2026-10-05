#include <QTest>
#include <QApplication>
#include <QPushButton>
#include <QSignalSpy>

#include "ui/TransportBar.h"

using namespace quewi;

// The bottom bar's GO Both: hidden until a Matrix List is up, then sits
// beside GO and fires sound + lights in one press.
class TransportBarTests : public QObject {
    Q_OBJECT

private slots:
    void goBothShowsOnlyWhenAsked()
    {
        ui::TransportBar bar;
        bar.resize(1100, 120);
        bar.show();
        QVERIFY(QTest::qWaitForWindowExposed(&bar));

        auto *both = bar.findChild<QPushButton *>(QStringLiteral("goBothButton"));
        auto *go   = bar.findChild<QPushButton *>(QStringLiteral("goButton"));
        QVERIFY(both && go);
        QVERIFY(!both->isVisible());                  // not a Matrix List: no GO Both

        QSignalSpy pressed(&bar, &ui::TransportBar::goBothPressed);
        QSignalSpy goPressed(&bar, &ui::TransportBar::goPressed);

        // Matrix List up, but nothing to fire: shown, greyed out.
        bar.setGoBothState(true, false, QStringLiteral("No desk"));
        QVERIFY(both->isVisible());
        QVERIFY(!both->isEnabled());
        QCOMPARE(both->toolTip(), QStringLiteral("No desk"));
        QTest::mouseClick(both, Qt::LeftButton);
        QCOMPARE(pressed.count(), 0);

        bar.setGoBothState(true, true, QStringLiteral("Fire sound 1.00 and LX 5 together"));
        QVERIFY(both->isEnabled());
        QCOMPARE(both->property("state").toString(), QStringLiteral("ready"));
        // Right beside GO, on its left.
        QVERIFY(both->geometry().right() < go->geometry().left());
        QVERIFY(go->geometry().left() - both->geometry().right() < 40);
        QTest::mouseClick(both, Qt::LeftButton);
        QCOMPARE(pressed.count(), 1);
        QCOMPARE(goPressed.count(), 0);              // GO Both is its own signal
        QCOMPARE(both->focusPolicy(), Qt::NoFocus);  // Space stays the plain GO

        // Leaving the Matrix List hides it again.
        bar.setGoBothState(false, false, QString());
        QVERIFY(!both->isVisible());
        QVERIFY(!both->isEnabled());
    }
};

QTEST_MAIN(TransportBarTests)
#include "test_transport_bar.moc"
