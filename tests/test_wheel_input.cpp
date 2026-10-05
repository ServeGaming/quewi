#include <QTest>
#include <QApplication>
#include <QHeaderView>
#include <QScrollBar>
#include <QStandardItemModel>
#include <QTableView>
#include <QWheelEvent>

#include <cmath>

#include "audio/AudioEditorModel.h"
#include "ui/SmoothScroll.h"
#include "ui/TimelineCanvas.h"

using namespace quewi;

// Trackpads (macOS, Linux) send dozens of small pixel-delta wheel events a
// gesture, with begin / end markers that carry no delta and "momentum"
// events after the fingers lift. A mouse wheel sends whole 120-unit notches
// with no phase. These check both behave: the trackpad smoothly and
// boundedly, the mouse exactly as before.
class WheelInputTests : public QObject {
    Q_OBJECT

    static void send(QWidget *w, QPoint pixel, QPoint angle, Qt::ScrollPhase phase,
                     Qt::KeyboardModifiers mods = Qt::NoModifier)
    {
        const QPointF pos(w->width() / 2.0, w->height() / 2.0);
        QWheelEvent e(pos, w->mapToGlobal(pos), pixel, angle, Qt::NoButton, mods, phase, false);
        QApplication::sendEvent(w, &e);
    }

private slots:
    void timelineZeroDeltaDoesNotZoom()
    {
        audio::AudioEditorModel model;
        model.addTrack(QStringLiteral("T"));
        ui::TimelineCanvas canvas(&model);
        canvas.resize(900, 300);
        canvas.setFramesPerPixel(100.0);

        // A gesture's begin / end markers (and stray empty events) used to
        // fall into the "zoom out" branch.
        send(&canvas, {}, {}, Qt::NoScrollPhase, Qt::ControlModifier);
        send(&canvas, {}, {}, Qt::ScrollBegin, Qt::ControlModifier);
        send(&canvas, {}, {}, Qt::ScrollEnd, Qt::ControlModifier);
        QCOMPARE(canvas.framesPerPixel(), 100.0);

        // A mouse notch still snaps one step each way, exactly.
        send(&canvas, {}, QPoint(0, 120), Qt::NoScrollPhase, Qt::ControlModifier);
        QCOMPARE(canvas.framesPerPixel(), 80.0);
        send(&canvas, {}, QPoint(0, -120), Qt::NoScrollPhase, Qt::ControlModifier);
        QCOMPARE(canvas.framesPerPixel(), 100.0);
        // A high-res wheel's half-notches add up to one step.
        send(&canvas, {}, QPoint(0, 60), Qt::NoScrollPhase, Qt::ControlModifier);
        QCOMPARE(canvas.framesPerPixel(), 100.0);
        send(&canvas, {}, QPoint(0, 60), Qt::NoScrollPhase, Qt::ControlModifier);
        QCOMPARE(canvas.framesPerPixel(), 80.0);
    }

    void timelineTrackpadZoomIsSmoothAndBounded()
    {
        audio::AudioEditorModel model;
        model.addTrack(QStringLiteral("T"));
        ui::TimelineCanvas canvas(&model);
        canvas.resize(900, 300);
        canvas.setFramesPerPixel(100.0);

        // 100 events of 5 px = 500 px of pinch-free Ctrl+swipe: five steps
        // in total, no single event more than one step.
        send(&canvas, {}, {}, Qt::ScrollBegin, Qt::ControlModifier);
        for (int i = 0; i < 100; ++i) {
            const double before = canvas.framesPerPixel();
            send(&canvas, QPoint(0, 5), QPoint(0, 15), Qt::ScrollUpdate, Qt::ControlModifier);
            const double ratio = canvas.framesPerPixel() / before;
            QVERIFY(ratio < 1.0);
            QVERIFY(ratio >= 0.8);
        }
        const double afterSwipe = canvas.framesPerPixel();
        QVERIFY(std::abs(afterSwipe - 100.0 * std::pow(0.8, 5.0)) < 0.01);

        // Momentum after the fingers lift doesn't keep zooming.
        for (int i = 0; i < 50; ++i)
            send(&canvas, QPoint(0, 5), QPoint(0, 15), Qt::ScrollMomentum, Qt::ControlModifier);
        send(&canvas, {}, {}, Qt::ScrollEnd, Qt::ControlModifier);
        QCOMPARE(canvas.framesPerPixel(), afterSwipe);

        // One huge flick event is still only one step.
        send(&canvas, QPoint(0, -2000), QPoint(0, -6000), Qt::ScrollUpdate, Qt::ControlModifier);
        QVERIFY(std::abs(canvas.framesPerPixel() - afterSwipe * 1.25) < 0.01);
    }

