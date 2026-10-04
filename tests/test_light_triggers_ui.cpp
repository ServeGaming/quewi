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
#include <QTableWidget>
#include <QDialogButtonBox>
#include <QRandomGenerator>
#include <QUndoStack>

#include <cmath>

#include "audio/AudioCue.h"
#include "audio/AudioEditorModel.h"
#include "core/LightingDesk.h"
#include "ui/AudioEditorWindow.h"
#include "ui/FillBeatsDialog.h"
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
    QVariant    m_savedSnap;                      // triggers/snapToBeats

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
        m_savedSnap = s.value(QStringLiteral("triggers/snapToBeats"));
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
        if (m_savedSnap.isValid()) s.setValue(QStringLiteral("triggers/snapToBeats"), m_savedSnap);
        else                       s.remove(QStringLiteral("triggers/snapToBeats"));
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
            const QList<int> ma3 = {-1, int(Do::Go), int(Do::GoList), int(Do::Back), int(Do::GoToCue),
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
            QCOMPARE(what->count(), 13);      // Nothing + all eleven + Fire a quewi cue
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
        auto *point = button(panel, QStringLiteral("+ Point here"));
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

    // While the song plays, "+ Point here" goes where you're listening — the
    // edit cursor stays at the playback start, so every point used to land
    // at 0.000.
    void pointHereFollowsThePlayhead()
    {
        audio::AudioCue cue;
        ui::LightTriggersPanel panel;
        panel.setCue(&cue);
        panel.setCursorSeconds(0.0);
        double playhead = 5.25;
        panel.setPlayheadProvider([&playhead] { return playhead; });
        button(panel, QStringLiteral("+ Point here"))->click();
        QCOMPARE(cue.lightTriggers().back().start, 5.25);
        playhead = 9.5;
        panel.addPointHere();
        QCOMPARE(cue.lightTriggers().back().start, 9.5);
        playhead = -1.0;                       // stopped: back to the cursor
        panel.setCursorSeconds(2.0);
        panel.addPointHere();
        QCOMPARE(cue.lightTriggers().front().start, 2.0);
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
        QSignalSpy selected(&canvas, &ui::TimelineCanvas::triggerClicked);
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

    // ── Beat grid ────────────────────────────────────────────────────────

    void gridStripCommitsAndUndoes()
    {
        audio::AudioCue cue;
        QUndoStack undo;
        ui::LightTriggersPanel panel;
        panel.setCue(&cue);
        panel.setUndoStack(&undo);
        auto *bpm = named<QDoubleSpinBox>(panel, "ltBpm");
        auto *first = named<QDoubleSpinBox>(panel, "ltFirstBeat");
        auto *bpb = named<QSpinBox>(panel, "ltBeatsPerBar");
        auto *setFirst = named<QPushButton>(panel, "ltSetFirstBeat");
        QVERIFY(bpm && first && bpb && setFirst);
        QCOMPARE(bpm->text(), QStringLiteral("Off"));

        bpm->setValue(120.0);
        first->setValue(0.5);
        bpb->setValue(3);
        QCOMPARE(cue.beatGrid().bpm, 120.0);
        QCOMPARE(cue.beatGrid().firstBeat, 0.5);
        QCOMPARE(cue.beatGrid().beatsPerBar, 3);
        QCOMPARE(undo.count(), 1);                // spin edits merge

        panel.setCursorSeconds(1.25);
        setFirst->click();                        // its own step
        QCOMPARE(cue.beatGrid().firstBeat, 1.25);
        QCOMPARE(first->value(), 1.25);
        QCOMPARE(undo.count(), 2);

        undo.undo();
        QCOMPARE(cue.beatGrid().firstBeat, 0.5);
        QCOMPARE(first->value(), 0.5);            // the strip followed
        undo.undo();
        QVERIFY(!cue.beatGrid().isSet());
        QCOMPARE(bpm->value(), 0.0);
        QCOMPARE(bpb->value(), 4);
    }

    void tapSetsTempoAndFirstBeat()
    {
        audio::AudioCue cue;
        QUndoStack undo;
        ui::LightTriggersPanel panel;
        panel.setCue(&cue);
        panel.setUndoStack(&undo);

        panel.tapAt(10.0);
        QVERIFY(!cue.beatGrid().isSet());         // one tap isn't a tempo
        panel.tapAt(10.5);
        QCOMPARE(cue.beatGrid().bpm, 120.0);
        panel.tapAt(11.0);
        panel.tapAt(11.5);
        QCOMPARE(cue.beatGrid().bpm, 120.0);
        QCOMPARE(cue.beatGrid().firstBeat, 0.0);  // not playing: first beat untouched
        QCOMPARE(undo.count(), 1);                // one tap run, one step
        QCOMPARE(named<QDoubleSpinBox>(panel, "ltBpm")->value(), 120.0);

        // A new run (after a pause) while the preview plays: 100 BPM, and the
        // first beat lands on the tap, wound back to the earliest beat >= 0.
        panel.tapAt(20.0, 6.9);
        panel.tapAt(20.6, 7.5);
        QCOMPARE(cue.beatGrid().bpm, 100.0);
        QVERIFY(std::abs(cue.beatGrid().firstBeat - 0.3) < 1e-9);   // 7.5 - 12 × 0.6
        QCOMPARE(undo.count(), 2);
        undo.undo();
        QCOMPARE(cue.beatGrid().bpm, 120.0);

        // The button taps too (real clock).
        auto *tapBtn = named<QPushButton>(panel, "ltTap");
        QVERIFY(tapBtn);
        tapBtn->click();
    }

    void fillWithBeatsAddsOneUndoStep()
    {
        audio::AudioCue cue;
        QUndoStack undo;
        ui::LightTriggersPanel panel;
        panel.setCue(&cue);
        panel.setUndoStack(&undo);
        audio::BeatGrid g;
        g.bpm = 120.0;
        panel.setBeatGrid(g);
        panel.addTrigger(0.25);                   // something already there
        const QUuid kept = panel.selectedTrigger();
        const int before = undo.count();

        audio::TriggerAction bump;
        bump.kind = audio::TriggerAction::Kind::Desk;
        bump.deskDo = audio::TriggerAction::DeskDo::SubBump;
        bump.number = QStringLiteral("3");
        bump.hold = 0.2;
        QCOMPARE(panel.fillWithBeats(2.0, 4.0, 1, bump, QStringLiteral("Beat")), 4);
        const auto &t = cue.lightTriggers();
        QCOMPARE(t.size(), size_t(5));
        const double want[] = {2.0, 2.5, 3.0, 3.5};
        for (int i = 0; i < 4; ++i) {
            QCOMPARE(t[size_t(i + 1)].start, want[i]);
            QVERIFY(!t[size_t(i + 1)].isRange());
            QCOMPARE(t[size_t(i + 1)].enter.deskDo, audio::TriggerAction::DeskDo::SubBump);
            QCOMPARE(t[size_t(i + 1)].enter.number, QStringLiteral("3"));
        }
        QCOMPARE(t[1].name, QStringLiteral("Beat 1"));
        QCOMPARE(undo.count(), before + 1);
        // The new row is one group, selected, ready to edit together.
        QVERIFY(t[0].group.isEmpty());
        for (int i = 1; i < 5; ++i) QCOMPARE(t[size_t(i)].group, QStringLiteral("Beat"));
        QCOMPARE(panel.selectedTriggers().size(), 4);
        QVERIFY(!panel.selectedTriggers().contains(kept));
        // A second fill gets its own group.
        QCOMPARE(panel.fillWithBeats(6.0, 7.0, 1, bump, QStringLiteral("Beat")), 2);
        QCOMPARE(cue.lightTriggers().back().group, QStringLiteral("Beat 2"));
        undo.undo();
        undo.undo();
        QCOMPARE(cue.lightTriggers().size(), size_t(1));
    }

    // ── Several at once, and groups ──────────────────────────────────────

    // Three bumps on different subs: one edit of Hold through the editor
    // reaches all three and leaves each one's sub alone.
    void bulkEditChangesOnlyWhatChanged()
    {
        setDesk(core::LightingDesk::Type::Eos);
        audio::AudioCue cue;
        QUndoStack undo;
        ui::LightTriggersPanel panel;
        panel.setCue(&cue);
        panel.setUndoStack(&undo);
        audio::LightTriggers ts;
        for (int i = 0; i < 3; ++i) {
            audio::LightTrigger t;
            t.start = 1.0 + i;
            t.name = QStringLiteral("B%1").arg(i + 1);
            t.enter.kind = audio::TriggerAction::Kind::Desk;
            t.enter.deskDo = audio::TriggerAction::DeskDo::SubBump;
            t.enter.number = QString::number(i + 1);
            t.enter.hold = 0.25;
            ts.push_back(t);
        }
        cue.setField(QStringLiteral("lightTriggers"), audio::triggersToJson(ts));
        panel.setSelection({ts[0].id, ts[1].id, ts[2].id}, ts[1].id);
        QCOMPARE(panel.selectedTriggers().size(), 3);
        QCOMPARE(panel.selectedTrigger(), ts[1].id);

        auto *multi = named<QLabel>(panel, "ltMultiLabel");
        QVERIFY(multi && !multi->isHidden());
        QVERIFY(multi->text().startsWith(QStringLiteral("3 triggers selected")));
        QVERIFY(!named<QLineEdit>(panel, "ltName")->isEnabled());

        auto *enter = enterEditor(panel);
        auto *hold = named<QDoubleSpinBox>(*enter, "ltHold");
        QVERIFY(hold);
        const int before = undo.count();
        hold->setValue(0.1);
        const auto &now = cue.lightTriggers();
        for (int i = 0; i < 3; ++i) {
            QCOMPARE(now[size_t(i)].enter.hold, 0.1);
            QCOMPARE(now[size_t(i)].enter.number, QString::number(i + 1));   // kept
        }
        QCOMPARE(undo.count(), before + 1);

        // Changing what it does carries the new action's fields.
        auto *what = named<QComboBox>(*enter, "ltDo");
        what->setCurrentIndex(what->findText(QStringLiteral("Bump fader")));
        for (const auto &t : cue.lightTriggers())
            QCOMPARE(t.enter.deskDo, audio::TriggerAction::DeskDo::FaderBump);
        QCOMPARE(cue.lightTriggers()[0].enter.number, QStringLiteral("1"));

        // Enable / disable for all.
        panel.applyTriggerAction(ts[0].id, QStringLiteral("toggleEnabled"));
        for (const auto &t : cue.lightTriggers()) QVERIFY(!t.enabled);

        undo.undo();
        undo.undo();
        undo.undo();
        QCOMPARE(cue.lightTriggers()[2].enter.hold, 0.25);
    }

    void groupsSelectTogether()
    {
        audio::AudioCue cue;
        QUndoStack undo;
        ui::LightTriggersPanel panel;
        panel.setCue(&cue);
        panel.setUndoStack(&undo);
        const QUuid a = panel.addTrigger(1.0), b = panel.addTrigger(2.0),
                    c = panel.addTrigger(3.0), d = panel.addTrigger(4.0);
        panel.setSelection({a, b, c}, a);
        panel.groupSelection(QStringLiteral("Chorus"));
        for (int i = 0; i < 3; ++i) QCOMPARE(cue.lightTriggers()[size_t(i)].group, QStringLiteral("Chorus"));
        QVERIFY(cue.lightTriggers()[3].group.isEmpty());

        QSignalSpy sets(&panel, &ui::LightTriggersPanel::selectionSetChanged);
        panel.clickTrigger(d);                                 // ungrouped: just it
        QCOMPARE(panel.selectedTriggers(), QList<QUuid>{d});
        panel.clickTrigger(b);                                 // grouped: the group
        QCOMPARE(panel.selectedTriggers(), (QList<QUuid>{a, b, c}));
        QCOMPARE(panel.selectedTrigger(), b);
        QVERIFY(sets.count() >= 2);
        panel.clickTrigger(b, Qt::AltModifier);                // just this one
        QCOMPARE(panel.selectedTriggers(), QList<QUuid>{b});
        panel.clickTrigger(d, Qt::ControlModifier);            // toggle in
        QCOMPARE(panel.selectedTriggers(), (QList<QUuid>{b, d}));
        panel.clickTrigger(d, Qt::ControlModifier);            // and out
        QCOMPARE(panel.selectedTriggers(), QList<QUuid>{b});
        panel.clickTrigger(d, Qt::ShiftModifier);              // add
        QCOMPARE(panel.selectedTriggers(), (QList<QUuid>{b, d}));

        panel.selectGroup(QStringLiteral("Chorus"));
        QCOMPARE(panel.selectedTriggers(), (QList<QUuid>{a, b, c}));
        panel.selectSpan(3.5, 4.5);
        QCOMPARE(panel.selectedTriggers(), (QList<QUuid>{a, b, c, d}));

        // The Group column shows it.
        auto *table = panel.findChild<QTableWidget *>(QStringLiteral("ltTable"));
        QVERIFY(table);
        QCOMPARE(table->item(0, 2)->text(), QStringLiteral("Chorus"));
        QCOMPARE(table->selectionModel()->selectedRows().size(), 4);

        // Ungroup.
        panel.setSelection({a, b, c}, a);
        panel.applyTriggerAction(a, QStringLiteral("ungroup"));
        for (const auto &t : cue.lightTriggers()) QVERIFY(t.group.isEmpty());
    }

    void moveDeleteAndDuplicateASelection()
    {
        audio::AudioCue cue;
        QUndoStack undo;
        ui::LightTriggersPanel panel;
        panel.setCue(&cue);
        panel.setUndoStack(&undo);
        audio::BeatGrid g;
        g.bpm = 120.0;                    // a beat = 0.5 s, a bar = 2 s
        panel.setBeatGrid(g);
        audio::TriggerAction go;
        go.kind = audio::TriggerAction::Kind::Desk;
        QCOMPARE(panel.fillWithBeats(2.0, 4.0, 1, go, QStringLiteral("Beat")), 4);   // 2, 2.5, 3, 3.5
        const auto ids = panel.selectedTriggers();

        panel.moveTriggersBy(ids, 0.5);
        QCOMPARE(cue.lightTriggers()[0].start, 2.5);
        QCOMPARE(cue.lightTriggers()[3].start, 4.0);
        panel.moveTriggersBy(ids, -10.0);             // can't go before 0
        QCOMPARE(cue.lightTriggers()[0].start, 0.0);
        QCOMPARE(cue.lightTriggers()[3].start, 1.5);
        undo.undo();
        undo.undo();

        // A block copies straight after itself, on the next bar.
        panel.setSelection(ids);
        panel.duplicateSelection();
        QCOMPARE(cue.lightTriggers().size(), size_t(8));
        QCOMPARE(cue.lightTriggers()[4].start, 4.0);
        QCOMPARE(cue.lightTriggers()[7].start, 5.5);
        QCOMPARE(cue.lightTriggers()[4].group, QStringLiteral("Beat 2"));
        QCOMPARE(panel.selectedTriggers().size(), 4);   // the copies
        QVERIFY(!panel.selectedTriggers().contains(ids[0]));

        // Delete the copies in one step.
        const int before = undo.count();
        panel.deleteSelection();
        QCOMPARE(cue.lightTriggers().size(), size_t(4));
        QCOMPARE(undo.count(), before + 1);
        undo.undo();
        QCOMPARE(cue.lightTriggers().size(), size_t(8));
    }

    // In the lane: clicking a grouped marker selects the group, dragging it
    // moves the lot, and Shift-drag selects a stretch.
    void laneSelectsAndMovesGroups()
    {
        audio::AudioCue cue;
        QUndoStack undo;
        ui::LightTriggersPanel panel;
        panel.setCue(&cue);
        panel.setUndoStack(&undo);
        audio::AudioEditorModel model;
        model.addTrack(QStringLiteral("T"));
        ui::TimelineCanvas canvas(&model);
        canvas.resize(900, 300);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));
        const double rate = 48000.0;
        canvas.setFramesPerPixel(rate / 100.0);          // 100 px per second
        // The same wiring as the audio editor.
        connect(&canvas, &ui::TimelineCanvas::triggerClicked, &panel, &ui::LightTriggersPanel::clickTrigger);
        connect(&canvas, &ui::TimelineCanvas::triggersSpanSelected, &panel, &ui::LightTriggersPanel::selectSpan);
        connect(&canvas, &ui::TimelineCanvas::triggersMovedBy, &panel, &ui::LightTriggersPanel::moveTriggersBy);
        connect(&canvas, &ui::TimelineCanvas::triggerMoved, &panel, &ui::LightTriggersPanel::moveTrigger);
        connect(&panel, &ui::LightTriggersPanel::selectionSetChanged, &canvas, &ui::TimelineCanvas::setSelectedTriggers);
        auto sync = [&] { canvas.setTriggers(cue.lightTriggers(), rate); };
        connect(&cue, &cues::Cue::changed, &canvas, sync);

        const QUuid a = panel.addTrigger(1.0), b = panel.addTrigger(2.0), c = panel.addTrigger(3.0);
        const QUuid lone = panel.addTrigger(5.0);
        panel.setSelection({a, b, c});
        panel.groupSelection(QStringLiteral("Hits"));
        panel.selectTrigger(lone);
        sync();

        const int laneY = ui::TimelineCanvas::kRulerHeight + ui::TimelineCanvas::kMarkerLaneHeight / 2;
        const int x0 = ui::TimelineCanvas::kHeaderWidth;
        QTest::mouseClick(&canvas, Qt::LeftButton, {}, QPoint(x0 + 200, laneY));   // b
        QCOMPARE(panel.selectedTriggers(), (QList<QUuid>{a, b, c}));
        QVERIFY(canvas.isTriggerSelected(a) && canvas.isTriggerSelected(c));
        QVERIFY(!canvas.isTriggerSelected(lone));

        // Drag b half a second right: the whole group goes.
        QTest::mousePress(&canvas, Qt::LeftButton, {}, QPoint(x0 + 200, laneY));
        QTest::mouseMove(&canvas, QPoint(x0 + 220, laneY));
        QTest::mouseMove(&canvas, QPoint(x0 + 250, laneY));
        QTest::mouseRelease(&canvas, Qt::LeftButton, {}, QPoint(x0 + 250, laneY));
        QCOMPARE(cue.lightTriggers()[0].start, 1.5);
        QCOMPARE(cue.lightTriggers()[1].start, 2.5);
        QCOMPARE(cue.lightTriggers()[2].start, 3.5);
        QCOMPARE(cue.lightTriggers()[3].start, 5.0);          // not selected, not moved

        // Alt+click: just one of the group.
        QTest::mouseClick(&canvas, Qt::LeftButton, Qt::AltModifier, QPoint(x0 + 250, laneY));
        QCOMPARE(panel.selectedTriggers(), QList<QUuid>{b});

        // Shift-drag over 3.0 – 6.0 adds c and lone.
        QTest::mousePress(&canvas, Qt::LeftButton, Qt::ShiftModifier, QPoint(x0 + 300, laneY));
        QTest::mouseMove(&canvas, QPoint(x0 + 400, laneY));
        QTest::mouseMove(&canvas, QPoint(x0 + 600, laneY));
        QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::ShiftModifier, QPoint(x0 + 600, laneY));
        QCOMPARE(panel.selectedTriggers(), (QList<QUuid>{b, c, lone}));
        QCOMPARE(cue.lightTriggers().size(), size_t(4));        // no range made
    }

    void fillDialogCountsAndDefaults()
    {
        audio::BeatGrid g;
        g.bpm = 120.0;
        ui::FillBeatsDialog dlg(g, 2.1, 60.0, std::nullopt);
        auto *selected = named<QWidget>(dlg, "ltFillSelected");
        QVERIFY(selected && !selected->isEnabled());   // no range selected
        dlg.setBars(1);                                // from the beat at the cursor (2.0), 1 bar
        QCOMPARE(dlg.range(), ui::FillBeatsDialog::Range::FromCursor);
        QCOMPARE(dlg.from(), 2.0);
        QCOMPARE(dlg.to(), 4.0);
        QCOMPARE(dlg.count(), 4);
        QCOMPARE(named<QLabel>(dlg, "ltFillCount")->text(), QStringLiteral("Adds 4 bumps"));
        const auto a = dlg.action();
        QCOMPARE(a.kind, audio::TriggerAction::Kind::Desk);
        QCOMPARE(a.deskDo, audio::TriggerAction::DeskDo::SubBump);
        QCOMPARE(a.number, QStringLiteral("1"));
        QCOMPARE(a.hold, 0.2);                         // 40% of a 0.5 s beat
        dlg.setEvery(4);                               // every bar
        QCOMPARE(dlg.count(), 1);
        dlg.setRange(ui::FillBeatsDialog::Range::WholeSong);
        QCOMPARE(dlg.count(), 30);
        QCOMPARE(dlg.triggers().size(), size_t(30));

        // A selected range fills its span.
        ui::FillBeatsDialog sel(g, 0.0, 60.0, std::make_pair(10.0, 12.0));
        QCOMPARE(sel.range(), ui::FillBeatsDialog::Range::Selected);
        QCOMPARE(sel.count(), 4);

        // No grid: nothing to add, and it says why.
        ui::FillBeatsDialog none(audio::BeatGrid{}, 0.0, 60.0, std::nullopt);
        QCOMPARE(none.count(), 0);
        QVERIFY(!named<QLabel>(none, "ltFillHint")->isHidden());
        auto *box = none.findChild<QDialogButtonBox *>();
        QVERIFY(box && !box->button(QDialogButtonBox::Ok)->isEnabled());
    }

    void laneSnapsToBeatsUnlessAlt()
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
        audio::BeatGrid g;
        g.bpm = 120.0;
        g.firstBeat = 0.1;                               // beats at 0.1, 0.6, … 2.1, 2.6
        canvas.setBeatGrid(g);
        canvas.setSnapToBeats(true);

        QSignalSpy added(&canvas, &ui::TimelineCanvas::triggerAdded);
        const int laneY = ui::TimelineCanvas::kRulerHeight + ui::TimelineCanvas::kMarkerLaneHeight / 2;
        const int x0 = ui::TimelineCanvas::kHeaderWidth;
        QTest::mouseClick(&canvas, Qt::LeftButton, {}, QPoint(x0 + 237, laneY));
        QCOMPARE(added.count(), 1);
        QVERIFY(std::abs(added[0][0].toDouble() - 2.6) < 1e-9);

        QTest::mouseClick(&canvas, Qt::LeftButton, Qt::AltModifier, QPoint(x0 + 237, laneY));
        QCOMPARE(added.count(), 2);
        QVERIFY(std::abs(added[1][0].toDouble() - 2.37) < 1e-6);

        // A drag makes a range from beat to beat.
        QTest::mousePress(&canvas, Qt::LeftButton, {}, QPoint(x0 + 405, laneY));
        QTest::mouseMove(&canvas, QPoint(x0 + 500, laneY));
        QTest::mouseMove(&canvas, QPoint(x0 + 640, laneY));
        QTest::mouseRelease(&canvas, Qt::LeftButton, {}, QPoint(x0 + 640, laneY));
        QCOMPARE(added.count(), 3);
        QVERIFY(std::abs(added[2][0].toDouble() - 4.1) < 1e-9);
        QVERIFY(std::abs(added[2][1].toDouble() - 6.6) < 1e-9);

        // Snap off: where the mouse is.
        canvas.setSnapToBeats(false);
        QTest::mouseClick(&canvas, Qt::LeftButton, {}, QPoint(x0 + 237, laneY));
        QVERIFY(std::abs(added[3][0].toDouble() - 2.37) < 1e-6);
    }

    void pointAtCursorSnaps()
    {
        audio::AudioCue cue;
        ui::LightTriggersPanel panel;
        panel.setCue(&cue);
        audio::BeatGrid g;
        g.bpm = 120.0;
        panel.setBeatGrid(g);
        panel.setSnapToBeats(true);
        panel.setCursorSeconds(3.2);
        button(panel, QStringLiteral("+ Point here"))->click();
        QCOMPARE(cue.lightTriggers()[0].start, 3.0);
        panel.setSnapToBeats(false);
        button(panel, QStringLiteral("+ Point here"))->click();
        QCOMPARE(cue.lightTriggers()[1].start, 3.2);
    }

    void detectFindsTheClickTrackTempo()
    {
        const int sr = 22050;
        const double bpm = 128.0, firstBeat = 0.37, seconds = 30.0;
        // Stereo click track: a kick-like click on every beat over quiet noise.
        std::vector<float> pcm(size_t(seconds * sr) * 2);
        auto *rng = QRandomGenerator::global();
        for (auto &x : pcm) x = float((rng->generateDouble() - 0.5) * 0.02);
        for (double t = firstBeat; t < seconds; t += 60.0 / bpm) {
            const auto start = size_t(t * sr);
            for (size_t i = 0; i < size_t(0.06 * sr) && (start + i) * 2 + 1 < pcm.size(); ++i) {
                const double env = std::exp(-double(i) / (0.012 * sr));
                const float v = float(0.8 * env * std::sin(2.0 * 3.14159265358979323846 * 60.0 * double(i) / sr));
                pcm[(start + i) * 2] += v;
                pcm[(start + i) * 2 + 1] += v;
            }
        }

        audio::AudioCue cue;
        QUndoStack undo;
        ui::LightTriggersPanel panel;
        panel.setCue(&cue);
        panel.setUndoStack(&undo);
        QSignalSpy done(&panel, &ui::LightTriggersPanel::tempoDetected);
        auto *detect = named<QPushButton>(panel, "ltDetect");
        panel.detectFromPcm(std::move(pcm), sr, 2);
        QVERIFY(panel.isDetecting());
        QVERIFY(!detect->isEnabled());                  // busy while it listens
        QTRY_VERIFY_WITH_TIMEOUT(!panel.isDetecting(), 60000);
        QCOMPARE(done.count(), 1);
        QVERIFY2(std::abs(cue.beatGrid().bpm - 128.0) < 0.1,
                 qPrintable(QString::number(cue.beatGrid().bpm)));
        const double beat = 60.0 / bpm;
        double off = std::fmod(std::abs(cue.beatGrid().firstBeat - firstBeat), beat);
        QVERIFY(std::min(off, beat - off) < 0.02);
        QVERIFY(detect->isEnabled());
        QCOMPARE(undo.count(), 1);
        QVERIFY(named<QLabel>(panel, "ltGridStatus")->text().startsWith(QStringLiteral("Detected 128.")));
        undo.undo();
        QVERIFY(!cue.beatGrid().isSet());

        // Without a detect source (no editor), the button says so instead.
        detect->click();
        QVERIFY(!panel.isDetecting());
    }

    void editorWindowTapsOnT()
    {
        audio::AudioCue cue;
        auto *win = new ui::AudioEditorWindow(&cue);
        win->setAttribute(Qt::WA_DeleteOnClose, false);
        win->resize(1280, 720);
        win->show();
        QVERIFY(QTest::qWaitForWindowActive(win));
        win->showLightingTab();
        win->refreshLightingDesk();
        auto *panel = win->findChild<ui::LightTriggersPanel *>();
        QVERIFY(panel);
        auto *canvas = win->findChild<ui::TimelineCanvas *>();
        QVERIFY(canvas);
        canvas->setFocus();
        QTest::keyClick(canvas, Qt::Key_T);
        QTest::qWait(400);
        QTest::keyClick(canvas, Qt::Key_T);
        QVERIFY(cue.beatGrid().isSet());              // two taps: a tempo
        QVERIFY(std::abs(cue.beatGrid().bpm - 150.0) < 40.0);
        // The canvas follows the cue's grid.
        QCOMPARE(canvas->beatGrid().bpm, cue.beatGrid().bpm);

        // Typing a T into a text field is just a T.
        audio::BeatGrid g;
        cue.setField(QStringLiteral("beatGrid"), g.toJson());
        auto *name = named<QLineEdit>(*panel, "ltName");
        panel->addTrigger(1.0);
        name->setFocus();
        QTest::keyClick(name, Qt::Key_T);
        QTest::qWait(300);
        QTest::keyClick(name, Qt::Key_T);
        QVERIFY(!cue.beatGrid().isSet());
        win->close();
        delete win;
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
