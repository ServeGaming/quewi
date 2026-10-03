#include <QTest>
#include <QApplication>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QUndoStack>

#include "audio/AudioCue.h"
#include "audio/AudioEditorModel.h"
#include "ui/Inspector.h"
#include "ui/LightTriggersPanel.h"
#include "ui/TimelineCanvas.h"
#include "video/VideoCue.h"

using namespace quewi;

// The lighting-trigger UI: the audio editor's Lighting tab, the timeline's
// marker lane and the Inspector's "Lighting triggers…" buttons.
class LightTriggersUiTests : public QObject {
    Q_OBJECT

    template <typename T>
    static T *named(QWidget &root, const char *name)
    {
        return root.findChild<T *>(QString::fromLatin1(name));
    }

    static QPushButton *button(QWidget &root, const QString &text)
    {
        for (auto *b : root.findChildren<QPushButton *>())
            if (b->text() == text) return b;
        return nullptr;
    }

private slots:
    void addsPointAtCursorAndRange()
    {
        audio::AudioCue cue;
        QUndoStack undo;
        ui::LightTriggersPanel panel;
        panel.setCue(&cue);
        panel.setUndoStack(&undo);
        panel.setCursorSeconds(12.5);

        QSignalSpy edited(&panel, &ui::LightTriggersPanel::triggersEdited);
        auto *point = button(panel, QStringLiteral("+ Point at cursor"));
        auto *range = button(panel, QStringLiteral("+ Range"));
        QVERIFY(point && range);

        point->click();
        QCOMPARE(cue.lightTriggers().size(), size_t(1));
        QCOMPARE(cue.lightTriggers()[0].start, 12.5);
        QVERIFY(!cue.lightTriggers()[0].isRange());

        range->click();
        QCOMPARE(cue.lightTriggers().size(), size_t(2));
        const auto &r = cue.lightTriggers()[1];
        QCOMPARE(r.start, 12.5);
        QCOMPARE(r.end, 16.5);
        QCOMPARE(panel.selectedTrigger(), r.id);
        QCOMPARE(edited.count(), 2);
        QCOMPARE(undo.count(), 2);   // structural edits don't merge
    }

    void editsCommitAndUndoRestores()
    {
        audio::AudioCue cue;
        QUndoStack undo;
        ui::LightTriggersPanel panel;
        panel.setCue(&cue);
        panel.setUndoStack(&undo);
        const QUuid id = panel.addTrigger(3.0, 5.0);
        QCOMPARE(panel.selectedTrigger(), id);
        const QString before = cue.lightTriggers()[0].name;

        auto *name = named<QLineEdit>(panel, "ltName");
        auto *start = named<QDoubleSpinBox>(panel, "ltStart");
        QVERIFY(name && start);
        QCOMPARE(name->text(), before);

        name->setText(QStringLiteral("Chorus wash"));
        emit name->editingFinished();
        QCOMPARE(cue.lightTriggers()[0].name, QStringLiteral("Chorus wash"));

        start->setValue(4.0);                 // a range keeps its length
        QCOMPARE(cue.lightTriggers()[0].start, 4.0);
        QCOMPARE(cue.lightTriggers()[0].end, 6.0);

        undo.undo();                          // name + start merged into one step
        QCOMPARE(cue.lightTriggers()[0].name, before);
        QCOMPARE(cue.lightTriggers()[0].start, 3.0);
        QCOMPARE(name->text(), before);       // the panel followed the undo
        QCOMPARE(start->value(), 3.0);
        QCOMPARE(panel.selectedTrigger(), id);

        undo.undo();                          // the add
        QVERIFY(cue.lightTriggers().empty());
        QVERIFY(panel.selectedTrigger().isNull());
        undo.redo();
        QCOMPARE(cue.lightTriggers().size(), size_t(1));

        // Without an undo stack edits go straight to the cue.
        panel.setUndoStack(nullptr);
        panel.applyTriggerAction(id, QStringLiteral("toggleEnabled"));
        QVERIFY(!cue.lightTriggers()[0].enabled);
        panel.applyTriggerAction(id, QStringLiteral("toggleRange"));
        QVERIFY(!cue.lightTriggers()[0].isRange());
        panel.applyTriggerAction(id, QStringLiteral("delete"));
        QVERIFY(cue.lightTriggers().empty());
    }

    void presetFillsOscFields()
    {
        audio::AudioCue cue;
        ui::LightTriggersPanel panel;
        panel.setCue(&cue);
        panel.addTrigger(1.0);

        auto editors = panel.findChildren<ui::TriggerActionEditor *>();
        QCOMPARE(editors.size(), 2);
        auto *enter = editors.first();
        const QStringList presets = ui::TriggerActionEditor::presetNames();
        int go = -1;
        for (int i = 0; i < presets.size(); ++i)
            if (presets[i].startsWith(QStringLiteral("ETC Eos")) && presets[i].endsWith(QStringLiteral("GO")))
                go = i;
        QVERIFY(go >= 0);
        enter->applyPreset(go);

        const auto &a = cue.lightTriggers()[0].enter;
        QCOMPARE(a.kind, audio::TriggerAction::Kind::Osc);
        QCOMPARE(a.address, QStringLiteral("/eos/key/go_0"));
        QCOMPARE(a.port, 8000);
        auto *addr = named<QLineEdit>(*enter, "ltOscAddress");
        QVERIFY(addr);
        QCOMPARE(addr->text(), QStringLiteral("/eos/key/go_0"));

        // Test sends the action as shown.
        QSignalSpy test(&panel, &ui::LightTriggersPanel::testRequested);
        auto *testBtn = button(*enter, QStringLiteral("Test"));
        QVERIFY(testBtn);
        testBtn->click();
        QCOMPARE(test.count(), 1);
        QCOMPARE(test.first().first().value<audio::TriggerAction>().address,
                 QStringLiteral("/eos/key/go_0"));
    }

