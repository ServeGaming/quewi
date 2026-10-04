#include <QTest>
#include <QApplication>
#include <QDir>
#include <QLabel>
#include <QPushButton>
#include <QSignalSpy>

#include <cmath>

#include "ui/LightingPanel.h"
#include "ui/ShowModeView.h"
#include "ui/Theme.h"

using namespace quewi;
using namespace quewi::ui;

// The stage-manager screen and the Lighting dock, fed ShowSnapshots by
// hand: the standby hero, GO's label, the hit countdown, the desk's cues,
// every empty / paused / disarmed state, and the buttons' signals. No
// timers are needed — setSnapshot renders synchronously.
//
// Set QUEWI_RENDER_DIR to a folder to also write PNGs of both widgets at
// the sizes the design was checked at (used while iterating on the look).
class ShowModeViewTests : public QObject {
    Q_OBJECT

    static ShowCueLine cue(const char *number, const char *name, const char *type,
                           const char *notes = "", double duration = 0.0)
    {
        ShowCueLine c;
        c.id = QUuid::createUuid();
        c.number = QString::fromUtf8(number);
        c.name = QString::fromUtf8(name);
        c.type = QString::fromUtf8(type);
        c.notes = QString::fromUtf8(notes);
        c.durationSeconds = duration;
        return c;
    }

    static ShowUpcomingHit hit(const char *name, const char *does, double in, bool paused = false)
    {
        ShowUpcomingHit h;
        h.triggerId = QUuid::createUuid();
        h.cueNumber = QStringLiteral("12");
        h.cueName = QStringLiteral("Defying Gravity");
        h.name = QString::fromUtf8(name);
        h.does = QString::fromUtf8(does);
        h.inSeconds = in;
        h.paused = paused;
        return h;
    }

    // Mid-act: a song playing with hits coming, the next cue on standby, the
    // desk live.
    static ShowSnapshot live()
    {
        ShowSnapshot s;
        s.showName = QStringLiteral("Wicked — Act 1");
        s.listName = QStringLiteral("Main cue list");
        s.position = 13;
        s.cueCount = 40;
        auto sb = cue("13", "Thunder + blackout", "Audio",
                      "STANDBY on Elphaba's last note — \"...and nobody in all of Oz\". "
                      "GO as she raises the broom. Flys come in on the thunder.", 8.0);
        sb.colour = QColor(0x4F, 0x8E, 0xAF);
        sb.preWaitSeconds = 1.5;
        sb.autoContinue = true;
        sb.lightTriggers = 3;
        sb.firstTriggerSeconds = 4.0;
        s.standby = sb;
        auto c14 = cue("14", "Act 1 curtain", "Video", "House to half as the tabs close.", 12.0);
        c14.colour = QColor(0xC2, 0x6A, 0x55);
        s.comingUp = {c14,
                      cue("15", "Interval loop", "Audio", "Loops until the half. Fade on the 5.", 180.0),
                      cue("16", "Interval announcement", "Memo", "Call at 5 minutes."),
                      cue("17", "House lights preset", "Light")};
        s.lastFired = cue("12", "Defying Gravity", "Audio");
        ShowRunningCue r;
        r.cue = cue("12", "Defying Gravity", "Audio", "", 214.0);
        r.cue.colour = QColor(0x6F, 0xAE, 0x63);
        r.elapsedSeconds = 171.4;
        r.remainingSeconds = 42.6;
        r.progress = 171.4 / 214.0;
        s.running = {r};
        ShowRunningCue bed;
        bed.cue = cue("9.5", "Storm bed", "Audio");
        bed.elapsedSeconds = 402.0;
        bed.looping = true;
        s.running.push_back(bed);
        s.hits = {hit("Lightning 1", "Desk: GO", 3.2), hit("Lightning 2", "Desk: GO", 7.5),
                  hit("Strobe on", "Desk: bump sub 3", 12.0), hit("Strobe on", "Desk: sub 3 → 0", 14.0)};
        s.hits.back().exit = true;
        s.desk.link = ShowDeskStatus::Link::Live;
        s.desk.deskName = QStringLiteral("ETC Eos");
        s.desk.address = QStringLiteral("192.168.1.40:3032");
        s.desk.showName = QStringLiteral("Wicked_v7");
        s.desk.activeList = QStringLiteral("1");
        s.desk.activeCue = QStringLiteral("112");
        s.desk.activeLabel = QStringLiteral("Gravity build");
        s.desk.activeProgress = 0.62;
        s.desk.activeTime = QStringLiteral("8.00");
        s.desk.pendingList = QStringLiteral("1");
        s.desk.pendingCue = QStringLiteral("113");
        s.desk.pendingLabel = QStringLiteral("Lightning");
        return s;
    }

