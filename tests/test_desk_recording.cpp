#include <QTest>
#include <QApplication>
#include <QElapsedTimer>
#include <QPushButton>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QToolButton>
#include <QUndoStack>
#include <QtEndian>

#include "audio/AudioCue.h"
#include "audio/DeskRecording.h"
#include "osc/EosFeedback.h"
#include "osc/OscCodec.h"
#include "osc/OscMessage.h"
#include "ui/AudioEditorWindow.h"
#include "ui/DeskTakeRecorder.h"
#include "ui/LightTriggersPanel.h"

using namespace quewi;

// Recording lighting triggers from the desk: the take (pure), the Eos link
// reporting each fired cue once, the recorder timing them against a song's
// playhead with a fake desk firing cues at known moments, and keeping a take
// as triggers (one undo step) from the Lighting tab and the audio editor.
class DeskRecordingTests : public QObject {
    Q_OBJECT

    static QByteArray framed(const osc::Message &m)
    {
        const QByteArray pkt = osc::Codec::encode(m);
        QByteArray out(4, Qt::Uninitialized);
        qToBigEndian<quint32>(quint32(pkt.size()), out.data());
        return out + pkt;
    }

    struct FakeDesk {
        QTcpServer server;
        QTcpSocket *peer = nullptr;
        osc::EosFeedback fb;
        bool start()
        {
            if (!server.listen(QHostAddress::LocalHost, 0)) return false;
            fb.setTimings(200, 1000, 60000);
            fb.start(QStringLiteral("127.0.0.1"), server.serverPort());
            return QTest::qWaitFor([this] { return server.hasPendingConnections(); }, 3000)
                && (peer = server.nextPendingConnection()) != nullptr;
        }
        void active(const QString &text)
        {
            peer->write(framed({QStringLiteral("/eos/out/active/cue/text"), {osc::Argument::s(text)}}));
            peer->flush();
        }
        void event(const QString &address)
        {
            peer->write(framed({address, {}}));
            peer->flush();
        }
    };

private slots:
    void takeRules()
    {
        audio::DeskRecording r;
        QVERIFY(!r.add(QStringLiteral("1"), QStringLiteral("5"), {}, 1.0));     // not recording
        r.start(QStringLiteral("2"));
        QVERIFY(r.add(QStringLiteral("2"), QStringLiteral("8.4"), QStringLiteral("Back"), 12.4));
        QVERIFY(!r.add(QStringLiteral("2"), QStringLiteral("8.40"), {}, 12.5));   // the same fire, twice
        QVERIFY(!r.add(QStringLiteral("1"), QStringLiteral("3"), {}, 13.0));      // another list
        QVERIFY(!r.add(QStringLiteral("2"), QStringLiteral("9"), {}, -1.0));      // not playing
        QVERIFY(r.add(QStringLiteral("2"), QStringLiteral("8.4"), {}, 20.0));     // again, later: kept
        QVERIFY(r.add(QStringLiteral("2"), QStringLiteral("7"), {}, 3.0));
        QCOMPARE(r.takes().size(), size_t(3));
        r.stop();
        QVERIFY(!r.add(QStringLiteral("2"), QStringLiteral("10"), {}, 30.0));

        const auto t = audio::DeskRecording::toTriggers(r.takes(), QStringLiteral("Recorded"));
        QCOMPARE(t.size(), size_t(3));
        QCOMPARE(t[0].start, 3.0);                                               // in time order
        QCOMPARE(t[1].name, QStringLiteral("LX 8.4 Back"));
        QCOMPARE(t[1].enter.kind, audio::TriggerAction::Kind::Desk);
        QCOMPARE(t[1].enter.deskDo, audio::TriggerAction::DeskDo::GoToCue);
        QCOMPARE(t[1].enter.number, QStringLiteral("8.4"));
        QCOMPARE(t[1].enter.list, 2);
        QCOMPARE(t[1].group, QStringLiteral("Recorded"));
        QVERIFY(!t[1].isRange());
        audio::BeatGrid g;
        g.bpm = 120.0;                                                           // a beat every 0.5 s
        const auto snapped = audio::DeskRecording::toTriggers(r.takes(), {}, &g);
        QCOMPARE(snapped[1].start, 12.5);
        QCOMPARE(audio::DeskRecording::timeText(72.25), QStringLiteral("1:12.25"));
    }

