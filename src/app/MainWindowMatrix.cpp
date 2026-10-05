// The Matrix List — quewi's cues and the lighting desk's in one running
// order (ui/MatrixView, core/MatrixModel). Its tab, and its remote queries
// for HeliOSC; documented in docs/osc-control/reference.md:
//
//   /quewi/query/matrix [<from i>] [<count i>] [<list s>]
//                       → /quewi/reply/matrix <json s>
//       rows [from, from + count) of the merged list (default 0, 100 — a
//       whole 300-row show doesn't fit one UDP packet, so page through it
//       with "total"). <list> picks a Matrix List by name or id; default:
//       the one on screen, else the show's first.
//   /quewi/query/matrix/current [<before i>] [<after i>] [<list s>]
//                       → /quewi/reply/matrix <json s>
//       the rows around standby (default 3 before, 12 after).
// Notifications:
//   /quewi/notify/matrix/changed <listId s>   rows rebuilt (placements, desk
//       cue list read, source list edited) — query again.

#include "MainWindow.h"
#include "ui/SafeKey.h"

#include "GoEngine.h"

#include "core/CueList.h"
#include "core/Workspace.h"
#include "cues/Cue.h"
#include "osc/EosCueLists.h"
#include "osc/EosFeedback.h"
#include "osc/OscEngine.h"
#include "ui/CueListView.h"
#include "audio/AudioCue.h"
#include "audio/AudioEngine.h"
#include "ui/DeskTakeRecorder.h"
#include "ui/LightingDeskDialog.h"
#include "video/VideoCue.h"
#include "ui/MatrixView.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QSignalBlocker>
#include "ui/TransportBar.h"

#include <QStackedWidget>
#include <QStatusBar>
#include <QJsonArray>
#include <QTabBar>
#include <QTimer>