    static ShowSnapshot endOfList()
    {
        ShowSnapshot s;
        s.showName = QStringLiteral("Wicked — Act 1");
        s.listName = QStringLiteral("Main cue list");
        s.position = 0;
        s.cueCount = 40;
        s.lastFired = cue("40", "Walk-out music", "Audio");
        ShowRunningCue r;
        r.cue = *s.lastFired;
        r.elapsedSeconds = 30.0;
        r.remainingSeconds = 150.0;
        r.progress = 30.0 / 180.0;
        s.running = {r};
        s.desk.link = ShowDeskStatus::Link::Off;
        return s;
    }

    static ShowSnapshot pausedDisarmed()
    {
        ShowSnapshot s = live();
        s.paused = true;
        s.triggersArmed = false;
        for (auto &r : s.running) r.paused = true;
        for (auto &h : s.hits) h.paused = true;
        return s;
    }

    static ShowSnapshot deskFailed()
    {
        ShowSnapshot s = live();
        s.running.clear();
        s.hits.clear();
        s.desk = {};
        s.desk.link = ShowDeskStatus::Link::Failed;
        s.desk.deskName = QStringLiteral("ETC Eos");
        s.desk.address = QStringLiteral("192.168.1.40:3032");
        s.desk.detail = QStringLiteral("Connection refused — is OSC TCP on at the desk?");
        return s;
    }

    template <typename T>
    static T *child(QWidget &w, const char *name)
    {
        auto *c = w.findChild<T *>(QString::fromLatin1(name));
        if (!c) qWarning("no child %s", name);
        return c;
    }
    static QString text(QWidget &w, const char *name)
    {
        if (auto *l = w.findChild<QLabel *>(QString::fromLatin1(name))) return l->text();
        if (auto *b = w.findChild<QPushButton *>(QString::fromLatin1(name))) return b->text();
        qWarning("no label/button %s", name);
        return {};
    }
    static bool shown(QWidget &w, const char *name)
    {
        auto *c = w.findChild<QWidget *>(QString::fromLatin1(name));
        return c && !c->isHidden();
    }

private slots:
    void initTestCase()
    {
        // The QSS lives in the app's resources (not linked here); the
        // widgets style themselves, so only the palette matters.
        Theme::load(QStringLiteral("quewi-dark"));
        qApp->setPalette(Theme::palette());
    }

    void textHelpers()
    {
        QCOMPARE(showClockText(0.0), QStringLiteral("0:00"));
        QCOMPARE(showClockText(65.0), QStringLiteral("1:05"));
        QCOMPARE(showClockText(3600.0), QStringLiteral("1:00:00"));
        QCOMPARE(showCountdownText(3.24), QStringLiteral("0:03.2"));
        QCOMPARE(showCountdownText(0.0), QStringLiteral("0:00.0"));
        QCOMPARE(showCountdownText(-2.0), QStringLiteral("0:00.0"));
        QCOMPARE(showCountdownText(75.0), QStringLiteral("1:15.0"));
        QCOMPARE(showRemainingText(42.6), QStringLiteral("-0:43"));
        QCOMPARE(showRemainingText(-1.0), QString());
        QCOMPARE(showDeskLinkText(ShowDeskStatus::Link::Live), QStringLiteral("Live"));
    }

