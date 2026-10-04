#include <QTest>
#include <QPushButton>
#include <QDoubleSpinBox>

#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QMouseEvent>
#include <QPointer>
#include <QSignalSpy>
#include <QTimer>
#include <QUndoStack>

#include "audio/AudioCue.h"
#include "core/UndoCommands.h"
#include "ui/VideoEditorWindow.h"
#include "ui/VideoInspector.h"
#include "ui/VideoThumbnailer.h"
#include "ui/VideoTimeline.h"
#include "video/PictureTiming.h"
#include "video/VideoCue.h"
#include "video/VideoEngine.h"
#include "video/VideoLayer.h"

using namespace quewi;

namespace {

// Spins the event loop until pred() holds or ms pass.
template <typename Pred>
bool waitFor(Pred pred, int ms)
{
    QElapsedTimer t;
    t.start();
    while (!pred()) {
        if (t.elapsed() > ms) return false;
        QTest::qWait(20);
    }
    return true;
}

void mouse(QWidget *w, QEvent::Type type, QPoint at, Qt::MouseButtons buttons,
           Qt::KeyboardModifiers mods = Qt::NoModifier)
{
    const Qt::MouseButton button =
        type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton;
    QMouseEvent e(type, QPointF(at), w->mapToGlobal(QPointF(at)), button, buttons, mods);
    QApplication::sendEvent(w, &e);
}

void drag(QWidget *w, QPoint from, QPoint to, Qt::KeyboardModifiers mods = Qt::NoModifier)
{
    mouse(w, QEvent::MouseButtonPress, from, Qt::LeftButton, mods);
    mouse(w, QEvent::MouseMove, (from + to) / 2, Qt::LeftButton, mods);
    mouse(w, QEvent::MouseMove, to, Qt::LeftButton, mods);
    mouse(w, QEvent::MouseButtonRelease, to, Qt::NoButton, mods);
}

QString videoFile() { return qEnvironmentVariable("QUEWI_TEST_VIDEO_FILE"); }

// The editor without a file, for the splice tests: the timeline is told a
// duration so the marks have somewhere to go; Out stands in for the end of
// the file.
struct Bench {
    video::VideoCue vc;
    QUndoStack undo;
    ui::VideoEditorWindow w{&vc, &undo};
    Bench()
    {
        w.setAttribute(Qt::WA_DeleteOnClose, false);
        vc.setField(QStringLiteral("trimOutSeconds"), 8.0);
        w.timeline()->setDuration(10.0);
        w.resize(1280, 820);
        w.show();
        QApplication::processEvents();
    }
};

} // namespace

// The video editor and what it edits: a video cue's In / Out (shared with
// its sound) and picture fades, how a playing picture honours them, the
// timeline's handles, and the editor window. The parts that need a real
// video file skip unless QUEWI_TEST_VIDEO_FILE points at one (≥ 8 s, with
// sound) — CI has none.
class VideoEditorTests : public QObject {
    Q_OBJECT
private slots:
    // ── PictureTiming ──────────────────────────────────────────────────
    void envelopeWithoutFadesIsFull()
    {
        video::PictureTiming t;
        QCOMPARE(t.envelope(0.0, 10.0, true), 1.0);
        QCOMPARE(t.envelope(9.99, 10.0, true), 1.0);
        QVERIFY(!t.isTrimmed());
    }

    void fadeInRampsFromIn()
    {
        video::PictureTiming t;
        t.inSeconds = 2.0;
        t.fadeInSeconds = 1.0;
        QCOMPARE(t.envelope(2.0, 10.0, true), 0.0);
        QCOMPARE(t.envelope(2.5, 10.0, true), 0.5);
        QCOMPARE(t.envelope(3.0, 10.0, true), 1.0);
        // Only the first pass fades in.
        QCOMPARE(t.envelope(2.0, 10.0, false), 1.0);
    }

    void fadeOutRampsToOut()
    {
        video::PictureTiming t;
        t.outSeconds = 8.0;
        t.fadeOutSeconds = 2.0;
        QCOMPARE(t.endSeconds(10.0), 8.0);
        QCOMPARE(t.envelope(6.0, 10.0, true), 1.0);
        QCOMPARE(t.envelope(7.0, 10.0, true), 0.5);
        QCOMPARE(t.envelope(8.0, 10.0, true), 0.0);
        // No Out: the fade runs into the end of the file.
        t.outSeconds = 0.0;
        QCOMPARE(t.envelope(9.0, 10.0, true), 0.5);
        // A loop has no last pass to fade out of.
        t.loop = true;
        QCOMPARE(t.envelope(9.9, 10.0, true), 1.0);
    }

