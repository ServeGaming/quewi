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

#include "core/CueList.h"
#include "core/Workspace.h"
#include "cues/Cue.h"
#include "osc/EosCueLists.h"
#include "osc/EosFeedback.h"
#include "osc/OscEngine.h"
#include "ui/CueListView.h"
#include "ui/MatrixView.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTabBar>

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

} // namespace quewi
