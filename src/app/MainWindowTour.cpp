// The screenshot tour (--screenshot-tour <dir>): open the app, build a small
// demo show, and save a picture of each main screen — the cue list, bigger
// text, Preferences pages, the command menu, Show Mode, the Matrix List, the
// Lighting panel, About and Shortcuts — plus fit.txt, every visible button or
// label whose text doesn't fit its width. It exists so the macOS and Linux
// builds can be looked at from CI (real platform, real fonts), where nobody
// sits in front of them. Never run on a real show: it adds cues and changes
// the cue list's zoom (put back afterwards).

#include "MainWindow.h"

#include "cues/Cue.h"
#include "ui/CueListView.h"

#include <QAbstractButton>
#include <QApplication>
#include <QDir>
#include <QDockWidget>
#include <QEventLoop>
#include <QFile>
#include <QLabel>
#include <QSettings>
#include <QStatusBar>
#include <QTextStream>
#include <QTimer>

namespace quewi {

namespace {

void settle(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

// Text that doesn't fit: a single-line label or a button whose text is wider
// than the room it's given (a sign that a platform's font runs wider).
void checkFit(QWidget *top, const QString &shot, QTextStream &out)
{
    for (auto *b : top->findChildren<QAbstractButton *>()) {
        if (!b->isVisible() || b->text().isEmpty()) continue;
        const QString t = QString(b->text()).remove(QLatin1Char('&'));
        const int need = b->fontMetrics().horizontalAdvance(t) + (b->icon().isNull() ? 0 : b->iconSize().width() + 4);
        if (need > b->contentsRect().width() - 4)
            out << shot << "\tbutton\t" << b->objectName() << "\t\"" << t << "\"\tneeds " << need
                << " px, has " << b->contentsRect().width() << "\n";
    }
    for (auto *l : top->findChildren<QLabel *>()) {
        // An ElideLabel shortens itself with "…" by design (tooltip has it all).
        if (l->inherits("quewi::ui::ElideLabel")) continue;
        if (!l->isVisible() || l->wordWrap() || l->text().isEmpty() || l->textFormat() == Qt::RichText
            || Qt::mightBeRichText(l->text()) || l->text().contains(QLatin1Char('\n')))
            continue;
        const int need = l->fontMetrics().horizontalAdvance(l->text());
        if (need > l->contentsRect().width() + 1)
            out << shot << "\tlabel\t" << l->objectName() << "\t\"" << l->text() << "\"\tneeds " << need
                << " px, has " << l->contentsRect().width() << "\n";
    }
}

} // namespace

void MainWindow::runScreenshotTour(const QString &dirPath)
{
    QDir dir(dirPath);
    dir.mkpath(QStringLiteral("."));
    QFile fitFile(dir.filePath(QStringLiteral("fit.txt")));
    fitFile.open(QIODevice::WriteOnly | QIODevice::Text);
    QTextStream fit(&fitFile);
    fit << "# quewi " << QApplication::applicationVersion() << " on " << QSysInfo::prettyProductName()
        << ", platform " << QApplication::platformName() << ", font " << QApplication::font().family()
        << " " << QApplication::font().pointSizeF() << "pt, dpr " << devicePixelRatioF() << "\n";
    int shotNo = 0;
    auto save = [&](QWidget *w, const QString &name) {
        if (!w) { fit << name << "\tMISSING\n"; return; }
        const QString file = QStringLiteral("%1-%2.png").arg(++shotNo, 2, 10, QLatin1Char('0')).arg(name);
        w->grab().save(dir.filePath(file));
        checkFit(w, name, fit);
    };
    // A dialog the code opens with exec(): grab it once it's up, then close it.
    auto grabModal = [&](const QString &name, int ms = 700) {
        QTimer::singleShot(ms, this, [&, name] {
            QWidget *w = QApplication::activeModalWidget();
            if (!w) w = QApplication::activePopupWidget();
            save(w, name);
            if (w) w->close();
        });
    };

    resize(1440, 900);
    settle(400);

    // A small show to look at.
    const char *names[] = {"Preshow announcement", "Overture", "Defying Gravity", "Storm bed",
                           "Thunder 1", "Scene change music", "Blackout"};
    for (const char *n : names) {
        insertMemoCue();
        if (auto *c = m_cueListView ? m_cueListView->currentCue() : nullptr)
            c->setField(QStringLiteral("name"), QString::fromLatin1(n));
    }
    if (m_cueListView) m_cueListView->setCurrentIndex(m_cueListView->model()->index(1, 0));
    settle(400);
    save(this, QStringLiteral("cue-list"));

    if (m_cueListView) {
        const double zoomWas = m_cueListView->zoom();
        m_cueListView->setZoom(1.6);
        settle(300);
        save(this, QStringLiteral("cue-list-zoom-160"));
        m_cueListView->setZoom(zoomWas);
        QSettings s(QStringLiteral("ServeGaming"), QStringLiteral("quewi"));
        if (qFuzzyCompare(zoomWas, 1.0)) s.remove(QStringLiteral("cueList/zoom"));
    }

    for (const char *page : {"General", "Audio", "Cue List", "OSC", "Lighting", "Theme", "Show Mode", "Command menu"}) {
        grabModal(QStringLiteral("prefs-%1").arg(QString::fromLatin1(page).toLower().replace(QLatin1Char(' '), QLatin1Char('-'))));
        showPreferencesPage(QString::fromLatin1(page));
        settle(100);
    }

    grabModal(QStringLiteral("command-search"));
    showCommandMenu(false);
    settle(100);
    grabModal(QStringLiteral("command-keys"));
    showCommandMenu(true);
    settle(100);

    grabModal(QStringLiteral("shortcuts"));
    showShortcutsDialog();
    settle(100);
    grabModal(QStringLiteral("about"));
    showAbout();
    settle(100);

    if (m_lightingDock) {
        m_lightingDock->show();
        settle(400);
        save(this, QStringLiteral("lighting-panel"));
        m_lightingDock->hide();
    }

    addMatrixListTab();
    settle(900);
    save(this, QStringLiteral("matrix-list"));
    if (m_listTabs && m_listTabs->count() > 0) {
        m_listTabs->setCurrentIndex(0);
        onTabSelected(0);
        settle(300);
    }

    if (m_actShowMode) {
        m_actShowMode->setChecked(true);
        toggleShowMode();
        settle(900);
        save(this, QStringLiteral("show-mode"));
        resize(1280, 720);
        settle(700);
        save(this, QStringLiteral("show-mode-1280x720"));
        m_actShowMode->setChecked(false);
        toggleShowMode();
        settle(300);
    }

    fit << "# done\n";
    fitFile.close();
    // Nothing to save: this was a throwaway show.
    if (m_workspace) m_workspace->markClean();
    if (m_journalTimer) m_journalTimer->stop();
    clearJournal();      // and no crash-recovery copy of it left behind
    settle(300);         // a write already queued lands now; clear that one too
    if (m_workspace) m_workspace->markClean();
    clearJournal();
    QApplication::exit(0);
}

} // namespace quewi