    void outBeyondTheFileIsTheEnd()
    {
        video::PictureTiming t;
        t.outSeconds = 30.0;
        QCOMPARE(t.endSeconds(10.0), 10.0);
        QCOMPARE(t.endSeconds(0.0), 30.0);   // duration not known yet
    }

    // ── VideoCue ───────────────────────────────────────────────────────
    void trimsAreTheSoundsTrims()
    {
        video::VideoCue vc;
        vc.setField(QStringLiteral("trimInSeconds"), 1.5);
        vc.setField(QStringLiteral("trimOutSeconds"), 7.25);
        QCOMPARE(vc.sound()->trimInSeconds(), 1.5);
        QCOMPARE(vc.sound()->trimOutSeconds(), 7.25);
        // …and the other way: the audio editor / Inspector set the sound's.
        vc.setField(QStringLiteral("sound.trimInSeconds"), 2.0);
        QCOMPARE(vc.trimInSeconds(), 2.0);
        QCOMPARE(vc.field(QStringLiteral("trimInSeconds")).toDouble(), 2.0);
        const auto t = vc.pictureTiming();
        QCOMPARE(t.inSeconds, 2.0);
        QCOMPARE(t.outSeconds, 7.25);
        // Negative trims are nonsense: clamp.
        vc.setField(QStringLiteral("trimInSeconds"), -3.0);
        QCOMPARE(vc.trimInSeconds(), 0.0);
    }

    void pictureFadesSaveAndLoad()
    {
        video::VideoCue a;
        a.setField(QStringLiteral("filePath"), QStringLiteral("/show/song.mov"));
        a.setField(QStringLiteral("pictureFadeInSeconds"), 1.25);
        a.setField(QStringLiteral("pictureFadeOutSeconds"), 3.0);
        a.setField(QStringLiteral("trimInSeconds"), 4.0);
        QSignalSpy changed(&a, &cues::Cue::changed);
        a.setField(QStringLiteral("pictureFadeInSeconds"), 1.25);   // same value
        QCOMPARE(changed.count(), 0);

        video::VideoCue b;
        b.fromPayload(a.toPayload());
        QCOMPARE(b.pictureFadeInSeconds(), 1.25);
        QCOMPARE(b.pictureFadeOutSeconds(), 3.0);
        QCOMPARE(b.trimInSeconds(), 4.0);

        // A show from before picture fades: none.
        QJsonObject old = a.toPayload();
        old.remove(QStringLiteral("pictureFadeInSeconds"));
        old.remove(QStringLiteral("pictureFadeOutSeconds"));
        video::VideoCue c;
        c.fromPayload(old);
        QCOMPARE(c.pictureFadeInSeconds(), 0.0);
        QCOMPARE(c.pictureFadeOutSeconds(), 0.0);
    }

    void fireParamsCarryTheTiming()
    {
        video::VideoCue vc;
        vc.setField(QStringLiteral("filePath"), QStringLiteral("/show/song.mov"));
        vc.setField(QStringLiteral("loop"), true);
        vc.setField(QStringLiteral("trimInSeconds"), 1.0);
        vc.setField(QStringLiteral("trimOutSeconds"), 5.0);
        vc.setField(QStringLiteral("pictureFadeInSeconds"), 0.5);
        vc.setField(QStringLiteral("opacity"), 0.8);
        const auto p = video::voiceParamsFor(vc);
        QCOMPARE(p.kind, video::VideoVoiceParams::Video);
        QCOMPARE(p.filePath, QStringLiteral("/show/song.mov"));
        QVERIFY(p.loop);
        QCOMPARE(p.timing.inSeconds, 1.0);
        QCOMPARE(p.timing.outSeconds, 5.0);
        QCOMPARE(p.timing.fadeInSeconds, 0.5);
        QCOMPARE(p.opacity, 0.8);
    }

