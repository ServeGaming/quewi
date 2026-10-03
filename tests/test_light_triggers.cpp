#include <QTest>
#include <QJsonDocument>
#include <QUndoStack>

#include "audio/AudioCue.h"
#include "audio/DeskCommands.h"
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

    // ── Simple mode: desk actions → the desk's own messages ──────────────
    static audio::TriggerAction desk(audio::TriggerAction::DeskDo d, const QString &n = {},
                                     int list = 1, int level = 100, double hold = 0.25)
    {
        audio::TriggerAction a;
        a.kind = audio::TriggerAction::Kind::Desk;
        a.deskDo = d; a.number = n; a.list = list; a.level = level; a.hold = hold;
        return a;
    }
    // "addr args @delay" lines, for readable comparisons.
    static QStringList wire(const std::vector<audio::DeskSend> &s)
    {
        QStringList out;
        for (const auto &x : s) {
            if (x.type == audio::DeskSend::Type::Msc) {
                out << QStringLiteral("MSC %1 %2").arg(x.mscCommand, 2, 16, QChar('0'))
                                                .arg(QString::fromLatin1(x.mscPayload.toHex()));
                continue;
            }
            QStringList a;
            for (const auto &v : x.args)
                a << (v.typeId() == QMetaType::Float ? QString::number(v.toDouble(), 'g', 4) : v.toString());
            out << (x.address + (a.isEmpty() ? QString() : QLatin1Char(' ') + a.join(QLatin1Char(',')))
                    + (x.delayMs ? QStringLiteral(" @%1").arg(x.delayMs) : QString()));
        }
        return out;
    }

    void eosDeskCommands()
    {
        using D = audio::TriggerAction::DeskDo;
        core::LightingDesk eos;   // default = Eos
        QCOMPARE(wire(audio::deskSends(eos, desk(D::Go))),
                 (QStringList{"/eos/key/go_0 1", "/eos/key/go_0 0 @50"}));
        QCOMPARE(wire(audio::deskSends(eos, desk(D::Stop))),
                 (QStringList{"/eos/key/stop 1", "/eos/key/stop 0 @50"}));
        QCOMPARE(wire(audio::deskSends(eos, desk(D::GoToCue, "12.5", 2))),
                 QStringList{"/eos/cue/2/12.5/fire"});
        QCOMPARE(wire(audio::deskSends(eos, desk(D::SubLevel, "3", 1, 80))),
                 QStringList{"/eos/sub/3 0.8"});
        QCOMPARE(wire(audio::deskSends(eos, desk(D::SubBump, "3", 1, 100, 0.2))),
                 (QStringList{"/eos/sub/3/fire 1", "/eos/sub/3/fire 0 @200"}));
        QCOMPARE(wire(audio::deskSends(eos, desk(D::FaderLevel, "4", 2, 50))),
                 (QStringList{"/eos/fader/9/config/2/10", "/eos/fader/9/4 0.5"}));
        QCOMPARE(wire(audio::deskSends(eos, desk(D::FaderBump, "4", 1, 100, 0.1))),
                 (QStringList{"/eos/fader/9/config/1/10", "/eos/fader/9/4/fire 1",
                              "/eos/fader/9/4/fire 0 @100"}));
        QCOMPARE(wire(audio::deskSends(eos, desk(D::Macro, "10"))),
                 (QStringList{"/eos/macro/10/fire 1", "/eos/macro/10/fire 0 @50"}));
        auto cmd = desk(D::Command);
        cmd.text = QStringLiteral("Chan 1 At Full");
        QCOMPARE(wire(audio::deskSends(eos, cmd)), QStringList{"/eos/newcmd Chan 1 At Full Enter"});
        cmd.text = QStringLiteral("Sub 2 At 50#");
        QCOMPARE(wire(audio::deskSends(eos, cmd)), QStringList{"/eos/newcmd Sub 2 At 50#"});

        // Missing numbers are refused with a reason, not sent half-built.
        QString why;
        QVERIFY(audio::deskSends(eos, desk(D::SubBump), &why).empty());
        QVERIFY(!why.isEmpty());
    }

    void maDeskCommands()
    {
        using D = audio::TriggerAction::DeskDo;
        core::LightingDesk ma3;
        ma3.type = core::LightingDesk::Type::Ma3;
        QCOMPARE(wire(audio::deskSends(ma3, desk(D::Go))), QStringList{"/cmd Go+"});
        QCOMPARE(wire(audio::deskSends(ma3, desk(D::GoToCue, "5", 3))),
                 QStringList{"/cmd Goto Cue 5 Sequence 3"});
        ma3.ma3Prefix = QStringLiteral("gma3");
        QCOMPARE(wire(audio::deskSends(ma3, desk(D::Back))), QStringList{"/gma3/cmd Go-"});
        QVERIFY(!audio::deskSupports(ma3.type, D::SubBump));
        QVERIFY(audio::deskSends(ma3, desk(D::SubBump, "1")).empty());

        core::LightingDesk ma2;
        ma2.type = core::LightingDesk::Type::Ma2Msc;
        QCOMPARE(wire(audio::deskSends(ma2, desk(D::Go))), QStringList{"MSC 01 "});
        QCOMPARE(wire(audio::deskSends(ma2, desk(D::GoToCue, "7", 2))), QStringList{"MSC 01 370032"});
        QCOMPARE(wire(audio::deskSends(ma2, desk(D::Macro, "5"))), QStringList{"MSC 07 05"});
        QVERIFY(audio::deskSends(ma2, desk(D::Back)).empty());
    }

    void deskActionsSaveAndTheDeskTravelsWithTheShow()
    {
        auto a = desk(audio::TriggerAction::DeskDo::SubBump, "3", 1, 100, 0.4);
        const auto back = audio::TriggerAction::fromJson(a.toJson());
        QCOMPARE(back.kind, audio::TriggerAction::Kind::Desk);
        QCOMPARE(back.deskDo, audio::TriggerAction::DeskDo::SubBump);
        QCOMPARE(back.number, QStringLiteral("3"));
        QCOMPARE(back.hold, 0.4);
        QVERIFY(a.setField(QStringLiteral("do"), QStringLiteral("faderLevel")));
        QVERIFY(a.setField(QStringLiteral("level"), 250));            // clamped
        QCOMPARE(a.level, 100);

        core::LightingDesk d;
        d.type = core::LightingDesk::Type::Ma3;
        d.host = QStringLiteral("10.0.0.5");
        d.port = 8001;
        d.ma3Prefix = QStringLiteral("/gma3/");
        const auto d2 = core::LightingDesk::fromJson(d.toJson());
        QCOMPARE(d2.type, core::LightingDesk::Type::Ma3);
        QCOMPARE(d2.host, QStringLiteral("10.0.0.5"));
        QCOMPARE(d2.ma3Prefix, QStringLiteral("gma3"));   // slashes stripped on load
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
