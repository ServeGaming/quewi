#include <QTest>
#include <QJsonDocument>
#include <QUndoStack>

#include "audio/AudioCue.h"
#include "audio/LightTrigger.h"
#include "core/UndoCommands.h"

using namespace quewi;
using audio::LightTrigger;
using audio::LightTriggers;
using audio::TriggerEvent;
using audio::TriggerTracker;

// Lighting triggers: the model, saving, and the rules for when a playing
// song sends each one.
class LightTriggerTests : public QObject {
    Q_OBJECT

    static LightTrigger point(double at, const QString &name)
    {
        LightTrigger t;
        t.name = name;
        t.start = at;
        t.enter.kind = audio::TriggerAction::Kind::Osc;
        t.enter.address = QStringLiteral("/eos/cue/1/%1/fire").arg(name);
        return t;
    }
    static LightTrigger range(double a, double b, const QString &name)
    {
        LightTrigger t = point(a, name);
        t.end = b;
        t.exit.kind = audio::TriggerAction::Kind::Msc;
        t.exit.qNumber = name;
        return t;
    }
    // Run positions through a tracker; render events as "name+"/"name-".
    static QStringList run(const LightTriggers &t, const std::vector<double> &positions,
                           double loopStart = 0.0, double loopEnd = -1.0, bool stopAtEnd = false)
    {
        TriggerTracker tr;
        QStringList out;
        auto add = [&](const std::vector<TriggerEvent> &ev) {
            for (const auto &e : ev)
                out << t[size_t(e.index)].name + (e.exit ? QStringLiteral("-") : QStringLiteral("+"));
        };
        bool first = true;
        for (double p : positions) {
            add(first ? tr.begin(t, p) : tr.advance(t, p, loopStart, loopEnd));
            first = false;
        }
        if (stopAtEnd) add(tr.stop(t));
        return out;
    }

private slots:
    void pointsFireOnceAsThePlayheadPasses()
    {
        const LightTriggers t{point(1.0, "a"), point(2.0, "b")};
        QCOMPARE(run(t, {0.0, 0.5, 0.99, 1.0, 1.01, 1.5, 1.9, 2.2, 2.3}),
                 (QStringList{"a+", "b+"}));
        // A point right where playback starts fires immediately.
        QCOMPARE(run(t, {1.0, 1.1}), QStringList{"a+"});
        // Paused (position not moving) never refires.
        QCOMPARE(run(t, {0.9, 1.05, 1.05, 1.05}), QStringList{"a+"});
    }

    void rangesSendEnterAndExit()
    {
        const LightTriggers t{range(1.0, 2.0, "chorus")};
        QCOMPARE(run(t, {0.0, 0.4, 0.8, 1.2, 1.6, 2.0, 2.4}),
                 (QStringList{"chorus+", "chorus-"}));
        // A range shorter than one tick still sends both, in order.
        const LightTriggers blip{range(1.00, 1.01, "blip")};
        QCOMPARE(run(blip, {0.9, 1.1}), (QStringList{"blip+", "blip-"}));
        // Starting inside a range enters it straight away.
        QCOMPARE(run(t, {1.5, 1.6}), QStringList{"chorus+"});
    }

    void stoppingInsideARangeSendsItsExit()
    {
        const LightTriggers t{range(1.0, 5.0, "verse"), point(3.0, "hit")};
        QCOMPARE(run(t, {0.5, 0.9, 1.3, 1.7}, 0, -1, /*stop*/ true),
                 (QStringList{"verse+", "verse-"}));
    }

    void seeksSkipPointsButCatchRangesUp()
    {
        const LightTriggers t{point(2.0, "hit"), range(5.0, 8.0, "chorus"), range(0.0, 3.0, "intro")};
        // Jump from 1 s to 6 s: the hit at 2 s is skipped; intro is left,
        // chorus is entered.
        QCOMPARE(run(t, {1.0, 6.0}), (QStringList{"intro+", "intro-", "chorus+"}));
        // Seek backwards from inside chorus to inside intro.
        QCOMPARE(run(t, {6.0, 6.2, 1.0}), (QStringList{"chorus+", "chorus-", "intro+"}));
    }

    void loopsReplayTheirTriggers()
    {
        // Loop 0–4 s. A hit right on the loop end and one at the top.
        const LightTriggers t{point(0.0, "top"), point(4.0, "end"), range(3.0, 4.0, "tail")};
        QCOMPARE(run(t, {0.0, 0.4, 3.8, 0.1, 0.3}, 0.0, 4.0),   // 3.8 → wraps to 0.1
                 (QStringList{"top+",                     // begin
                              // 0.4 → 3.8 isn't a step (>0.5 s), so it's a jump:
                              "tail+",
                              // wrap: the end point and tail's exit (same instant,
                              // list order), then the top again
                              "end+", "tail-", "top+"}));
    }