    // ── VideoTimeline ──────────────────────────────────────────────────
    void draggingTheBarsSetsInAndOut()
    {
        ui::VideoTimeline tl;
        tl.resize(876, tl.height());   // 792 px of track: 79.2 px a second
        tl.setDuration(10.0);
        tl.setTrims(0.0, 0.0);
        QSignalSpy in(&tl, &ui::VideoTimeline::trimInChanged);
        QSignalSpy out(&tl, &ui::VideoTimeline::trimOutChanged);
        QSignalSpy done(&tl, &ui::VideoTimeline::editingFinished);
        const int y = tl.pictureLane().center().y();

        drag(&tl, {tl.xForSeconds(0.0), y}, {tl.xForSeconds(2.0), y});
        QVERIFY(!in.isEmpty());
        QVERIFY(std::abs(in.last().at(0).toDouble() - 2.0) < 0.02);
        QCOMPARE(done.count(), 1);

        drag(&tl, {tl.xForSeconds(10.0), y}, {tl.xForSeconds(7.0), y});
        QVERIFY(std::abs(out.last().at(0).toDouble() - 7.0) < 0.02);
        // Back to the end of the file = no Out point at all.
        drag(&tl, {tl.xForSeconds(7.0), y}, {tl.xForSeconds(10.0) + 40, y});
        QCOMPARE(out.last().at(0).toDouble(), 0.0);
    }

    void outCantPassIn()
    {
        ui::VideoTimeline tl;
        tl.resize(876, tl.height());
        tl.setDuration(10.0);
        tl.setTrims(4.0, 6.0);
        QSignalSpy out(&tl, &ui::VideoTimeline::trimOutChanged);
        const int y = tl.soundLane().center().y();
        drag(&tl, {tl.xForSeconds(6.0), y}, {tl.xForSeconds(1.0), y});
        QVERIFY(out.last().at(0).toDouble() > 4.0);
    }

    void fadeHandlesSitOnTheLaneTops()
    {
        ui::VideoTimeline tl;
        tl.resize(876, tl.height());
        tl.setDuration(10.0);
        tl.setTrims(1.0, 9.0);
        QSignalSpy picIn(&tl, &ui::VideoTimeline::pictureFadeInChanged);
        QSignalSpy sndOut(&tl, &ui::VideoTimeline::soundFadeOutChanged);
        QSignalSpy trimIn(&tl, &ui::VideoTimeline::trimInChanged);
        const int picTop = tl.pictureLane().top() + 3;
        const int sndTop = tl.soundLane().top() + 3;

        // With no fade the handle sits on the In bar's top: drag it right.
        drag(&tl, {tl.xForSeconds(1.0), picTop}, {tl.xForSeconds(2.5), picTop});
        QVERIFY(std::abs(picIn.last().at(0).toDouble() - 1.5) < 0.02);
        QVERIFY(trimIn.isEmpty());    // the fade, not the trim

        drag(&tl, {tl.xForSeconds(9.0), sndTop}, {tl.xForSeconds(8.0), sndTop});
        QVERIFY(std::abs(sndOut.last().at(0).toDouble() - 1.0) < 0.02);
    }

    void clickingElsewhereSeeks()
    {
        ui::VideoTimeline tl;
        tl.resize(876, tl.height());
        tl.setDuration(10.0);
        QSignalSpy seek(&tl, &ui::VideoTimeline::seekRequested);
        const QPoint at(tl.xForSeconds(5.0), tl.pictureLane().center().y());
        mouse(&tl, QEvent::MouseButtonPress, at, Qt::LeftButton);
        mouse(&tl, QEvent::MouseButtonRelease, at, Qt::NoButton);
        QCOMPARE(seek.count(), 1);
        QVERIFY(std::abs(seek.last().at(0).toDouble() - 5.0) < 0.02);
    }

    // ── VideoEditorWindow ──────────────────────────────────────────────
    void editsAreUndoableCueEdits()
    {
        video::VideoCue vc;
        QUndoStack undo;
        {
            ui::VideoEditorWindow w(&vc, &undo);
            w.setAttribute(Qt::WA_DeleteOnClose, false);
            emit w.timeline()->pictureFadeOutChanged(2.0);
            emit w.timeline()->soundFadeInChanged(0.5);
            emit w.timeline()->trimInChanged(1.0);
        }
        QCOMPARE(vc.pictureFadeOutSeconds(), 2.0);
        QCOMPARE(vc.sound()->fadeInSeconds(), 0.5);
        QCOMPARE(vc.trimInSeconds(), 1.0);
        QCOMPARE(undo.count(), 3);
        undo.undo();
        QCOMPARE(vc.trimInSeconds(), 0.0);
        undo.undo();
        undo.undo();
        QCOMPARE(vc.pictureFadeOutSeconds(), 0.0);
    }