    // The bar's target is always exactly what was set; the drawn value only
    // glides while the bar is on screen, and snaps for a new item, a
    // restart, or an unknown value.
    void progressBarGlides()
    {
        ThinProgressBar bar;
        bar.setProgress(0.25, QStringLiteral("a"));
        QCOMPARE(bar.progress(), 0.25);
        QCOMPARE(bar.shownProgress(), 0.25);        // hidden: no glide
        QVERIFY(!bar.isGliding());
        bar.resize(300, 4);
        bar.show();
        QVERIFY(QTest::qWaitForWindowExposed(&bar));
        bar.setProgress(0.30, QStringLiteral("a"));
        QCOMPARE(bar.progress(), 0.30);
        QVERIFY(bar.isGliding());
        QTRY_VERIFY(!bar.isGliding());              // settles on its own…
        QVERIFY(std::abs(bar.shownProgress() - 0.30) < 0.02);   // …at (or just past) the target
        // Steady rise, 0.01 per 100 ms: the drawn value stays within 150 ms'
        // worth of the target (never behind it by more than a frame or two).
        for (int i = 1; i <= 5; ++i) {
            bar.setProgress(0.30 + 0.01 * i, QStringLiteral("a"));
            QTest::qWait(100);
            const double diff = bar.shownProgress() - bar.progress();
            QVERIFY2(diff > -0.004 && diff < 0.016,
                     qPrintable(QStringLiteral("shown %1 vs target %2").arg(bar.shownProgress()).arg(bar.progress())));
        }
        // A restart (big step back) snaps.
        bar.setProgress(0.02, QStringLiteral("a"));
        QCOMPARE(bar.shownProgress(), 0.02);
        QVERIFY(!bar.isGliding());
        // A different item snaps, even forwards.
        bar.setProgress(0.90, QStringLiteral("b"));
        QCOMPARE(bar.shownProgress(), 0.90);
        QVERIFY(!bar.isGliding());
        // Unknown empties the track at once.
        bar.setProgress(-1.0, QStringLiteral("b"));
        QCOMPARE(bar.progress(), -1.0);
        QCOMPARE(bar.shownProgress(), -1.0);
        // Hidden mid-glide: the animation stops and the drawn value lands.
        bar.setProgress(0.10, QStringLiteral("c"));
        bar.setProgress(0.20, QStringLiteral("c"));
        QVERIFY(bar.isGliding());
        bar.hide();
        QVERIFY(!bar.isGliding());
        QCOMPARE(bar.shownProgress(), 0.20);
    }

    void standbyHero()
    {
        ShowModeView v;
        v.resize(1280, 720);
        v.setSnapshot(live());
        QCOMPARE(text(v, "smStandbyNumber"), QStringLiteral("13"));
        QCOMPARE(text(v, "smStandbyName"), QStringLiteral("Thunder + blackout"));
        QVERIFY(text(v, "smStandbyNotes").startsWith(QStringLiteral("STANDBY on Elphaba")));
        QVERIFY(text(v, "smStandbyMeta").contains(QStringLiteral("Audio")));
        QVERIFY(text(v, "smStandbyMeta").contains(QStringLiteral("pre-wait 1.5 s")));
        QVERIFY(text(v, "smStandbyMeta").contains(QStringLiteral("AUTO-CONTINUE")));
        QCOMPARE(text(v, "smStandbyHits"), QStringLiteral("3 lighting hits  ·  first 0:04 after GO"));
        QVERIFY(shown(v, "smStandbyHits"));
        QVERIFY(!shown(v, "smStandbyEmpty"));
        QCOMPARE(text(v, "smGo"), QStringLiteral("GO  13"));
        QVERIFY(child<QPushButton>(v, "smGo")->isEnabled());
        QCOMPARE(text(v, "smPosition"), QStringLiteral("Cue 13 of 40"));
        QVERIFY(text(v, "smShow").contains(QStringLiteral("Wicked")));
        QVERIFY(text(v, "smShow").contains(QStringLiteral("Main cue list")));
    }

    void endOfListState()
    {
        ShowModeView v;
        v.resize(1280, 720);
        v.setSnapshot(endOfList());
        QVERIFY(shown(v, "smStandbyEmpty"));
        QVERIFY(!shown(v, "smStandbyNumber"));
        QVERIFY(!shown(v, "smStandbyHits"));
        QCOMPARE(text(v, "smGo"), QStringLiteral("GO"));
        QVERIFY(!child<QPushButton>(v, "smGo")->isEnabled());
        QCOMPARE(text(v, "smPosition"), QStringLiteral("End of 40 cues"));
        QVERIFY(text(v, "smLastFired").contains(QStringLiteral("40")));
        // No desk feedback: the card says so and hides the cue readouts.
        QCOMPARE(text(v, "smDeskLink"), QStringLiteral("No desk feedback"));
        QVERIFY(!shown(v, "smDeskActive"));
    }