    // The link says "this cue ran" once per fire: not for the state it
    // reports on connecting, once for the event + active change pair.
    void linkReportsEachFireOnce()
    {
        FakeDesk desk;
        QVERIFY(desk.start());
        QSignalSpy fired(&desk.fb, &osc::EosFeedback::cueFired);
        desk.active(QStringLiteral("2/8.3 Right 0.0 100%"));                     // the state on connect
        QTRY_COMPARE(desk.fb.active().cue, QStringLiteral("8.3"));
        QCOMPARE(fired.count(), 0);
        desk.active(QStringLiteral("2/8.4 Back 2.0 0%"));                        // GO
        QTRY_COMPARE(fired.count(), 1);
        QCOMPARE(fired.last().at(0).toString(), QStringLiteral("2"));
        QCOMPARE(fired.last().at(1).toString(), QStringLiteral("8.4"));
        QCOMPARE(fired.last().at(2).toString(), QStringLiteral("Back"));
        desk.active(QStringLiteral("2/8.4 Back 2.0 40%"));                       // progress: not a fire
        desk.event(QStringLiteral("/eos/out/event/cue/2/9/fire"));               // the desk's own event…
        QTRY_COMPARE(fired.count(), 2);
        desk.active(QStringLiteral("2/9 Calm 3.0 0%"));                          // …and its active change
        QTest::qWait(150);
        QCOMPARE(fired.count(), 2);
    }

    // A fake desk fires cues at known moments while a "song" plays from 10 s:
    // they're recorded at those moments of the song.
    void recorderTimesCuesAgainstThePlayhead()
    {
        FakeDesk desk;
        QVERIFY(desk.start());
        ui::DeskTakeRecorder rec;
        QVERIFY(!rec.whyNot().isEmpty());                                        // no desk yet
        rec.setFeedback(&desk.fb);
        QVERIFY(!rec.start());                                                   // connected, not live
        desk.active(QStringLiteral("1/1 Preset 0.0 100%"));
        QTRY_COMPARE(desk.fb.link(), osc::EosFeedback::Link::Live);
        QElapsedTimer song;
        song.start();
        rec.setPlayhead([&song] { return 10.0 + song.elapsed() / 1000.0; });
        QVERIFY(rec.start());
        QSignalSpy got(&rec, &ui::DeskTakeRecorder::recorded);
        // Compare against the song's position when the desk actually fired,
        // not when the timer was due: a busy CI Mac runs timers 100 ms+ late.
        double firedAt[2] = {0, 0};
        QTimer::singleShot(300, [&] { firedAt[0] = 10.0 + song.elapsed() / 1000.0;
                                      desk.active(QStringLiteral("1/2 Sunrise 3.0 0%")); });
        QTimer::singleShot(700, [&] { firedAt[1] = 10.0 + song.elapsed() / 1000.0;
                                      desk.event(QStringLiteral("/eos/out/event/cue/1/3/fire")); });
        QTRY_COMPARE_WITH_TIMEOUT(got.count(), 2, 3000);
        rec.stop();
        const auto takes = rec.takes();
        QCOMPARE(takes.size(), size_t(2));
        QCOMPARE(takes[0].cue, QStringLiteral("2"));
        QCOMPARE(takes[0].label, QStringLiteral("Sunrise"));
        // Recorded when it arrived: just after it was sent (local TCP), never before.
        for (int i = 0; i < 2; ++i)
            QVERIFY2(takes[size_t(i)].at >= firedAt[i] - 0.001 && takes[size_t(i)].at - firedAt[i] < 0.15,
                     qPrintable(QStringLiteral("take %1 at %2, sent at %3").arg(i).arg(takes[size_t(i)].at).arg(firedAt[i])));
        QVERIFY(takes[1].at > takes[0].at);
        // Stopped: later fires aren't recorded.
        desk.active(QStringLiteral("1/4 Noon 3.0 0%"));
        QTest::qWait(150);
        QCOMPARE(rec.takes().size(), size_t(2));
    }

