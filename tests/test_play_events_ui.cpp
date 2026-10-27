#include <QTest>
#include <QApplication>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QUndoStack>

#include <cmath>

#include "audio/AudioCue.h"
#include "audio/AudioEditorModel.h"
#include "ui/AudioEditorWindow.h"
#include "ui/PlayEventsPanel.h"
#include "ui/TimelineCanvas.h"

using namespace quewi;
using audio::PlayEvent;

// The editing UI for vamps, repeats, holds, skips and markers: the timeline's
// VAMPS lane, the editor's Vamps tab, and the cue field they both write.
class PlayEventsUiTests : public QObject {
    Q_OBJECT

    static constexpr double kRate = 48000.0;
    static constexpr int x0 = ui::TimelineCanvas::kHeaderWidth;
    static int vampY()
    {
        return ui::TimelineCanvas::kRulerHeight + ui::TimelineCanvas::kMarkerLaneHeight
             + ui::TimelineCanvas::kVampLaneHeight / 2;
    }
    static int tracksTop()
    {
        return ui::TimelineCanvas::kRulerHeight + ui::TimelineCanvas::kMarkerLaneHeight
             + ui::TimelineCanvas::kVampLaneHeight;
    }

    // What the editor window does: lane gestures → the panel's commit; the
    // lane follows the cue.
    static void wire(ui::TimelineCanvas &canvas, ui::PlayEventsPanel &panel, audio::AudioCue &cue)
    {
        QObject::connect(&canvas, &ui::TimelineCanvas::playEventAddRequested, &panel,
                         [&panel](const QString &k, double s, double e) { panel.addFromLane(k, s, e); });
        QObject::connect(&canvas, &ui::TimelineCanvas::playEventMoved, &panel,
                         [&panel](QUuid id, double s, double e) { panel.moveEvent(id, s, e); });
        QObject::connect(&canvas, &ui::TimelineCanvas::playEventClicked, &panel, &ui::PlayEventsPanel::selectEvent);
        QObject::connect(&canvas, &ui::TimelineCanvas::playEventContextAction, &panel,
                         [&panel](QUuid id, const QString &a) { panel.applyEventAction(id, a); });
        QObject::connect(&panel, &ui::PlayEventsPanel::selectionChanged,
                         &canvas, &ui::TimelineCanvas::setSelectedPlayEvent);
        QObject::connect(&cue, &cues::Cue::changed, &canvas,
                         [&canvas, &cue] { canvas.setPlayEvents(cue.playEvents()); });
        canvas.setPlayEvents(cue.playEvents());
    }

    static void drag(QWidget *w, int fromX, int toX, int y, Qt::KeyboardModifiers mods = {})
    {
        QTest::mousePress(w, Qt::LeftButton, mods, QPoint(fromX, y));
        QTest::mouseMove(w, QPoint((fromX + toX) / 2, y));
        QTest::mouseMove(w, QPoint(toX, y));
        QTest::mouseRelease(w, Qt::LeftButton, mods, QPoint(toX, y));
    }

    static bool near(double a, double b) { return std::abs(a - b) < 1e-6; }

    static QPushButton *button(QWidget &root, const QString &text)
    {
        for (auto *b : root.findChildren<QPushButton *>())
            if (b->text() == text) return b;
        return nullptr;
    }

    // The cell widget of `objectName` on the row showing event `id`.
    template <typename T>
    static T *cell(ui::PlayEventsPanel &panel, const char *objectName, const QUuid &id)
    {
        // The table rebuilds deferred, and the old cells go by deleteLater:
        // let both happen, so only the live widgets are found. (A pointer
        // from an earlier call is stale after this.)
        QCoreApplication::processEvents();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        for (auto *w : panel.findChildren<T *>(QString::fromLatin1(objectName)))
            if (w->property("peId").toUuid() == id) return w;
        return nullptr;
    }