    void runningAndHits()
    {
        ShowModeView v;
        v.resize(1280, 720);
        v.setSnapshot(live());
        auto *running = child<QWidget>(v, "smRunning");
        QVERIFY(running);
        const auto leads = running->findChildren<QLabel *>();
        bool sawRemaining = false, sawLoop = false;
        for (auto *l : leads) {
            if (l->text() == QStringLiteral("-0:43")) sawRemaining = true;
            if (l->text() == QStringLiteral("∞")) sawLoop = true;
        }
        QVERIFY(sawRemaining);
        QVERIFY(sawLoop);
        QCOMPARE(text(v, "smRunningState"), QStringLiteral("2 cues"));
        QCOMPARE(text(v, "smHitCountdown"), QStringLiteral("0:03.2"));
        QCOMPARE(text(v, "smHitName"), QStringLiteral("Lightning 1"));
        QVERIFY(text(v, "smHitDoes").contains(QStringLiteral("Desk: GO")));
        QVERIFY(text(v, "smHitDoes").contains(QStringLiteral("Defying Gravity")));
        QCOMPARE(text(v, "smArmed"), QStringLiteral("ARMED"));
        QVERIFY(child<QPushButton>(v, "smArmed")->isChecked());
        QVERIFY(!shown(v, "smHitsState"));
        QVERIFY(child<QPushButton>(v, "smPause")->isEnabled());
        QCOMPARE(text(v, "smPause"), QStringLiteral("Pause"));
        QVERIFY(child<QPushButton>(v, "smFadeAll")->isEnabled());
    }

    void nothingPlaying()
    {
        ShowModeView v;
        v.resize(1280, 720);
        v.setSnapshot(deskFailed());
        QCOMPARE(text(v, "smRunningState"), QStringLiteral("Nothing playing"));
        QVERIFY(!shown(v, "smHitCountdown"));
        QCOMPARE(text(v, "smHitsState"), QStringLiteral("Hits appear here while a song with lighting triggers plays."));
        QVERIFY(!child<QPushButton>(v, "smPause")->isEnabled());
        QVERIFY(!child<QPushButton>(v, "smFadeAll")->isEnabled());
    }

    void pausedAndDisarmed()
    {
        ShowModeView v;
        v.resize(1280, 720);
        v.setSnapshot(pausedDisarmed());
        QCOMPARE(text(v, "smRunningState"), QStringLiteral("PAUSED"));
        QCOMPARE(text(v, "smPause"), QStringLiteral("Resume"));
        QCOMPARE(text(v, "smArmed"), QStringLiteral("DISARMED"));
        QVERIFY(!child<QPushButton>(v, "smArmed")->isChecked());
        QVERIFY(text(v, "smHitsState").startsWith(QStringLiteral("Triggers disarmed")));
        QCOMPARE(child<QLabel>(v, "smHitCountdown")->property("tone").toString(), QStringLiteral("off"));
        // Re-arm the snapshot but keep it paused: the countdown is frozen and says so.
        auto s = pausedDisarmed();
        s.triggersArmed = true;
        v.setSnapshot(s);
        QCOMPARE(child<QLabel>(v, "smHitCountdown")->property("tone").toString(), QStringLiteral("paused"));
        QVERIFY(text(v, "smHitsState").startsWith(QStringLiteral("Paused")));
    }

    void deskCard()
    {
        ShowModeView v;
        v.resize(1280, 720);
        v.setSnapshot(live());
        QCOMPARE(text(v, "smDeskLink"), QStringLiteral("ETC Eos · Live"));
        QCOMPARE(text(v, "smDeskActive"), QStringLiteral("1/112"));
        QCOMPARE(text(v, "smDeskActiveLabel"), QStringLiteral("Gravity build"));
        QCOMPARE(text(v, "smDeskActiveTime"), QStringLiteral("8.00"));
        QVERIFY(text(v, "smDeskPending").startsWith(QStringLiteral("1/113")));
        QVERIFY(text(v, "smDeskDetail").contains(QStringLiteral("Wicked_v7")));
        QVERIFY(!shown(v, "smDeskBlind"));
        auto s = live();
        s.desk.blind = true;
        v.setSnapshot(s);
        QVERIFY(shown(v, "smDeskBlind"));

        v.setSnapshot(deskFailed());
        QCOMPARE(text(v, "smDeskLink"), QStringLiteral("ETC Eos · Failed"));
        QVERIFY(text(v, "smDeskDetail").contains(QStringLiteral("Connection refused")));
        QVERIFY(!shown(v, "smDeskActive"));
    }