    void aStalledTickIsStillPlaybackNotASeek()
    {
        // The UI thread froze for 2 s: the position moved 2 s in 2 s of real
        // time, so the hit in between must still fire.
        const LightTriggers t{point(2.0, "hit")};
        TriggerTracker tr;
        tr.begin(t, 1.0);
        const auto ev = tr.advance(t, 3.0, 0, -1, /*wallElapsed*/ 2.0);
        QCOMPARE(int(ev.size()), 1);
        // The same move with no time passing is a seek, and skips it.
        TriggerTracker seek;
        seek.begin(t, 1.0);
        QVERIFY(seek.advance(t, 3.0, 0, -1, 0.01).empty());
    }

    void disabledTriggersStayQuiet()
    {
        auto t = LightTriggers{point(1.0, "a"), range(2.0, 3.0, "r")};
        t[0].enabled = false;
        t[1].enabled = false;
        QVERIFY(run(t, {0.5, 0.9, 1.3, 1.7, 2.1, 2.5, 2.9, 3.3}).isEmpty());
    }

    void actionsBuildTheRightBytes()
    {
        audio::TriggerAction a;
        a.kind = audio::TriggerAction::Kind::Midi;
        a.midiType = audio::TriggerAction::MidiType::NoteOn;
        a.channel = 2; a.data1 = 60; a.data2 = 100;
        QCOMPARE(a.midiBytes(), QByteArray::fromHex("913c64"));
        a.midiType = audio::TriggerAction::MidiType::ProgramChange; a.data1 = 5;
        QCOMPARE(a.midiBytes(), QByteArray::fromHex("c105"));
        a.midiType = audio::TriggerAction::MidiType::Raw; a.rawHex = QStringLiteral("F0 7f, 01 f7");
        QCOMPARE(a.midiBytes(), QByteArray::fromHex("f07f01f7"));
        a.rawHex = QStringLiteral("zz");
        QVERIFY(a.midiBytes().isEmpty());

        a.kind = audio::TriggerAction::Kind::Msc;
        a.qNumber = QStringLiteral("12.5"); a.qList = QStringLiteral("2");
        QCOMPARE(a.mscPayload(), QByteArray("12.5\0" "2", 6));
    }

    void savesAndUndoesOnTheCue()
    {
        audio::AudioCue cue;
        LightTriggers t{point(1.5, "a"), range(10.0, 20.0, "b")};
        t[1].exit.kind = audio::TriggerAction::Kind::FireCue;
        t[1].exit.cueId = QUuid::createUuid();

        QUndoStack undo;
        undo.push(new core::EditCueFieldCommand(&cue, QStringLiteral("lightTriggers"),
                                                cue.field(QStringLiteral("lightTriggers")),
                                                audio::triggersToJson(t)));
        QCOMPARE(cue.lightTriggers().size(), size_t(2));

        audio::AudioCue back;
        back.fromPayload(cue.toPayload());
        QVERIFY(back.lightTriggers() == cue.lightTriggers());
        QCOMPARE(back.lightTriggers()[1].exit.cueId, t[1].exit.cueId);

        undo.undo();
        QVERIFY(cue.lightTriggers().empty());
        QVERIFY(!cue.toPayload().contains(QStringLiteral("lightTriggers")));

        // A remote sends JSON text; bad JSON leaves the triggers alone.
        const QString json = QString::fromUtf8(
            QJsonDocument(audio::triggersToJson(t)).toJson(QJsonDocument::Compact));
        cue.setField(QStringLiteral("lightTriggers"), json);
        QCOMPARE(cue.lightTriggers().size(), size_t(2));
        cue.setField(QStringLiteral("lightTriggers"), QStringLiteral("{not json"));
        QCOMPARE(cue.lightTriggers().size(), size_t(2));
    }

    void fieldsAreReachableByName()
    {
        LightTrigger t = range(1.0, 2.0, "x");
        QVERIFY(t.setField(QStringLiteral("enter.address"), QStringLiteral("/eos/key/go_0")));
        QVERIFY(t.setField(QStringLiteral("exit.kind"), QStringLiteral("midi")));
        QVERIFY(t.setField(QStringLiteral("exit.data1"), 64));
        QVERIFY(!t.setField(QStringLiteral("start"), QStringLiteral("soon")));
        QVERIFY(!t.setField(QStringLiteral("bogus"), 1));
        QCOMPARE(t.field(QStringLiteral("enter.address")).toString(), QStringLiteral("/eos/key/go_0"));
        QCOMPARE(t.exit.kind, audio::TriggerAction::Kind::Midi);
        QCOMPARE(t.exit.data1, 64);
        QVERIFY(t.setField(QStringLiteral("end"), -1));
        QVERIFY(!t.isRange());
        QVERIFY(!t.toJson().contains(QStringLiteral("exit")));
    }
};

QTEST_MAIN(LightTriggerTests)
#include "test_light_triggers.moc"