namespace quewi {

bool MainWindow::matrixShowing() const
{
    return m_centerStack && m_matrixView && m_matrixView->cueList()
        && m_centerStack->currentWidget() == m_matrixView;
}

core::CueList *MainWindow::firstMatrixList() const
{
    if (!m_workspace) return nullptr;
    for (const auto &l : m_workspace->cueLists())
        if (l->kind() == core::CueList::Kind::Matrix) return l.get();
    return nullptr;
}

void MainWindow::syncMatrixWatch()
{
    if (!m_eosCueLists || !m_workspace) return;
    QStringList want;
    for (const auto &l : m_workspace->cueLists())
        if (l->kind() == core::CueList::Kind::Matrix)
            want << core::matrix::normalNumber(l->matrixConfig().deskList);
    want.removeDuplicates();
    m_eosCueLists->setWatched(want);
}

void MainWindow::onDeskCuesRead(const QString &deskList)
{
    if (!m_workspace || !m_eosCueLists
        || m_eosCueLists->state(deskList) != osc::EosCueLists::State::Ready) return;
    const auto now = ui::toDeskCues(m_eosCueLists->cues(deskList));
    for (const auto &l : m_workspace->cueLists()) {
        if (l->kind() != core::CueList::Kind::Matrix
            || core::matrix::normalNumber(l->matrixConfig().deskList) != deskList) continue;
        auto cfg = l->matrixConfig();
        if (cfg.deskCache == now && cfg.deskCachedAt.isValid()) continue;
        cfg.deskCache = now;
        cfg.deskCachedAt = QDateTime::currentDateTime();
        // Not an edit of yours: saved with the show next time, no "unsaved".
        QSignalBlocker block(l.get());
        l->setMatrixConfig(cfg);
    }
}

void MainWindow::wireMatrixView(ui::MatrixView *view)
{
    view->setDeskInfo(m_deskSeen.type == core::LightingDesk::Type::Eos, QStringLiteral("ETC Eos"));
    connect(view, &ui::MatrixView::statusMessage, this,
            [this](const QString &t) { statusBar()->showMessage(t, 4000); });
    connect(view, &ui::MatrixView::modified, this, [this] {
        if (m_workspace) m_workspace->markModified();
    });
    connect(view, &ui::MatrixView::goLightsRequested, this, &MainWindow::goLights);
    connect(view, &ui::MatrixView::goLightsStateChanged, this, &MainWindow::updateGoBoth);
    connect(view, &ui::MatrixView::lightsBackRequested, this, &MainWindow::lightsBack);
    connect(view, &ui::MatrixView::lightsStopRequested, this, &MainWindow::lightsStop);
    connect(view, &ui::MatrixView::deskSettingsRequested, this, [this] {
        if (ui::LightingDeskDialog::edit(this)) syncDeskFeedback();
    });
}

void MainWindow::goLights()
{
    // The safe key covers GO Lights too: it's a GO, for the desk.
    if (!ui::SafeKey::instance()->allows(ui::SafeKey::Action::Go)) {
        statusBar()->showMessage(ui::SafeKey::instance()->blockedMessage(ui::SafeKey::Action::Go), 3000);
        return;
    }
    if (!matrixShowing() || !m_goEngine) {
        statusBar()->showMessage(tr("GO Lights works from a Matrix List — open one from the List menu"), 3000);
        return;
    }
    const auto t = m_matrixView->goLightsTarget();
    if (!t.ok) {
        statusBar()->showMessage(tr("GO Lights: %1").arg(t.reason), 5000);
        return;
    }
    // The same send path as a lighting trigger's "Go to cue": the desk's
    // OSC port over UDP, /eos/cue/<list>/<cue>/fire.
    const bool ok = m_goEngine->sendDeskAction(ui::goLightsAction(t));
    statusBar()->showMessage(ok ? tr("GO Lights → LX %1%2 (desk cue list %3)")
                                      .arg(t.number, t.label.isEmpty() ? QString() : QStringLiteral(" ") + t.label, t.list)
                                : tr("GO Lights: couldn't send to the desk"), 4000);
}

void MainWindow::goBoth()
{
    if (!ui::SafeKey::instance()->allows(ui::SafeKey::Action::Go)) {
        statusBar()->showMessage(ui::SafeKey::instance()->blockedMessage(ui::SafeKey::Action::Go), 3000);
        return;
    }
    if (!matrixShowing() || !m_goEngine) {
        statusBar()->showMessage(tr("GO Both works from a Matrix List — open one from the List menu"), 3000);
        return;
    }
    // Read the lights' target BEFORE the sound GO moves the standby, so the
    // pair is the sound cue and the lighting cue that were both up next.
    const auto t = m_matrixView->goLightsTarget();
    const bool soundUp = m_cueListView && m_cueListView->nextCue();
    if (!t.ok && !soundUp) {
        statusBar()->showMessage(tr("GO Both: nothing is up next (%1)").arg(t.reason), 5000);
        return;
    }
    if (soundUp) onGoRequested();
    const bool lightsOk = t.ok && m_goEngine->sendDeskAction(ui::goLightsAction(t));
    QString msg;
    if (lightsOk)
        msg = tr("GO Both → %1 + LX %2%3").arg(soundUp ? tr("sound") : tr("no sound up next"), t.number,
                                              t.label.isEmpty() ? QString() : QStringLiteral(" ") + t.label);
    else if (t.ok)
        msg = tr("GO Both: the sound went, but the desk couldn't be reached");
    else
        msg = tr("GO Both: the sound went; no lights (%1)").arg(t.reason);
    statusBar()->showMessage(msg, 4000);
    updateGoBoth();
}

void MainWindow::updateGoBoth()
{
    if (!m_transport) return;
    if (!matrixShowing()) {
        m_transport->setGoBothState(false, false, QString());
        return;
    }
    const auto t = m_matrixView->goLightsTarget();
    auto *next = m_cueListView ? m_cueListView->nextCue() : nullptr;
    QStringList parts;
    if (next) parts << tr("sound %1 %2").arg(QString::number(next->number(), 'f', 2), next->name()).trimmed();
    if (t.ok) parts << tr("LX %1%2").arg(t.number, t.label.isEmpty() ? QString() : QStringLiteral(" ") + t.label);
    QString tip = parts.isEmpty() ? (t.reason.isEmpty() ? tr("Nothing is up next") : t.reason)
                                  : tr("Fire %1 together").arg(parts.join(tr(" and ")));
    if (!t.ok && next) tip += QStringLiteral("\n") + tr("(Lights: %1)").arg(t.reason);
    m_transport->setGoBothState(true, t.ok || next, tip);
}

void MainWindow::lightsBack()
{
    if (!matrixShowing() || !m_goEngine) {
        statusBar()->showMessage(tr("Lights Back works from a Matrix List — open one from the List menu"), 3000);
        return;
    }
    const auto t = m_matrixView->backTarget();
    if (!t.ok) {
        statusBar()->showMessage(tr("Lights Back: %1").arg(t.reason), 5000);
        return;
    }
    const bool ok = m_goEngine->sendDeskAction(ui::goLightsAction(t));
    statusBar()->showMessage(ok ? tr("Lights Back → LX %1%2 (desk cue list %3)")
                                      .arg(t.number, t.label.isEmpty() ? QString() : QStringLiteral(" ") + t.label, t.list)
                                : tr("Lights Back: couldn't send to the desk"), 4000);
}

void MainWindow::lightsStop()
{
    if (!matrixShowing() || !m_goEngine) {
        statusBar()->showMessage(tr("Lights Stop works from a Matrix List — open one from the List menu"), 3000);
        return;
    }
    const QString why = m_matrixView->stopReason();
    if (!why.isEmpty()) {
        statusBar()->showMessage(tr("Lights Stop: %1").arg(why), 5000);
        return;
    }
    const bool ok = m_goEngine->sendDeskAction(ui::lightsStopAction());
    statusBar()->showMessage(ok ? tr("Lights Stop → the desk's Stop key") : tr("Lights Stop: couldn't send to the desk"), 4000);
}

void MainWindow::addMatrixListTab()
{
    if (!m_workspace) return;
    // Like the mix list: jump to the existing one rather than pile up empty
    // ones on repeat presses (a show can still hold several — the + menu's
    // second press on a show that has one just shows it).
    core::CueList *list = firstMatrixList();
    if (!list) {
        auto created = std::make_unique<core::CueList>(tr("Matrix"));
        created->setKind(core::CueList::Kind::Matrix);
        core::matrix::Config cfg;
        // Interleave the list GO runs now; the desk's list 1 to start with.
        if (auto *active = m_workspace->activeCueList();
            active && active->kind() == core::CueList::Kind::Normal)
            cfg.sourceList = active->id();
        created->setMatrixConfig(cfg);
        list = m_workspace->addCueList(std::move(created));
        m_workspace->markModified();
        syncMatrixWatch();
    }
    if (!list) return;
    rebuildListTabs();
    for (int i = 0; i < m_listTabs->count(); ++i)
        if (m_listTabs->tabData(i).toUuid() == list->id()) {
            m_listTabs->setCurrentIndex(i);
            onTabSelected(i);
            break;
        }
    statusBar()->showMessage(tr("Matrix List ready — drag a lighting cue onto a quewi cue to tie them"), 4000);
}

void MainWindow::registerOscMatrix()
{
    auto sender = [this] {
        osc::Destination d;
        d.host = m_oscEngine->lastSenderHost();
        d.port = m_oscEngine->lastSenderPort();
        d.transport = osc::Destination::Udp;
        return d;
    };
    auto reply = [this](const osc::Destination &to, const QJsonObject &o) {
        osc::Message msg;
        msg.address = QStringLiteral("/quewi/reply/matrix");
        msg.args = {osc::Argument::s(QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact)))};
        m_oscEngine->send(to, msg);
    };
    auto intArg = [](const osc::Message &m, size_t i, int fallback) {
        if (m.args.size() <= i) return fallback;
        if (const auto n = osc::toNumber(m.args[i])) return int(*n);
        return fallback;
    };
    auto strArg = [](const osc::Message &m) -> QString {
        for (const auto &a : m.args)
            if (a.tag == osc::Argument::Tag::String) return std::get<QString>(a.value);
        return {};
    };
    // Which Matrix List a query means: by name/id, else the one on screen,
    // else the first.
    auto pick = [this](const QString &want) -> core::CueList * {
        if (!m_workspace) return nullptr;
        if (!want.isEmpty()) {
            for (const auto &l : m_workspace->cueLists())
                if (l->kind() == core::CueList::Kind::Matrix
                    && (l->name().compare(want, Qt::CaseInsensitive) == 0
                        || l->id() == QUuid::fromString(want)))
                    return l.get();
            return nullptr;
        }
        if (matrixShowing()) return m_matrixView->cueList();
        return firstMatrixList();
    };
    auto answer = [=](const osc::Message &m, bool around) {
        const auto to = sender();
        const int a = intArg(m, 0, around ? 3 : 0);
        const int b = intArg(m, 1, around ? 12 : 100);
        const QString want = strArg(m);
        QMetaObject::invokeMethod(this, [=] {
            auto *list = pick(want);
            if (!list) {
                reply(to, QJsonObject{{QStringLiteral("error"), QStringLiteral("no matrix list")}});
                return;
            }
            const auto build = ui::buildMatrix(m_workspace.get(), list, m_eosCueLists);
            ui::MatrixLive live;
            if (auto *c = m_cueListView ? m_cueListView->nextCue() : nullptr) live.standby = c->id();
            live.running = m_runningCueIds;
            if (m_eosFeedback) {
                live.deskActiveList = m_eosFeedback->active().list;
                live.deskActiveCue = m_eosFeedback->active().cue;
                live.deskPendingList = m_eosFeedback->pending().list;
                live.deskPendingCue = m_eosFeedback->pending().cue;
            }
            int from = std::max(0, a), count = std::clamp(b, 0, 500);
            if (around) {
                int at = build.rowOfQuewi(live.standby);
                if (at < 0) at = std::max(0, build.rowOfDesk(live.deskActiveList, live.deskActiveCue));
                from = std::max(0, at - std::max(0, a));
                count = std::clamp(a + 1 + b, 1, 500);
            }
            reply(to, ui::matrixJson(list, build, live, from, count));
        }, Qt::QueuedConnection);
    };
    m_oscEngine->subscribe(QStringLiteral("/quewi/query/matrix"),
                           [=](const osc::Message &m) { answer(m, false); });
    m_oscEngine->subscribe(QStringLiteral("/quewi/query/matrix/current"),
                           [=](const osc::Message &m) { answer(m, true); });
    // (/quewi/notify/matrix/changed is pushed from buildLayout, where the
    // view is made — this runs before it exists.)
}