    void buttonsEmitAndTakeNoFocus()
    {
        ShowModeView v;
        v.resize(1280, 720);
        v.setSnapshot(live());
        QSignalSpy go(&v, &ShowModeView::goPressed);
        QSignalSpy pause(&v, &ShowModeView::pausePressed);
        QSignalSpy fade(&v, &ShowModeView::fadeAllPressed);
        QSignalSpy panic(&v, &ShowModeView::panicPressed);
        QSignalSpy exit(&v, &ShowModeView::exitRequested);
        QSignalSpy armed(&v, &ShowModeView::armedToggled);
        for (const char *name : {"smGo", "smPause", "smFadeAll", "smPanic", "smExit", "smArmed", "smStopwatch"}) {
            auto *b = child<QPushButton>(v, name);
            QVERIFY2(b, name);
            QCOMPARE(b->focusPolicy(), Qt::NoFocus);
        }
        QCOMPARE(v.focusPolicy(), Qt::NoFocus);
        child<QPushButton>(v, "smGo")->click();
        child<QPushButton>(v, "smPause")->click();
        child<QPushButton>(v, "smFadeAll")->click();
        child<QPushButton>(v, "smPanic")->click();
        child<QPushButton>(v, "smExit")->click();
        QCOMPARE(go.count(), 1);
        QCOMPARE(pause.count(), 1);
        QCOMPARE(fade.count(), 1);
        QCOMPARE(panic.count(), 1);
        QCOMPARE(exit.count(), 1);
        // The armed chip: a click asks for the opposite of the snapshot; a
        // snapshot that already agrees doesn't echo it back.
        child<QPushButton>(v, "smArmed")->click();
        QCOMPARE(armed.count(), 1);
        QCOMPARE(armed.at(0).at(0).toBool(), false);
        auto s = live();
        s.triggersArmed = false;
        v.setSnapshot(s);
        QCOMPARE(armed.count(), 1);
        QCOMPARE(text(v, "smArmed"), QStringLiteral("DISARMED"));
        // Pause can be forbidden (QLab-style "no pause in show mode").
        v.setPauseAllowed(false);
        QVERIFY(!shown(v, "smPause"));
        v.setFadeAllAllowed(false);
        QVERIFY(!shown(v, "smFadeAll"));
    }

    void stopwatch()
    {
        ShowModeView v;
        QCOMPARE(v.stopwatchSeconds(), 0.0);
        QVERIFY(!v.stopwatchRunning());
        v.stopwatchToggle();
        QVERIFY(v.stopwatchRunning());
        QTest::qWait(120);
        v.stopwatchToggle();
        QVERIFY(!v.stopwatchRunning());
        const double paused = v.stopwatchSeconds();
        QVERIFY(paused >= 0.1);
        QTest::qWait(60);
        QCOMPARE(v.stopwatchSeconds(), paused);   // paused holds
        v.stopwatchReset();
        QCOMPARE(v.stopwatchSeconds(), 0.0);
        QVERIFY(text(v, "smStopwatch").contains(QStringLiteral("0:00")));
    }

    void providerIsPolledOnlyWhileVisible()
    {
        ShowModeView v;
        int calls = 0;
        v.setSnapshotProvider([&] { ++calls; return live(); });
        QCOMPARE(calls, 0);               // hidden: nothing pulled
        v.show();
        QVERIFY(QTest::qWaitForWindowExposed(&v));
        QTRY_VERIFY(calls >= 2);          // shown: the first pull + the poll
        v.hide();
        const int atHide = calls;
        QTest::qWait(250);
        QVERIFY(calls <= atHide + 1);     // hidden again: the poll stopped
        QCOMPARE(text(v, "smStandbyNumber"), QStringLiteral("13"));
    }