    struct Rig {
        audio::AudioEditorModel model;
        audio::AudioCue cue;
        QUndoStack undo;
        ui::TimelineCanvas canvas{&model};
        ui::PlayEventsPanel panel;
        Rig()
        {
            model.addTrack(QStringLiteral("T"));
            canvas.resize(900, 300);
            canvas.setFramesPerPixel(kRate / 100.0);   // 100 px per second
            canvas.setTriggers({}, kRate);
            panel.setCue(&cue);
            panel.setUndoStack(&undo);
            panel.setCountAsker([](int) { return std::optional<int>(4); });
            panel.setNameAsker([](const QString &, const QString &) { return std::optional<QString>(QStringLiteral("Chorus")); });
            wire(canvas, panel, cue);
        }
    };

private slots:
    void laneDragMakesAVampAndUndoes()
    {
        Rig r;
        r.canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&r.canvas));

        QSignalSpy lightAdded(&r.canvas, &ui::TimelineCanvas::triggerAdded);
        drag(&r.canvas, x0 + 200, x0 + 400, vampY());
        QCOMPARE(r.cue.playEvents().size(), size_t(1));
        const auto v = r.cue.playEvents()[0];
        QCOMPARE(v.kind, PlayEvent::Kind::Vamp);
        QCOMPARE(v.count, 0);
        QVERIFY(near(v.start, 2.0));
        QVERIFY(near(v.end, 4.0));
        QCOMPARE(lightAdded.count(), 0);              // the lighting lane wasn't touched
        QCOMPARE(r.canvas.playEvents().size(), size_t(1));
        QCOMPARE(r.panel.selectedEvent(), v.id);
        QCOMPARE(r.canvas.selectedPlayEvent(), v.id);
        QCOMPARE(r.undo.count(), 1);

        // A plain click on empty lane makes nothing and deselects.
        QTest::mouseClick(&r.canvas, Qt::LeftButton, {}, QPoint(x0 + 600, vampY()));
        QCOMPARE(r.cue.playEvents().size(), size_t(1));
        QVERIFY(r.canvas.selectedPlayEvent().isNull());
        QVERIFY(r.panel.selectedEvent().isNull());

        // An overlapping vamp is refused, with the reason.
        drag(&r.canvas, x0 + 500, x0 + 350, vampY());          // from empty lane, back over it
        QCOMPARE(r.cue.playEvents().size(), size_t(1));
        QVERIFY(r.panel.problem().contains(QStringLiteral("overlap")));

        r.undo.undo();
        QVERIFY(r.cue.playEvents().empty());
        QVERIFY(r.canvas.playEvents().empty());
    }

    void laneMovesAndResizes()
    {
        Rig r;
        r.canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&r.canvas));
        const QUuid id = r.panel.addEvent(QStringLiteral("vamp"), 2.0, 4.0);
        QVERIFY(!id.isNull());
        QCOMPARE(r.undo.count(), 1);

        drag(&r.canvas, x0 + 300, x0 + 350, vampY());          // body: move
        QVERIFY(near(r.cue.playEvents()[0].start, 2.5));
        QVERIFY(near(r.cue.playEvents()[0].end, 4.5));
        QCOMPARE(r.undo.count(), 2);

        drag(&r.canvas, x0 + 450, x0 + 520, vampY());          // end edge
        QVERIFY(near(r.cue.playEvents()[0].start, 2.5));
        QVERIFY(near(r.cue.playEvents()[0].end, 5.2));

        drag(&r.canvas, x0 + 250, x0 + 200, vampY());          // start edge
        QVERIFY(near(r.cue.playEvents()[0].start, 2.0));
        QVERIFY(near(r.cue.playEvents()[0].end, 5.2));
        QCOMPARE(r.undo.count(), 4);                            // one step each

        r.undo.undo();
        QVERIFY(near(r.cue.playEvents()[0].start, 2.5));
        r.undo.undo();
        QVERIFY(near(r.cue.playEvents()[0].end, 4.5));
        r.undo.undo();
        QVERIFY(near(r.cue.playEvents()[0].start, 2.0));
        QVERIFY(near(r.cue.playEvents()[0].end, 4.0));
        QCOMPARE(r.cue.playEvents()[0].id, id);

        // A hold moves as a point.
        r.panel.addEvent(QStringLiteral("hold"), 7.0);
        drag(&r.canvas, x0 + 700, x0 + 760, vampY());
        const auto &ev = r.cue.playEvents();
        QCOMPARE(ev.size(), size_t(2));
        QCOMPARE(ev[1].kind, PlayEvent::Kind::Hold);
        QVERIFY(near(ev[1].start, 7.6));
        QVERIFY(near(ev[1].end, 7.6));
    }

    void laneSnapsToTheBeatGrid()
    {
        Rig r;
        r.canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&r.canvas));
        audio::BeatGrid g;
        g.bpm = 120.0;                                // beats every 0.5 s, bars every 2 s
        r.cue.setField(QStringLiteral("beatGrid"), g.toJson());
        r.canvas.setBeatGrid(g);
        r.canvas.setSnapToBeats(true);

        drag(&r.canvas, x0 + 190, x0 + 412, vampY());
        QCOMPARE(r.cue.playEvents().size(), size_t(1));
        QVERIFY(near(r.cue.playEvents()[0].start, 2.0));
        QVERIFY(near(r.cue.playEvents()[0].end, 4.0));

        // Alt: where the mouse is.
        drag(&r.canvas, x0 + 513, x0 + 637, vampY(), Qt::AltModifier);
        QCOMPARE(r.cue.playEvents().size(), size_t(2));
        QVERIFY(std::abs(r.cue.playEvents()[1].start - 5.13) < 1e-6);

        // The bar and beat show beside the times.
        QCOMPARE(r.panel.barBeatText(2.0), QStringLiteral("bar 2 beat 1"));
        QCOMPARE(r.panel.barBeatText(2.75), QStringLiteral("bar 2 beat 2.5"));
        QCOMPARE(r.panel.defaultLength(), 2.0);
    }

    void deleteAndBackspaceRemoveTheSelection()
    {
        Rig r;
        r.canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&r.canvas));
        r.panel.addEvent(QStringLiteral("skip"), 2.0, 4.0);
        r.panel.addEvent(QStringLiteral("marker"), 6.0, -1.0, 0, QStringLiteral("Top of 2"));
        QCOMPARE(r.cue.playEvents().size(), size_t(2));

        QTest::mouseClick(&r.canvas, Qt::LeftButton, {}, QPoint(x0 + 300, vampY()));
        QCOMPARE(r.canvas.selectedPlayEvent(), r.cue.playEvents()[0].id);
        QTest::keyClick(&r.canvas, Qt::Key_Backspace);
        QCOMPARE(r.cue.playEvents().size(), size_t(1));
        QCOMPARE(r.cue.playEvents()[0].kind, PlayEvent::Kind::Marker);

        QTest::mouseClick(&r.canvas, Qt::LeftButton, {}, QPoint(x0 + 601, vampY()));
        QCOMPARE(r.canvas.selectedPlayEvent(), r.cue.playEvents()[0].id);
        QTest::keyClick(&r.canvas, Qt::Key_Delete);
        QVERIFY(r.cue.playEvents().empty());

        r.undo.undo();
        r.undo.undo();
        QCOMPARE(r.cue.playEvents().size(), size_t(2));
        QCOMPARE(r.cue.playEvents()[1].name, QStringLiteral("Top of 2"));

        // After a click in the lighting lane, Delete isn't the vamps'.
        r.canvas.setSelectedPlayEvent(r.cue.playEvents()[0].id);
        QTest::mouseClick(&r.canvas, Qt::LeftButton, Qt::AltModifier,
                          QPoint(x0 + 800, ui::TimelineCanvas::kRulerHeight + 5));
        QVERIFY(!r.canvas.vampLaneActive());
        QTest::keyClick(&r.canvas, Qt::Key_Delete);
        QCOMPARE(r.cue.playEvents().size(), size_t(2));
    }

    void otherLanesAndTracksStillHit()
    {
        Rig r;
        r.canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&r.canvas));
        QSignalSpy lightAdded(&r.canvas, &ui::TimelineCanvas::triggerAdded);
        QSignalSpy track(&r.canvas, &ui::TimelineCanvas::trackSelected);
        QSignalSpy region(&r.canvas, &ui::TimelineCanvas::regionSelected);

        // The lighting lane is where it was.
        QTest::mouseClick(&r.canvas, Qt::LeftButton, {},
                          QPoint(x0 + 200, ui::TimelineCanvas::kRulerHeight
                                               + ui::TimelineCanvas::kMarkerLaneHeight / 2));
        QCOMPARE(lightAdded.count(), 1);
        QVERIFY(r.cue.playEvents().empty());

        // The track header starts under the vamps lane…
        QTest::mouseClick(&r.canvas, Qt::LeftButton, {}, QPoint(40, tracksTop() + 10));
        QCOMPARE(track.count(), 1);
        QCOMPARE(track[0][0].toInt(), 0);
        // …and its body too: a click there is a (missed) region click, nothing more.
        QTest::mouseClick(&r.canvas, Qt::LeftButton, {}, QPoint(x0 + 300, tracksTop() + 10));
        QCOMPARE(region.count(), 1);
        QCOMPARE(lightAdded.count(), 1);
        QVERIFY(r.cue.playEvents().empty());

        // The vamps lane's header cell is just a label.
        QTest::mouseClick(&r.canvas, Qt::LeftButton, {}, QPoint(40, vampY()));
        QCOMPARE(track.count(), 1);
    }

    void contextActionsChangeKindAndExit()
    {
        Rig r;
        const QUuid id = r.panel.addEvent(QStringLiteral("vamp"), 2.0, 4.0);
        QVERIFY(r.panel.applyEventAction(id, QStringLiteral("exitNextBar")));
        QCOMPARE(r.cue.playEvents()[0].exit, PlayEvent::Exit::NextBar);

        QVERIFY(r.panel.applyEventAction(id, QStringLiteral("makeRepeat")));   // asker: 4
        QCOMPARE(r.cue.playEvents()[0].kindName(), QStringLiteral("Repeat ×4"));
        QCOMPARE(r.cue.playEvents()[0].exit, PlayEvent::Exit::NextBar);

        QVERIFY(r.panel.applyEventAction(id, QStringLiteral("makeSkip")));
        QCOMPARE(r.cue.playEvents()[0].kind, PlayEvent::Kind::Skip);
        QCOMPARE(r.cue.playEvents()[0].count, 0);

        QVERIFY(r.panel.applyEventAction(id, QStringLiteral("rename")));       // asker: Chorus
        QCOMPARE(r.cue.playEvents()[0].name, QStringLiteral("Chorus"));

        QVERIFY(r.panel.applyEventAction(id, QStringLiteral("makeHold")));
        QCOMPARE(r.cue.playEvents()[0].kind, PlayEvent::Kind::Hold);
        QVERIFY(near(r.cue.playEvents()[0].end, 2.0));
        QVERIFY(r.panel.applyEventAction(id, QStringLiteral("makeVamp")));     // gets a length back
        QVERIFY(near(r.cue.playEvents()[0].end, 4.0));

        // Each was one step: six undos is the bare vamp again.
        for (int i = 0; i < 6; ++i) r.undo.undo();
        const auto &v = r.cue.playEvents()[0];
        QCOMPARE(v.kind, PlayEvent::Kind::Vamp);
        QCOMPARE(v.count, 0);
        QCOMPARE(v.exit, PlayEvent::Exit::EndOfPass);
        QVERIFY(v.name.isEmpty());

        // Cancelled questions change nothing.
        r.panel.setCountAsker([](int) { return std::optional<int>(); });
        QVERIFY(!r.panel.applyEventAction(id, QStringLiteral("makeRepeat")));
        QCOMPARE(r.cue.playEvents()[0].count, 0);

        QVERIFY(r.panel.applyEventAction(id, QStringLiteral("delete")));
        QVERIFY(r.cue.playEvents().empty());
    }

    void panelButtonsAddAtTheCursor()
    {
        Rig r;
        r.panel.setCursorSeconds(3.0);
        button(r.panel, QStringLiteral("+ Vamp"))->click();
        QCOMPARE(r.cue.playEvents().size(), size_t(1));
        QVERIFY(near(r.cue.playEvents()[0].start, 3.0));
        QVERIFY(near(r.cue.playEvents()[0].end, 5.0));           // no grid: 2 s

        // With a grid, a bar long, from the nearest beat when snapping.
        audio::BeatGrid g;
        g.bpm = 100.0;                                           // bar = 2.4 s
        r.cue.setField(QStringLiteral("beatGrid"), g.toJson());
        r.panel.setSnapToBeats(true);
        r.panel.setCursorSeconds(10.1);                          // nearest beat 10.2
        button(r.panel, QStringLiteral("+ Repeat"))->click();    // asker: 4
        const auto rep = r.cue.playEvents()[1];
        QVERIFY(rep.isRepeat());
        QCOMPARE(rep.count, 4);
        QVERIFY(near(rep.start, 10.2));
        QVERIFY(near(rep.end, 12.6));

        r.panel.setSnapToBeats(false);
        r.panel.setCursorSeconds(20.0);
        button(r.panel, QStringLiteral("+ Hold"))->click();
        r.panel.setCursorSeconds(21.0);
        button(r.panel, QStringLiteral("+ Marker"))->click();    // asker: Chorus
        r.panel.setCursorSeconds(22.0);
        button(r.panel, QStringLiteral("+ Skip"))->click();
        const auto &ev = r.cue.playEvents();
        QCOMPARE(ev.size(), size_t(5));
        QCOMPARE(ev[2].kind, PlayEvent::Kind::Hold);
        QVERIFY(near(ev[2].start, 20.0) && near(ev[2].end, 20.0));
        QCOMPARE(ev[3].kind, PlayEvent::Kind::Marker);
        QCOMPARE(ev[3].name, QStringLiteral("Chorus"));
        QCOMPARE(ev[4].kind, PlayEvent::Kind::Skip);
        QVERIFY(near(ev[4].end, 24.4));

        // The playhead wins while previewing; a span wins over both.
        r.panel.setPlayheadProvider([] { return 30.0; });
        button(r.panel, QStringLiteral("+ Vamp"))->click();
        QVERIFY(near(r.cue.playEvents()[5].start, 30.0));
        r.panel.setSpanProvider([](double &a, double &b) { a = 40.0; b = 47.5; return true; });
        button(r.panel, QStringLiteral("+ Vamp"))->click();
        QVERIFY(near(r.cue.playEvents()[6].start, 40.0));
        QVERIFY(near(r.cue.playEvents()[6].end, 47.5));

        QCOMPARE(r.undo.count(), 7);
        QCOMPARE(r.panel.selectedEvent(), r.cue.playEvents()[6].id);
        button(r.panel, QStringLiteral("Delete"))->click();
        QCOMPARE(r.cue.playEvents().size(), size_t(6));
    }

    void tableEditsInPlace()
    {
        Rig r;
        const QUuid rep = r.panel.addEvent(QStringLiteral("repeat"), 2.0, 4.0, 2);
        const QUuid vamp = r.panel.addEvent(QStringLiteral("vamp"), 6.0, 8.0);
        QCOMPARE(r.undo.count(), 2);

        QVERIFY(!cell<QSpinBox>(r.panel, "peCount", vamp));       // a vamp has no count
        auto *count = cell<QSpinBox>(r.panel, "peCount", rep);
        QVERIFY(count);
        QCOMPARE(count->value(), 2);
        count->setValue(5);
        QCOMPARE(r.cue.playEvents()[0].count, 5);

        QVERIFY(!cell<QComboBox>(r.panel, "peExit", rep));       // a repeat doesn't wait
        auto *exit = cell<QComboBox>(r.panel, "peExit", vamp);
        QVERIFY(exit);
        exit->setCurrentIndex(exit->findData(int(PlayEvent::Exit::Now)));
        emit exit->activated(exit->currentIndex());
        QCOMPARE(r.cue.playEvents()[1].exit, PlayEvent::Exit::Now);

        auto *name = cell<QLineEdit>(r.panel, "peName", vamp);
        QVERIFY(name);
        name->setText(QStringLiteral("Underscore"));
        emit name->editingFinished();
        QCOMPARE(r.cue.playEvents()[1].name, QStringLiteral("Underscore"));

        auto *start = cell<QDoubleSpinBox>(r.panel, "peStart", vamp);
        QVERIFY(start);
        QCOMPARE(start->text(), QStringLiteral("0:06.000"));
        start->setValue(6.5);
        QVERIFY(near(r.cue.playEvents()[1].start, 6.5));
        QVERIFY(near(r.cue.playEvents()[1].end, 8.0));

        auto *kind = cell<QComboBox>(r.panel, "peKind", vamp);
        QVERIFY(kind);
        kind->setCurrentIndex(kind->findData(QStringLiteral("hold")));
        emit kind->activated(kind->currentIndex());
        QCOMPARE(r.cue.playEvents()[1].kind, PlayEvent::Kind::Hold);
        QVERIFY(near(r.cue.playEvents()[1].end, 6.5));

        // Five edits, five steps; undo walks them back and the table follows.
        QCOMPARE(r.undo.count(), 7);
        for (int i = 0; i < 5; ++i) r.undo.undo();
        QCOMPARE(r.cue.playEvents()[0].count, 2);
        QCOMPARE(r.cue.playEvents()[1].exit, PlayEvent::Exit::EndOfPass);
        QVERIFY(r.cue.playEvents()[1].name.isEmpty());
        QVERIFY(near(r.cue.playEvents()[1].start, 6.0));
        QCOMPARE(cell<QSpinBox>(r.panel, "peCount", rep)->value(), 2);
        QCOMPARE(cell<QComboBox>(r.panel, "peExit", vamp)->currentData().toInt(),
                 int(PlayEvent::Exit::EndOfPass));

        // Double-clicking a row's first cell asks to go there.
        QSignalSpy seek(&r.panel, &ui::PlayEventsPanel::seekRequested);
        auto *table = r.panel.findChild<QTableWidget *>(QStringLiteral("peTable"));
        QVERIFY(table);
        emit table->cellDoubleClicked(1, 0);
        QCOMPARE(seek.count(), 1);
        QVERIFY(near(seek[0][0].toDouble(), 6.0));
    }

    void editorWindowHasTheLaneAndTab()
    {
        audio::AudioCue cue;
        auto *win = new ui::AudioEditorWindow(&cue);
        win->setAttribute(Qt::WA_DeleteOnClose, false);
        win->resize(1280, 720);
        win->show();
        QVERIFY(QTest::qWaitForWindowActive(win));
        auto *panel = win->findChild<ui::PlayEventsPanel *>();
        auto *canvas = win->findChild<ui::TimelineCanvas *>();
        auto *tabs = win->findChild<QTabWidget *>();
        QVERIFY(panel && canvas && tabs);
        QCOMPARE(tabs->tabText(tabs->indexOf(panel)), QStringLiteral("Vamps"));
        canvas->setFramesPerPixel(kRate / 100.0);

        drag(canvas, x0 + 100, x0 + 300, vampY());
        QCOMPARE(cue.playEvents().size(), size_t(1));
        QVERIFY(std::abs(cue.playEvents()[0].length() - 2.0) < 0.05);
        QCOMPARE(tabs->currentWidget(), panel);                  // the tab came forward
        QCOMPARE(canvas->playEvents().size(), size_t(1));        // the lane follows the cue

        // Delete goes to the vamps lane after a click there (the window's
        // shortcut), not to the lighting triggers.
        canvas->setFocus();
        QTest::mouseClick(canvas, Qt::LeftButton, {}, QPoint(x0 + 200, vampY()));
        QTest::keyClick(canvas, Qt::Key_Delete);
        QVERIFY(cue.playEvents().empty());
        QVERIFY(canvas->playEvents().empty());

        // A field set from outside (an undo, a remote) shows in the lane.
        PlayEvent h;
        h.kind = PlayEvent::Kind::Hold;
        h.start = 1.0;
        h.end = 1.0;
        cue.setField(QStringLiteral("playEvents"), audio::playEventsToJson({h}));
        QCOMPARE(canvas->playEvents().size(), size_t(1));
        win->close();
        delete win;
    }
};

QTEST_MAIN(PlayEventsUiTests)
#include "test_play_events_ui.moc"