    void editorFollowsTheCue()
    {
        video::VideoCue vc;
        ui::VideoEditorWindow w(&vc, nullptr);
        w.setAttribute(Qt::WA_DeleteOnClose, false);
        // Edited elsewhere (Inspector, OSC, undo): the editor shows it.
        vc.setField(QStringLiteral("trimInSeconds"), 3.0);
        vc.setField(QStringLiteral("trimOutSeconds"), 6.0);
        QCOMPARE(w.timeline()->endSeconds(), 6.0);
    }

    void editorClosesWhenItsCueGoes()
    {
        auto *vc = new video::VideoCue;
        auto *w = new ui::VideoEditorWindow(vc, nullptr);
        QPointer<ui::VideoEditorWindow> guard(w);
        w->show();
        delete vc;
        QVERIFY(waitFor([&] { return guard.isNull(); }, 2000));   // WA_DeleteOnClose
    }

    // ── Splicing ───────────────────────────────────────────────────────
    void deletingASelectionCutsItOut()
    {
        Bench b;
        b.w.selectRange(2.0, 4.0);
        QVERIFY(b.w.timeline()->hasSelection());
        b.w.deleteSelection();
        QCOMPARE(b.vc.cuts().size(), size_t(1));
        QCOMPARE(b.vc.cuts()[0].start, 2.0);
        QCOMPARE(b.vc.cuts()[0].end, 4.0);
        QCOMPARE(b.undo.count(), 1);                  // one step
        QVERIFY(!b.w.timeline()->hasSelection());
        QCOMPARE(b.w.timeline()->cuts().size(), size_t(1));   // drawn

        // A second cut is its own step, not merged into the first.
        b.w.selectRange(6.0, 7.0);
        b.w.deleteSelection();
        QCOMPARE(b.undo.count(), 2);
        QCOMPARE(b.vc.cuts().size(), size_t(2));
        b.undo.undo();
        QCOMPARE(b.vc.cuts().size(), size_t(1));

        // Restore section puts it back.
        b.w.restoreCut(2.0, 4.0);
        QVERIFY(b.vc.cuts().empty());
        b.undo.undo();
        QCOMPARE(b.vc.cuts().size(), size_t(1));
        b.w.restoreAllCuts();
        QVERIFY(b.vc.cuts().empty());
    }

    void razorSplitThenDeleteCutsTheSegment()
    {
        Bench b;
        auto *tl = b.w.timeline();
        QVERIFY(tl->width() > 600);
        b.w.setTool(ui::VideoTimeline::Tool::Razor);
        const QPoint at3(tl->xForSeconds(3.0), tl->pictureLane().center().y());
        mouse(tl, QEvent::MouseButtonPress, at3, Qt::LeftButton);
        mouse(tl, QEvent::MouseButtonRelease, at3, Qt::NoButton);
        QCOMPARE(tl->splitPoints().size(), 1);
        QVERIFY(std::abs(tl->splitPoints()[0] - 3.0) < 0.02);

        // Back to Select: a click in the segment picks it, split → Out.
        b.w.setTool(ui::VideoTimeline::Tool::Select);
        const QPoint at5(tl->xForSeconds(5.0), tl->soundLane().center().y());
        mouse(tl, QEvent::MouseButtonPress, at5, Qt::LeftButton);
        mouse(tl, QEvent::MouseButtonRelease, at5, Qt::NoButton);
        QVERIFY(tl->hasSelection());
        QVERIFY(std::abs(tl->selectionStart() - 3.0) < 0.02);
        QCOMPARE(tl->selectionEnd(), 8.0);

        QTest::keyClick(&b.w, Qt::Key_Delete);
        QCOMPARE(b.vc.cuts().size(), size_t(1));
        QVERIFY(std::abs(b.vc.cuts()[0].start - 3.0) < 0.02);
        QCOMPARE(b.vc.cuts()[0].end, 8.0);
        QVERIFY(tl->splitPoints().isEmpty());          // used up
    }

    void draggingInALaneSelectsARange()
    {
        Bench b;
        auto *tl = b.w.timeline();
        const int y = tl->pictureLane().center().y();
        drag(tl, {tl->xForSeconds(5.0), y}, {tl->xForSeconds(6.0), y});
        QVERIFY(tl->hasSelection());
        QVERIFY(std::abs(tl->selectionStart() - 5.0) < 0.05);
        QVERIFY(std::abs(tl->selectionEnd() - 6.0) < 0.05);
        QTest::keyClick(&b.w, Qt::Key_Escape);
        QVERIFY(!tl->hasSelection());
    }