// ── Recording lighting triggers from the desk, for remotes ─────────────────
//   /quewi/cue/<num>/triggers/record start [<list s>]   record while that cue plays
//   /quewi/cue/<num>/triggers/record stop               stop; the take waits
//   /quewi/cue/<num>/triggers/record keep [<group s>]   add it as triggers (one undo step)
//   /quewi/cue/<num>/triggers/record discard
// Notifications:
//   /quewi/notify/triggers/recording <num d> <T/F>
//   /quewi/notify/triggers/recorded  <num d> <json s>   the take, when it stops
// Recording stops by itself when the cue's song stops (after it has played).

void MainWindow::stopLiveRecord(bool notify)
{
    if (!m_liveRecorder || !m_liveRecorder->isRecording()) return;
    m_liveRecorder->stop();
    if (m_liveRecordWatch) m_liveRecordWatch->stop();
    m_liveTake = m_liveRecorder->takes();
    m_liveRecorder->clear();
    if (!notify) return;
    const double num = m_liveRecordCue ? m_liveRecordCue->number() : 0.0;
    QJsonArray takes;
    for (const auto &t : m_liveTake)
        takes.append(QJsonObject{{QStringLiteral("at"), t.at}, {QStringLiteral("list"), t.list},
                                 {QStringLiteral("cue"), t.cue}, {QStringLiteral("label"), t.label}});
    pushOscNotify(QStringLiteral("/quewi/notify/triggers/recording"), {osc::Argument::d(num), osc::Argument::F()});
    pushOscNotify(QStringLiteral("/quewi/notify/triggers/recorded"),
                  {osc::Argument::d(num),
                   osc::Argument::s(QString::fromUtf8(QJsonDocument(takes).toJson(QJsonDocument::Compact)))});
    statusBar()->showMessage(tr("Recorded %1 desk cue(s) on cue %2 — keep or discard from the remote")
                                 .arg(m_liveTake.size()).arg(num), 6000);
}

