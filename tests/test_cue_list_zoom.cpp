#include <QTest>
#include <QApplication>
#include <QNativeGestureEvent>
#include <QPointingDevice>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardItemModel>
#include <QWheelEvent>

#include "ui/CueListView.h"

using namespace quewi;

// Ctrl+wheel / Ctrl+= / Ctrl+- / Ctrl+0 zoom the cue list's text. The zoom
// is saved in the user's real QSettings, so it's put back afterwards.
class CueListZoomTests : public QObject {
    Q_OBJECT

    QVariant m_saved;
    static QSettings settings() { return QSettings(QStringLiteral("ServeGaming"), QStringLiteral("quewi")); }

    // Real wheel events land on the list's viewport.
    static void wheel(ui::CueListView *view, int delta, Qt::KeyboardModifiers mods,
                      Qt::ScrollPhase phase = Qt::NoScrollPhase, QPoint pixel = QPoint())
    {
        QWidget *w = view->viewport();
        const QPointF pos(w->width() / 2.0, w->height() / 2.0);
        QWheelEvent e(pos, w->mapToGlobal(pos), pixel, QPoint(0, delta),
                      Qt::NoButton, mods, phase, false);
        QApplication::sendEvent(w, &e);
    }

    // A macOS trackpad pinch: also lands on the viewport.
    static void pinch(ui::CueListView *view, Qt::NativeGestureType type, double value)
    {
        QWidget *w = view->viewport();
        const QPointF pos(w->width() / 2.0, w->height() / 2.0);
        QNativeGestureEvent e(type, QPointingDevice::primaryPointingDevice(), 2, pos, pos,
                              w->mapToGlobal(pos), value, QPointF());
        QApplication::sendEvent(w, &e);
    }

    static void showFresh(ui::CueListView &view, QStandardItemModel &model)
    {
        settings().remove(QStringLiteral("cueList/zoom"));
        view.QTreeView::setModel(&model);
        view.resize(500, 400);
        view.show();
    }

private slots:
    void initTestCase()  { m_saved = settings().value(QStringLiteral("cueList/zoom")); settings().remove(QStringLiteral("cueList/zoom")); }
    void cleanupTestCase()
    {
        if (m_saved.isValid()) settings().setValue(QStringLiteral("cueList/zoom"), m_saved);
        else settings().remove(QStringLiteral("cueList/zoom"));
    }

    void ctrlWheelAndKeysZoom()
    {
        ui::CueListView view;
        QStandardItemModel model(20, 3);
        view.QTreeView::setModel(&model);
        view.resize(500, 400);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        QCOMPARE(view.zoom(), 1.0);
        const int rowBefore = view.sizeHintForRow(0);

        QSignalSpy changed(&view, &ui::CueListView::zoomChanged);
        wheel(&view, 120, Qt::ControlModifier);
        QCOMPARE(view.zoom(), 1.1);
        wheel(&view, 240, Qt::ControlModifier);
        QCOMPARE(view.zoom(), 1.3);
        QVERIFY(view.styleSheet().contains(QStringLiteral("font-size: 17px")));
        QVERIFY(view.sizeHintForRow(0) > rowBefore);           // rows grow with the text
        QCOMPARE(settings().value(QStringLiteral("cueList/zoom")).toDouble(), 1.3);

        // A high-res wheel: part-notches add up to one step.
        wheel(&view, -60, Qt::ControlModifier);
        QCOMPARE(view.zoom(), 1.3);
        wheel(&view, -60, Qt::ControlModifier);
        QCOMPARE(view.zoom(), 1.2);

        // Plain wheel scrolls, doesn't zoom.
        wheel(&view, 120, Qt::NoModifier);
        QCOMPARE(view.zoom(), 1.2);

        QTest::keyClick(&view, Qt::Key_Minus, Qt::ControlModifier);
        QCOMPARE(view.zoom(), 1.1);
        QTest::keyClick(&view, Qt::Key_Equal, Qt::ControlModifier);
        QCOMPARE(view.zoom(), 1.2);
        QTest::keyClick(&view, Qt::Key_0, Qt::ControlModifier);
        QCOMPARE(view.zoom(), 1.0);
        QVERIFY(view.styleSheet().isEmpty());

        // Limits.
        for (int i = 0; i < 40; ++i) QTest::keyClick(&view, Qt::Key_Equal, Qt::ControlModifier);
        QCOMPARE(view.zoom(), ui::CueListView::kMaxZoom);
        for (int i = 0; i < 40; ++i) QTest::keyClick(&view, Qt::Key_Minus, Qt::ControlModifier);
        QCOMPARE(view.zoom(), ui::CueListView::kMinZoom);
        QVERIFY(changed.count() > 5);

        // A new list opens at the saved zoom.
        settings().setValue(QStringLiteral("cueList/zoom"), 1.5);
        ui::CueListView again;
        QCOMPARE(again.zoom(), 1.5);
    }

    void trackpadMomentumDoesNotZoom()
    {
        settings().remove(QStringLiteral("cueList/zoom"));
        ui::CueListView view;
        QStandardItemModel model(20, 3);
        showFresh(view, model);
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        QCOMPARE(view.zoom(), 1.0);

        // Command+swipe: the fingers' own events zoom...
        wheel(&view, 0, Qt::ControlModifier, Qt::ScrollBegin, QPoint());
        wheel(&view, 60, Qt::ControlModifier, Qt::ScrollUpdate, QPoint(0, 20));
        wheel(&view, 60, Qt::ControlModifier, Qt::ScrollUpdate, QPoint(0, 20));
        QCOMPARE(view.zoom(), 1.1);
        // ...the momentum after they lift doesn't.
        for (int i = 0; i < 30; ++i)
            wheel(&view, 120, Qt::ControlModifier, Qt::ScrollMomentum, QPoint(0, 40));
        wheel(&view, 0, Qt::ControlModifier, Qt::ScrollEnd, QPoint());
        QCOMPARE(view.zoom(), 1.1);
    }

    void pinchZooms()
    {
        settings().remove(QStringLiteral("cueList/zoom"));
        ui::CueListView view;
        QStandardItemModel model(20, 3);
        showFresh(view, model);
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        QCOMPARE(view.zoom(), 1.0);

        // Small pinch steps add up: 0.02 each, ten of them = 0.2 = two steps.
        pinch(&view, Qt::BeginNativeGesture, 0.0);
        for (int i = 0; i < 10; ++i) pinch(&view, Qt::ZoomNativeGesture, 0.02);
        pinch(&view, Qt::EndNativeGesture, 0.0);
        QCOMPARE(view.zoom(), 1.2);

        // A pinch in shrinks it back.
        pinch(&view, Qt::BeginNativeGesture, 0.0);
        for (int i = 0; i < 5; ++i) pinch(&view, Qt::ZoomNativeGesture, -0.02);
        pinch(&view, Qt::EndNativeGesture, 0.0);
        QCOMPARE(view.zoom(), 1.1);
    }
};

QTEST_MAIN(CueListZoomTests)
#include "test_cue_list_zoom.moc"
