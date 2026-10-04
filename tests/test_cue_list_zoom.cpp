#include <QTest>
#include <QApplication>
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
    static void wheel(ui::CueListView *view, int delta, Qt::KeyboardModifiers mods)
    {
        QWidget *w = view->viewport();
        const QPointF pos(w->width() / 2.0, w->height() / 2.0);
        QWheelEvent e(pos, w->mapToGlobal(pos), QPoint(), QPoint(0, delta),
                      Qt::NoButton, mods, Qt::NoScrollPhase, false);
        QApplication::sendEvent(w, &e);
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
};

QTEST_MAIN(CueListZoomTests)
#include "test_cue_list_zoom.moc"
