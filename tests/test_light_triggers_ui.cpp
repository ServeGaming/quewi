#include <QTest>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QSpinBox>
#include <QUndoStack>

#include "audio/AudioCue.h"
#include "audio/AudioEditorModel.h"
#include "core/LightingDesk.h"
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

    // QSettings here is the user's real quewi settings: the desk tests change
    // lighting/desk/*, so it's saved first and put back afterwards.
    QVariantMap m_savedDesk;

    static void setDesk(core::LightingDesk::Type type)
    {
        core::LightingDesk d;
        d.type = type;
        d.save();
    }

    static ui::TriggerActionEditor *enterEditor(QWidget &panel)
    {
        const auto editors = panel.findChildren<ui::TriggerActionEditor *>();
        return editors.isEmpty() ? nullptr : editors.first();
    }

    static QList<int> comboData(QComboBox *c)
    {
        QList<int> out;
        for (int i = 0; i < c->count(); ++i) out << c->itemData(i).toInt();
        return out;
    }

private slots:
    void initTestCase()
    {
        QSettings s(QStringLiteral("ServeGaming"), QStringLiteral("quewi"));
        s.beginGroup(QStringLiteral("lighting/desk"));
        for (const auto &k : s.childKeys()) m_savedDesk.insert(k, s.value(k));
        s.endGroup();
        setDesk(core::LightingDesk::Type::Eos);   // the tests below assume Eos
    }

    void cleanupTestCase()
    {
        QSettings s(QStringLiteral("ServeGaming"), QStringLiteral("quewi"));
        s.beginGroup(QStringLiteral("lighting/desk"));
        s.remove(QString());                      // everything in the group
        for (auto it = m_savedDesk.cbegin(); it != m_savedDesk.cend(); ++it)
            s.setValue(it.key(), it.value());
        s.endGroup();
        s.sync();
    }

    void newTriggersDefaultToDeskGo()
    {
        audio::AudioCue cue;
        ui::LightTriggersPanel panel;
        panel.setCue(&cue);
        panel.addTrigger(1.0);
        panel.addTrigger(2.0, 4.0);
        const auto &p = cue.lightTriggers()[0];
        QCOMPARE(p.enter.kind, audio::TriggerAction::Kind::Desk);
        QCOMPARE(p.enter.deskDo, audio::TriggerAction::DeskDo::Go);
        const auto &r = cue.lightTriggers()[1];
        QCOMPARE(r.enter.kind, audio::TriggerAction::Kind::Desk);
        QCOMPARE(r.enter.deskDo, audio::TriggerAction::DeskDo::Go);
        QCOMPARE(r.exit.kind, audio::TriggerAction::Kind::None);

        // The editor shows it in simple mode with a plain preview.
        auto *enter = enterEditor(panel);
        QVERIFY(enter && !enter->isCustom());
        auto *preview = named<QLabel>(*enter, "ltPreview");
        QVERIFY(preview);
        QCOMPARE(preview->text(), QStringLiteral("Desk: GO"));
    }

    void simpleModeBumpSubCommitsAndUndoes()
    {
        audio::AudioCue cue;
        QUndoStack undo;
        ui::LightTriggersPanel panel;
        panel.setCue(&cue);
        panel.setUndoStack(&undo);
        panel.addTrigger(1.0);

        auto *enter = enterEditor(panel);
        QVERIFY(enter);
        auto *what = named<QComboBox>(*enter, "ltDo");
        auto *number = named<QLineEdit>(*enter, "ltNumber");
        auto *hold = named<QDoubleSpinBox>(*enter, "ltHold");
        QVERIFY(what && number && hold);

        const int bump = what->findText(QStringLiteral("Bump sub"));
        QVERIFY(bump >= 0);
        what->setCurrentIndex(bump);
        number->setText(QStringLiteral("3"));
        emit number->editingFinished();
        hold->setValue(0.4);

        const auto &a = cue.lightTriggers()[0].enter;
        QCOMPARE(a.kind, audio::TriggerAction::Kind::Desk);
        QCOMPARE(a.deskDo, audio::TriggerAction::DeskDo::SubBump);
        QCOMPARE(a.number, QStringLiteral("3"));
        QCOMPARE(a.hold, 0.4);        QCOMPARE(named<QLabel>(*enter, "ltPreview")->text(), QStringLiteral("Desk: bump sub 3"));

        undo.undo();                          // the field edits are one step
        QCOMPARE(cue.lightTriggers().size(), size_t(1));
        QCOMPARE(cue.lightTriggers()[0].enter.deskDo, audio::TriggerAction::DeskDo::Go);
        QCOMPARE(enter->action().deskDo, audio::TriggerAction::DeskDo::Go);
        QCOMPARE(what->currentText(), QStringLiteral("GO (next cue)"));
    }

    void customToggleShowsTheRealCommand()
    {
        audio::AudioCue cue;
        QUndoStack undo;
        ui::LightTriggersPanel panel;
        panel.setCue(&cue);
        panel.setUndoStack(&undo);
        panel.addTrigger(1.0);                // Desk: GO

        auto *enter = enterEditor(panel);
        auto *custom = named<QCheckBox>(*enter, "ltCustom");
        QVERIFY(enter && custom);
        QVERIFY(!custom->isChecked());
        custom->setChecked(true);

        const auto &a = cue.lightTriggers()[0].enter;
        QCOMPARE(a.kind, audio::TriggerAction::Kind::Osc);
        QCOMPARE(a.address, QStringLiteral("/eos/key/go_0"));
        QCOMPARE(a.args, QStringLiteral("1"));
        QVERIFY(a.host.isEmpty());
        QCOMPARE(a.port, 0);
        QVERIFY(enter->isCustom());
        QCOMPARE(named<QLineEdit>(*enter, "ltOscHost")->placeholderText(),
                 QStringLiteral("lighting desk (Preferences)"));

        // Off again: simple mode, "Nothing".
        custom->setChecked(false);
        QCOMPARE(cue.lightTriggers()[0].enter.kind, audio::TriggerAction::Kind::None);
        QVERIFY(!enter->isCustom());
        QCOMPARE(named<QComboBox>(*enter, "ltDo")->currentText(), QStringLiteral("Nothing"));

        undo.undo();
        QCOMPARE(cue.lightTriggers()[0].enter.kind, audio::TriggerAction::Kind::Desk);
        QVERIFY(!enter->isCustom());

        // An existing OSC action opens in custom mode.
        audio::TriggerAction osc;
        osc.kind = audio::TriggerAction::Kind::Osc;
        osc.address = QStringLiteral("/x");
        enter->setAction(osc);
        QVERIFY(custom->isChecked());
    }

    void comboListsOnlyWhatTheDeskCanDo()
    {
        using Do = audio::TriggerAction::DeskDo;
        setDesk(core::LightingDesk::Type::Ma3);
        {
            audio::AudioCue cue;
            ui::LightTriggersPanel panel;
            panel.setCue(&cue);
            panel.addTrigger(1.0);
            auto *enter = enterEditor(panel);
            auto *what = named<QComboBox>(*enter, "ltDo");
            QVERIFY(what);
            const QList<int> ma3 = {-1, int(Do::Go), int(Do::Back), int(Do::GoToCue),
                                    int(Do::Command), 1000};
            QCOMPARE(comboData(what), ma3);
            QCOMPARE(what->itemText(what->count() - 1), QStringLiteral("Fire a quewi cue"));
            QVERIFY(named<QLabel>(panel, "ltDesk")->text().startsWith(QStringLiteral("Lighting desk: ")));

            // Go to cue on MA3 asks for a Sequence.
            what->setCurrentIndex(what->findData(int(Do::GoToCue)));
            auto *list = named<QSpinBox>(*enter, "ltList");
            QCOMPARE(qobject_cast<QLabel *>(qobject_cast<QFormLayout *>(list->parentWidget()->layout())
                                                ->labelForField(list))->text(),
                     QStringLiteral("Sequence"));

            // An action this desk can't do is kept and marked, not changed.
            audio::TriggerAction bump;
            bump.kind = audio::TriggerAction::Kind::Desk;
            bump.deskDo = Do::SubBump;
            bump.number = QStringLiteral("2");
            enter->setAction(bump);
            QCOMPARE(what->currentData().toInt(), int(Do::SubBump));
            QVERIFY(what->currentText().endsWith(QStringLiteral("(not on this desk)")));
            QCOMPARE(enter->action().deskDo, Do::SubBump);
        }
        setDesk(core::LightingDesk::Type::Eos);
        {
            audio::AudioCue cue;
            ui::LightTriggersPanel panel;
            panel.setCue(&cue);
            panel.addTrigger(1.0);
            auto *what = named<QComboBox>(*enterEditor(panel), "ltDo");
            QCOMPARE(what->count(), 12);      // Nothing + all ten + Fire a quewi cue
        }
    }

    void changeDeskButtonAsksForSettings()
    {
        ui::LightTriggersPanel panel;
        QSignalSpy spy(&panel, &ui::LightTriggersPanel::deskSettingsRequested);
        auto *change = button(panel, QString::fromUtf8("Change\xE2\x80\xA6"));
        QVERIFY(change);
        change->click();
        QCOMPARE(spy.count(), 1);
    }

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
        QCOMPARE(a.port, 0);                  // 0 + blank host = the desk in Preferences
        QVERIFY(a.host.isEmpty());
        QVERIFY(enter->isCustom());           // a preset is custom OSC
        auto *addr = named<QLineEdit>(*enter, "ltOscAddress");
        QVERIFY(addr);
        QCOMPARE(addr->text(), QStringLiteral("/eos/key/go_0"));
        auto *port = named<QSpinBox>(*enter, "ltOscPort");
        QVERIFY(port);
        QCOMPARE(port->text(), QStringLiteral("desk port"));

        // Eos Stop and Back are two presets now.
        QVERIFY(presets.contains(QStringLiteral("ETC Eos — Stop")));
        QVERIFY(presets.contains(QStringLiteral("ETC Eos — Back")));

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