    void keepOnlyThisTrimsToTheSelection()
    {
        Bench b;
        b.w.selectRange(2.0, 4.0);
        b.w.keepOnlySelection();
        QCOMPARE(b.vc.trimInSeconds(), 2.0);
        QCOMPARE(b.vc.trimOutSeconds(), 4.0);
        QCOMPARE(b.undo.count(), 1);                  // one step for both
        b.undo.undo();
        QCOMPARE(b.vc.trimInSeconds(), 0.0);
        QCOMPARE(b.vc.trimOutSeconds(), 8.0);
    }

    void playsForCountsTheCutsOut()
    {
        Bench b;
        b.vc.setField(QStringLiteral("trimInSeconds"), 1.0);
        QCOMPARE(b.w.playsForSeconds(), 7.0);
        b.w.selectRange(2.0, 4.0);
        b.w.deleteSelection();
        QCOMPARE(b.w.playsForSeconds(), 5.0);
        QVERIFY(b.w.inspector()->playsForLabel->text().contains(QStringLiteral("0:05.00")));
    }

    // ── With a real video file ─────────────────────────────────────────
    void previewPlaybackJumpsOverCuts()
    {
        if (videoFile().isEmpty()) QSKIP("set QUEWI_TEST_VIDEO_FILE to a video (>= 8 s)");
        video::VideoCue vc;
        vc.setField(QStringLiteral("filePath"), videoFile());
        QJsonArray cuts;
        cuts.append(QJsonArray{2.0, 4.0});
        vc.setField(QStringLiteral("cuts"), cuts);
        QCOMPARE(vc.cuts().size(), size_t(1));
        QUndoStack undo;
        ui::VideoEditorWindow w(&vc, &undo);
        w.setAttribute(Qt::WA_DeleteOnClose, false);
        w.show();
        QVERIFY(waitFor([&] { return w.durationSeconds() > 0.0; }, 5000));

        w.seekTo(1.5);
        w.togglePlay();
        QVERIFY(waitFor([&] { return w.isPlaying(); }, 3000));
        bool insideCut = false;
        QElapsedTimer clock;
        clock.start();
        while (w.playheadSeconds() < 4.2 && clock.elapsed() < 6000) {
            QTest::qWait(15);
            const double p = w.playheadSeconds();
            if (p > 2.2 && p < 3.9) insideCut = true;
        }
        QVERIFY2(w.playheadSeconds() >= 4.2, qPrintable(QString::number(w.playheadSeconds())));
        QVERIFY(!insideCut);
        w.togglePlay();
    }

    void editorMarksInAndOutFromThePlayhead()
    {
        if (videoFile().isEmpty()) QSKIP("set QUEWI_TEST_VIDEO_FILE to a video (>= 8 s)");
        video::VideoCue vc;
        vc.setField(QStringLiteral("filePath"), videoFile());
        QUndoStack undo;
        ui::VideoEditorWindow w(&vc, &undo);
        w.setAttribute(Qt::WA_DeleteOnClose, false);
        w.show();
        QVERIFY(waitFor([&] { return w.durationSeconds() > 0.0; }, 5000));
        QCOMPARE(w.timeline()->duration(), w.durationSeconds());

        w.seekTo(2.0);
        w.setInAtPlayhead();
        w.seekTo(5.0);
        w.setOutAtPlayhead();
        QCOMPARE(vc.trimInSeconds(), 2.0);
        QCOMPARE(vc.trimOutSeconds(), 5.0);
        // A frame step at the file's own rate.
        w.stepFrames(1);
        QVERIFY(w.playheadSeconds() > 5.0 && w.playheadSeconds() < 5.1);

        // Play from outside the trims starts at In and stops at Out.
        w.seekTo(0.5);
        w.togglePlay();
        QVERIFY(waitFor([&] { return w.isPlaying(); }, 3000));
        QVERIFY(w.playheadSeconds() >= 1.9);
        QVERIFY(waitFor([&] { return !w.isPlaying(); }, 8000));
        QVERIFY(std::abs(w.playheadSeconds() - 5.0) < 0.1);
    }