    void lightingPanelStates()
    {
        LightingPanel p;
        p.resize(300, 700);
        p.setSnapshot(live());
        QVERIFY(!p.isWideLayout());
        QCOMPARE(text(p, "lpDeskLink"), QStringLiteral("ETC Eos · Live"));
        QVERIFY(text(p, "lpDeskDetail").contains(QStringLiteral("192.168.1.40:3032")));
        QCOMPARE(text(p, "lpDeskActive"), QStringLiteral("1/112"));
        QCOMPARE(text(p, "lpDeskActiveLabel"), QStringLiteral("Gravity build"));
        QVERIFY(text(p, "lpDeskPending").startsWith(QStringLiteral("1/113")));
        QCOMPARE(text(p, "lpHitCountdown"), QStringLiteral("0:03.2"));
        QCOMPARE(text(p, "lpHitName"), QStringLiteral("Lightning 1"));
        QCOMPARE(text(p, "lpArmed"), QStringLiteral("ARMED"));
        QVERIFY(!shown(p, "lpHitsState"));
        QVERIFY(!shown(p, "lpDeskEmpty"));

        p.setSnapshot(pausedDisarmed());
        QCOMPARE(text(p, "lpArmed"), QStringLiteral("DISARMED"));
        QCOMPARE(child<QLabel>(p, "lpHitCountdown")->property("tone").toString(), QStringLiteral("off"));
        QVERIFY(text(p, "lpHitsState").startsWith(QStringLiteral("Triggers disarmed")));

        p.setSnapshot(deskFailed());
        QCOMPARE(text(p, "lpDeskLink"), QStringLiteral("ETC Eos · Failed"));
        QVERIFY(shown(p, "lpDeskEmpty"));
        QVERIFY(text(p, "lpDeskDetail").contains(QStringLiteral("Connection refused")));
        QVERIFY(!shown(p, "lpDeskActive"));
        QVERIFY(!shown(p, "lpHitCountdown"));
        QCOMPARE(text(p, "lpHitsState"), QStringLiteral("Hits appear here while a song with lighting triggers plays."));

        p.setSnapshot(endOfList());
        QCOMPARE(text(p, "lpDeskLink"), QStringLiteral("No desk feedback"));
        QVERIFY(shown(p, "lpDeskEmpty"));

        // Bottom-docked: wide and short → the blocks sit side by side.
        p.resize(1200, 260);
        QVERIFY(p.isWideLayout());
        p.resize(320, 600);
        QVERIFY(!p.isWideLayout());
    }

    void lightingPanelSignals()
    {
        LightingPanel p;
        p.setSnapshot(live());
        QSignalSpy armed(&p, &LightingPanel::armedToggled);
        QSignalSpy settings(&p, &LightingPanel::deskSettingsRequested);
        QCOMPARE(child<QPushButton>(p, "lpArmed")->focusPolicy(), Qt::NoFocus);
        child<QPushButton>(p, "lpArmed")->click();
        QCOMPARE(armed.count(), 1);
        QCOMPARE(armed.at(0).at(0).toBool(), false);
        child<QPushButton>(p, "lpDeskSettings")->click();
        QCOMPARE(settings.count(), 1);
    }

    // Not an assertion: writes the design-check PNGs when asked to.
    void renderPngs()
    {
        const QString dir = qEnvironmentVariable("QUEWI_RENDER_DIR");
        if (dir.isEmpty()) QSKIP("QUEWI_RENDER_DIR not set");
        QDir().mkpath(dir);
        struct Scene { const char *name; ShowSnapshot snap; };
        const Scene scenes[] = {
            {"live", live()}, {"end", endOfList()}, {"paused-disarmed", pausedDisarmed()}, {"desk-failed", deskFailed()},
        };
        for (const auto &sc : scenes) {
            for (const QSize sz : {QSize(1280, 720), QSize(1920, 1080), QSize(1024, 640)}) {
                ShowModeView v;
                v.resize(sz);
                v.setSnapshot(sc.snap);
                v.show();
                QVERIFY(QTest::qWaitForWindowExposed(&v));
                QTest::qWait(50);
                v.grab().save(QStringLiteral("%1/show-%2-%3x%4.png").arg(dir, QLatin1String(sc.name)).arg(sz.width()).arg(sz.height()));
            }
            for (const QSize sz : {QSize(300, 700), QSize(1200, 260)}) {
                LightingPanel p;
                p.resize(sz);
                p.setSnapshot(sc.snap);
                p.show();
                QVERIFY(QTest::qWaitForWindowExposed(&p));
                QTest::qWait(50);
                p.grab().save(QStringLiteral("%1/panel-%2-%3x%4.png").arg(dir, QLatin1String(sc.name)).arg(sz.width()).arg(sz.height()));
            }
        }
    }
};

QTEST_MAIN(ShowModeViewTests)
#include "test_show_mode_view.moc"