void MainWindow::registerOscRecord()
{
    m_liveRecorder = new ui::DeskTakeRecorder(this);
    // Where the recorded cue's song is (file seconds), from its playing voice.
    m_liveRecorder->setPlayhead([this]() -> double {
        auto *ac = m_liveRecordCue ? video::VideoCue::audioOf(m_liveRecordCue.data()) : nullptr;
        if (!ac || !m_audioEngine || ac->currentVoiceId() == 0) return -1.0;
        for (const auto &v : m_audioEngine->activeVoices())
            if (v.id == ac->currentVoiceId())
                return m_audioEngine->isPaused(v.id) ? -1.0 : v.positionSeconds;
        return -1.0;
    });
    m_liveRecordWatch = new QTimer(this);
    m_liveRecordWatch->setInterval(250);
    connect(m_liveRecordWatch, &QTimer::timeout, this, [this] {
        if (!m_liveRecorder->isRecording()) { m_liveRecordWatch->stop(); return; }
        if (!m_liveRecorder->isRecording()) return;
        auto *ac = m_liveRecordCue ? video::VideoCue::audioOf(m_liveRecordCue.data()) : nullptr;
        const bool playing = ac && ac->currentVoiceId() != 0;
        if (playing) m_liveRecordHeard = true;
        else if (m_liveRecordHeard || !m_liveRecordCue) stopLiveRecord(true);   // the song ended
    });

    m_oscEngine->subscribe(QStringLiteral("/quewi/cue/*/triggers/record"), [this](const osc::Message &m) {
        const auto parts = m.address.split(QChar('/'), Qt::SkipEmptyParts);
        bool ok = false;
        const double num = parts.value(2).toDouble(&ok);
        if (!ok) return;
        QString verb, extra;
        for (const auto &a : m.args)
            if (a.tag == osc::Argument::Tag::String) {
                if (verb.isEmpty()) verb = std::get<QString>(a.value).trimmed().toLower();
                else extra = std::get<QString>(a.value).trimmed();
            }
        QMetaObject::invokeMethod(this, [this, num, verb, extra] {
            auto *c = oscCueByNumber(num);
            auto *ac = c ? video::VideoCue::audioOf(c) : nullptr;
            if (!c) return;
            if (verb == QLatin1String("start")) {
                if (!qobject_cast<audio::AudioCue *>(c) && !qobject_cast<video::VideoCue *>(c)) return;
                stopLiveRecord(false);
                m_liveTake.clear();
                m_liveRecorder->setFeedback(m_eosFeedback);
                m_liveRecordCue = c;
                m_liveRecordHeard = ac && ac->currentVoiceId() != 0;
                if (!m_liveRecorder->start(extra)) {
                    statusBar()->showMessage(tr("Can't record from the desk: %1").arg(m_liveRecorder->whyNot()), 6000);
                    pushOscNotify(QStringLiteral("/quewi/notify/triggers/recording"),
                                  {osc::Argument::d(num), osc::Argument::F()});
                    return;
                }
                m_liveRecordWatch->start();
                statusBar()->showMessage(tr("● Recording desk cues against cue %1").arg(num), 4000);
                pushOscNotify(QStringLiteral("/quewi/notify/triggers/recording"), {osc::Argument::d(num), osc::Argument::T()});
            } else if (verb == QLatin1String("stop")) {
                stopLiveRecord(true);
            } else if (verb == QLatin1String("keep") && !m_liveTake.empty() && m_liveRecordCue == c) {
                auto *sound = qobject_cast<video::VideoCue *>(c) ? qobject_cast<video::VideoCue *>(c)->sound()
                                                                : qobject_cast<audio::AudioCue *>(c);
                if (!sound) return;
                auto next = sound->lightTriggers();
                const QString group = audio::uniqueGroupName(next, extra.isEmpty() ? tr("Recorded") : extra);
                for (auto &t : audio::DeskRecording::toTriggers(m_liveTake, group)) next.push_back(t);
                commitTriggers(c, next);
                m_liveTake.clear();
                statusBar()->showMessage(tr("Kept the recorded desk cues on cue %1 (Ctrl+Z undoes)").arg(num), 5000);
            } else if (verb == QLatin1String("discard")) {
                m_liveTake.clear();
            }
        }, Qt::QueuedConnection);
    });
}

} // namespace quewi