    void laneClickAddsPointAndDragAddsRange()
    {
        audio::AudioEditorModel model;
        model.addTrack(QStringLiteral("T"));
        ui::TimelineCanvas canvas(&model);
        canvas.resize(900, 300);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));
        const double rate = 48000.0;
        canvas.setFramesPerPixel(rate / 100.0);          // 100 px per second
        canvas.setTriggers({}, rate);

        QSignalSpy added(&canvas, &ui::TimelineCanvas::triggerAdded);
        const int laneY = ui::TimelineCanvas::kRulerHeight + ui::TimelineCanvas::kMarkerLaneHeight / 2;
        const int x0 = ui::TimelineCanvas::kHeaderWidth;

        QTest::mouseClick(&canvas, Qt::LeftButton, {}, QPoint(x0 + 200, laneY));
        QCOMPARE(added.count(), 1);
        QCOMPARE(added[0][0].toDouble(), 2.0);
        QVERIFY(added[0][1].toDouble() < 0.0);

        QTest::mousePress(&canvas, Qt::LeftButton, {}, QPoint(x0 + 400, laneY));
        QTest::mouseMove(&canvas, QPoint(x0 + 500, laneY));
        QTest::mouseMove(&canvas, QPoint(x0 + 650, laneY));
        QTest::mouseRelease(&canvas, Qt::LeftButton, {}, QPoint(x0 + 650, laneY));
        QCOMPARE(added.count(), 2);
        QCOMPARE(added[1][0].toDouble(), 4.0);
        QCOMPARE(added[1][1].toDouble(), 6.5);

        // Dragging an existing point moves it (reported on release).
        audio::LightTrigger t;
        t.start = 1.0;
        canvas.setTriggers({t}, rate);
        QSignalSpy moved(&canvas, &ui::TimelineCanvas::triggerMoved);
        QSignalSpy selected(&canvas, &ui::TimelineCanvas::triggerSelected);
        QTest::mousePress(&canvas, Qt::LeftButton, {}, QPoint(x0 + 100, laneY));
        QCOMPARE(selected.count(), 1);
        QTest::mouseMove(&canvas, QPoint(x0 + 120, laneY));
        QTest::mouseMove(&canvas, QPoint(x0 + 150, laneY));
        QTest::mouseRelease(&canvas, Qt::LeftButton, {}, QPoint(x0 + 150, laneY));
        QCOMPARE(moved.count(), 1);
        QCOMPARE(moved[0][0].toUuid(), t.id);
        QCOMPARE(moved[0][1].toDouble(), 1.5);
        QCOMPARE(added.count(), 2);                       // no new trigger

        // A click in the tracks below doesn't touch triggers.
        QTest::mouseClick(&canvas, Qt::LeftButton, {},
                          QPoint(x0 + 300, ui::TimelineCanvas::kRulerHeight
                                           + ui::TimelineCanvas::kMarkerLaneHeight + 30));
        QCOMPARE(added.count(), 2);
    }

    void inspectorOpensLightTriggers()
    {
        ui::Inspector inspector;
        QSignalSpy spy(&inspector, &ui::Inspector::editLightTriggersRequested);
        const QString text = QString::fromUtf8("Lighting triggers\xE2\x80\xA6");
        auto clickAll = [&] {
            for (auto *b : inspector.findChildren<QPushButton *>())
                if (b->text() == text) b->click();
        };

        audio::AudioCue cue;
        inspector.setCue(&cue);
        clickAll();
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy[0][0].value<audio::AudioCue *>(), &cue);

        // The count follows the cue.
        auto countLabel = [&]() -> QString {
            for (auto *l : inspector.findChildren<QLabel *>())
                if (l->text() == QStringLiteral("none") || l->text().endsWith(QStringLiteral("trigger"))
                    || l->text().endsWith(QStringLiteral("triggers")))
                    return l->text();
            return {};
        };
        QCOMPARE(countLabel(), QStringLiteral("none"));
        audio::LightTrigger t;
        cue.setField(QStringLiteral("lightTriggers"), audio::triggersToJson({t}));
        QCOMPARE(countLabel(), QStringLiteral("1 trigger"));

        video::VideoCue vc;
        inspector.setCue(&vc);
        clickAll();
        QCOMPARE(spy.count(), 2);
        QCOMPARE(spy[1][0].value<audio::AudioCue *>(), vc.sound());
        inspector.setCue(nullptr);
    }
};

QTEST_MAIN(LightTriggersUiTests)
#include "test_light_triggers_ui.moc"