    // Keeping a take from the Lighting tab: one undo step, a "Recorded" group.
    void keepingATakeIsOneUndoStep()
    {
        audio::AudioCue cue;
        QUndoStack undo;
        ui::LightTriggersPanel panel;
        panel.setCue(&cue);
        panel.setUndoStack(&undo);
        audio::DeskRecording r;
        r.start();
        r.add(QStringLiteral("1"), QStringLiteral("5"), QStringLiteral("Look"), 2.0);
        r.add(QStringLiteral("1"), QStringLiteral("6"), {}, 4.5);
        QCOMPARE(panel.keepRecorded(audio::DeskRecording::toTriggers(r.takes()), QStringLiteral("Recorded")), 2);
        QCOMPARE(undo.count(), 1);
        QCOMPARE(cue.lightTriggers().size(), size_t(2));
        QCOMPARE(cue.lightTriggers()[0].group, QStringLiteral("Recorded"));
        panel.keepRecorded(audio::DeskRecording::toTriggers(r.takes()), QStringLiteral("Recorded"));
        QCOMPARE(cue.lightTriggers()[3].group, QStringLiteral("Recorded 2"));
        undo.undo();
        undo.undo();
        QVERIFY(cue.lightTriggers().empty());
        // The toggle and its indicator.
        auto *btn = panel.findChild<QPushButton *>(QStringLiteral("ltRecord"));
        QVERIFY(btn);
        QSignalSpy toggled(&panel, &ui::LightTriggersPanel::recordToggled);
        btn->click();
        QCOMPARE(toggled.count(), 1);
        panel.setRecordingState(true, 2, QStringLiteral("LX 6 at 0:04.50"));
        QVERIFY(btn->isChecked());
        panel.setRecordingState(false);
        QVERIFY(!btn->isChecked());
    }

    // The audio editor: can't record without a live desk (says why, toggles
    // back off); a take is reviewed and kept as triggers on its song.
    void editorRecordsAndKeeps()
    {
        audio::AudioCue cue;
        QUndoStack undo;
        ui::AudioEditorWindow editor(&cue);
        editor.setTriggerSupport(nullptr, &undo, {});
        auto *tool = editor.findChild<QToolButton *>(QStringLiteral("editorRecordFromDesk"));
        QVERIFY(tool);
        QVERIFY(!editor.startRecordingFromDesk());                              // no desk
        QVERIFY(!tool->isChecked());
        QVERIFY(!editor.isRecordingFromDesk());

        FakeDesk desk;
        QVERIFY(desk.start());
        editor.setDeskFeedback(&desk.fb);
        desk.active(QStringLiteral("1/1 Preset 0.0 100%"));
        QTRY_COMPARE(desk.fb.link(), osc::EosFeedback::Link::Live);
        // (No sound device / song here: drive the recorder at a fixed playhead.)
        editor.deskRecorder()->setPlayhead([] { return 5.0; });
        QVERIFY(editor.deskRecorder()->start());
        desk.active(QStringLiteral("1/2 Sunrise 3.0 0%"));
        QTRY_COMPARE(editor.deskRecorder()->takes().size(), size_t(1));
        editor.deskRecorder()->stop();
        bool asked = false;
        editor.setRecordReviewer([&asked](const std::vector<audio::RecordedCue> &takes, bool, bool *snap) {
            asked = takes.size() == 1;
            *snap = false;
            return true;
        });
        editor.reviewRecording();
        QVERIFY(asked);
        QCOMPARE(cue.lightTriggers().size(), size_t(1));
        QCOMPARE(cue.lightTriggers()[0].start, 5.0);
        QCOMPARE(cue.lightTriggers()[0].enter.number, QStringLiteral("2"));
        QCOMPARE(undo.count(), 1);
        // Discarding leaves the song alone.
        QVERIFY(editor.deskRecorder()->start());
        desk.active(QStringLiteral("1/3 Noon 3.0 0%"));
        QTRY_COMPARE(editor.deskRecorder()->takes().size(), size_t(1));
        editor.deskRecorder()->stop();
        editor.setRecordReviewer([](const std::vector<audio::RecordedCue> &, bool, bool *) { return false; });
        editor.reviewRecording();
        QCOMPARE(cue.lightTriggers().size(), size_t(1));
    }
};

QTEST_MAIN(DeskRecordingTests)
#include "test_desk_recording.moc"