    void timelinePansSideways()
    {
        audio::AudioEditorModel model;
        model.addTrack(QStringLiteral("T"));
        ui::TimelineCanvas canvas(&model);
        QScrollBar h(Qt::Horizontal), v(Qt::Vertical);
        canvas.resize(900, 300);
        canvas.setFramesPerPixel(100.0);
        canvas.setScrollBars(&h, &v);
        QVERIFY(h.maximum() > 1000);

        // A trackpad swipe sideways follows the fingers at once.
        h.setValue(500);
        send(&canvas, QPoint(-30, 0), QPoint(-90, 0), Qt::ScrollUpdate);
        QCOMPARE(h.value(), 530);
        // Zero-delta markers do nothing.
        send(&canvas, {}, {}, Qt::ScrollEnd);
        QCOMPARE(h.value(), 530);

        // macOS hands Shift+wheel over as an x delta: it pans (gliding).
        send(&canvas, {}, QPoint(-120, 0), Qt::NoScrollPhase, Qt::ShiftModifier);
        QTRY_COMPARE(h.value(), 650);
        // Windows / Linux keep it in y: Shift still pans sideways.
        send(&canvas, {}, QPoint(0, -120), Qt::NoScrollPhase, Qt::ShiftModifier);
        QTRY_COMPARE(h.value(), 770);
    }

    void perItemTrackpadSwipeMovesItsDistance()
    {
        ui::SmoothScroll::install(qApp);
        QStandardItemModel items(500, 3);
        QTableView view;
        view.setModel(&items);
        view.verticalHeader()->setDefaultSectionSize(30);
        view.setAttribute(Qt::WA_DontShowOnScreen);
        view.resize(400, 300);
        view.show();
        QTest::qWait(50);
        QCOMPARE(view.verticalScrollMode(), QAbstractItemView::ScrollPerItem);
        QScrollBar *bar = view.verticalScrollBar();
        bar->setValue(0);

        // 100 events of 3 px = 300 px = 10 rows of 30 px. Rounding each
        // event up to a row used to send this 100 rows. No glide: the
        // scrollbar is already there when the last event lands.
        send(view.viewport(), {}, {}, Qt::ScrollBegin);
        for (int i = 0; i < 100; ++i)
            send(view.viewport(), QPoint(0, -3), QPoint(0, -9), Qt::ScrollUpdate);
        send(view.viewport(), {}, {}, Qt::ScrollEnd);
        QCOMPARE(bar->value(), 10);

        // A mouse notch is unchanged: three rows, glided.
        send(view.viewport(), {}, QPoint(0, -120), Qt::NoScrollPhase);
        QTRY_COMPARE(bar->value(), 13);
    }

    void perPixelTrackpadIsNotAnimated()
    {
        ui::SmoothScroll::install(qApp);
        QStandardItemModel items(500, 3);
        QTableView view;
        view.setModel(&items);
        view.setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        view.setAttribute(Qt::WA_DontShowOnScreen);
        view.resize(400, 300);
        view.show();
        QTest::qWait(50);
        QScrollBar *bar = view.verticalScrollBar();
        bar->setValue(0);
        send(view.viewport(), QPoint(0, -17), QPoint(0, -51), Qt::ScrollUpdate);
        QCOMPARE(bar->value(), 17);
        send(view.viewport(), QPoint(0, -8), QPoint(0, -24), Qt::ScrollMomentum);
        QCOMPARE(bar->value(), 25);
    }
};

QTEST_MAIN(WheelInputTests)
#include "test_wheel_input.moc"