    // Space plays / pauses whatever has focus in the editor — a focused
    // number box (In) used to swallow it, a focused button to press itself.
    void spaceTogglesPlayFromAnyFocus()
    {
        if (videoFile().isEmpty()) QSKIP("set QUEWI_TEST_VIDEO_FILE to a video (>= 8 s)");
        video::VideoCue vc;
        vc.setField(QStringLiteral("filePath"), videoFile());
        QUndoStack undo;
        ui::VideoEditorWindow w(&vc, &undo);
        w.setAttribute(Qt::WA_DeleteOnClose, false);
        w.show();
        w.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&w));
        QVERIFY(waitFor([&] { return w.durationSeconds() > 0.0; }, 5000));

        auto press = [&] {
            QWidget *fw = QApplication::focusWidget();
            QVERIFY(fw);
            QTest::keyClick(fw, Qt::Key_Space);
        };
        press();                                           // the timeline has focus
        QVERIFY(waitFor([&] { return w.isPlaying(); }, 3000));
        auto *inBox = w.findChildren<QDoubleSpinBox *>().value(0);
        QVERIFY(inBox);
        inBox->setFocus();
        QVERIFY(waitFor([&] { return QApplication::focusWidget() != nullptr; }, 500));
        const double inBefore = vc.trimInSeconds();
        press();                                           // focus in a number box
        QVERIFY(waitFor([&] { return !w.isPlaying(); }, 1000));
        QCOMPARE(vc.trimInSeconds(), inBefore);            // and In wasn't touched
        for (auto *b : w.findChildren<QPushButton *>()) {  // a focused button
            if (!b->isVisible()) continue;
            b->setFocus();
            break;
        }
        press();
        QVERIFY(waitFor([&] { return w.isPlaying(); }, 3000));
        w.togglePlay();
    }

    void thumbnailsFillTheStrip()
    {
        if (videoFile().isEmpty()) QSKIP("set QUEWI_TEST_VIDEO_FILE to a video (>= 8 s)");
        ui::VideoThumbnailer th;
        QList<double> times;
        connect(&th, &ui::VideoThumbnailer::thumbnailReady, this,
                [&](double t, const QImage &img) {
                    QVERIFY(!img.isNull());
                    QCOMPARE(img.height(), 72);
                    times << t;
                });
        QSignalSpy done(&th, &ui::VideoThumbnailer::finished);
        th.start(videoFile(), 1.0, 20, 72);
        QVERIFY(done.wait(20000));
        QVERIFY2(times.size() >= 8, qPrintable(QString::number(times.size())));
    }

    void playingPictureStartsAtInAndEndsAtOut()
    {
        if (videoFile().isEmpty()) QSKIP("set QUEWI_TEST_VIDEO_FILE to a video (>= 8 s)");
        video::PictureTiming t;
        t.inSeconds = 3.0;
        t.outSeconds = 5.0;
        t.fadeInSeconds = 1.0;
        video::VideoLayer layer(videoFile(), t);
        QSignalSpy finished(&layer, &video::Layer::finished);
        QVERIFY(waitFor([&] { return layer.positionMs() > 0; }, 5000));
        QVERIFY2(layer.positionMs() >= 2900, qPrintable(QString::number(layer.positionMs())));
        // Early on, the fade in has the picture part-way up.
        QVERIFY(layer.envelope() < 0.9);
        QVERIFY(finished.wait(6000));
        QVERIFY(layer.positionMs() >= 4900 && layer.positionMs() < 5500);
        QCOMPARE(finished.count(), 1);
    }

    void trimmedLoopWrapsToIn()
    {
        if (videoFile().isEmpty()) QSKIP("set QUEWI_TEST_VIDEO_FILE to a video (>= 8 s)");
        video::PictureTiming t;
        t.inSeconds = 1.0;
        t.outSeconds = 2.0;
        t.loop = true;
        video::VideoLayer layer(videoFile(), t);
        QSignalSpy finished(&layer, &video::Layer::finished);
        // Played past Out, came back round: never beyond Out for long, never
        // before In, never "finished".
        qint64 maxSeen = 0;
        bool wrapped = false;
        qint64 last = 0;
        QElapsedTimer clock;
        clock.start();
        while (clock.elapsed() < 3500) {
            QTest::qWait(25);
            const qint64 p = layer.positionMs();
            if (p == 0) continue;
            if (last > 1700 && p < 1300) wrapped = true;
            last = p;
            maxSeen = std::max(maxSeen, p);
        }
        QVERIFY(wrapped);
        QVERIFY2(maxSeen < 2300, qPrintable(QString::number(maxSeen)));
        QCOMPARE(finished.count(), 0);
    }
};

QTEST_MAIN(VideoEditorTests)
#include "test_video_editor.moc"
