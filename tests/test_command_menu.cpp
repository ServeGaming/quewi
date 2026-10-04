#include <QTest>
#include <QApplication>
#include <QDir>
#include <QDockWidget>
#include <QLabel>
#include <QCheckBox>
#include <QComboBox>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QPushButton>
#include <QMenu>
#include <QMenuBar>
#include <QPainter>
#include <QSettings>
#include <QSignalSpy>
#include <QTabBar>
#include <QTimer>
#include <QVBoxLayout>

#include "audio/AudioCue.h"
#include "core/CueList.h"
#include "core/CueListModel.h"
#include "core/Workspace.h"
#include "cues/FadeCue.h"
#include "cues/MemoCue.h"
#include "cues/WaitCue.h"
#include "lighting/LightCue.h"
#include "ui/CommandMatch.h"
#include "ui/CommandMenu.h"
#include "ui/CueListView.h"
#include "ui/Inspector.h"
#include "ui/LeaderKey.h"
#include "ui/PreferencesDialog.h"
#include "ui/ShortcutManager.h"
#include "ui/Theme.h"
#include "ui/TransportBar.h"

using namespace quewi;
using namespace quewi::ui;

// The command menu: fuzzy ranking, what's in it (and what never is), the
// key menu's mnemonics and drill-down, the leader key, text-field safety,
// and Show Mode's filtering.
//
// Set QUEWI_RENDER_DIR to a folder to also write PNGs of the search view
// and the key menu over a stand-in main window (design checks).
class CommandMenuTests : public QObject {
    Q_OBJECT

    // ── A menu bar shaped like quewi's ──
    struct Bar {
        QMenuBar *bar;
        QAction *newAudio = nullptr, *newLight = nullptr, *del = nullptr, *showMode = nullptr,
                *armed = nullptr, *desk = nullptr, *preflight = nullptr, *undo = nullptr,
                *themeDark = nullptr, *openRecentNone = nullptr, *prefs = nullptr;
    };
    static Bar makeBar(QWidget *parent)
    {
        Bar b;
        b.bar = new QMenuBar(parent);
        auto *file = b.bar->addMenu(QStringLiteral("&File"));
        file->addAction(QStringLiteral("&New"), QKeySequence::New);
        file->addAction(QStringLiteral("&Open…"), QKeySequence::Open);
        auto *recent = file->addMenu(QStringLiteral("Open &Recent"));
        b.openRecentNone = recent->addAction(QStringLiteral("(none)"));
        b.openRecentNone->setEnabled(false);
        file->addAction(QStringLiteral("&Save"), QKeySequence::Save);
        b.prefs = file->addAction(QStringLiteral("&Preferences…"));
        file->addAction(QStringLiteral("Check for &updates…"));
        auto *edit = b.bar->addMenu(QStringLiteral("&Edit"));
        b.undo = edit->addAction(QStringLiteral("&Undo"), QKeySequence::Undo);
        edit->addAction(QStringLiteral("&Find / Replace…"), QKeySequence(QStringLiteral("Ctrl+F")));
        auto *tools = b.bar->addMenu(QStringLiteral("&Tools"));
        b.preflight = tools->addAction(QStringLiteral("&Pre-flight…"), QKeySequence(QStringLiteral("Ctrl+P")));
        tools->addAction(QStringLiteral("&OSC Monitor…"), QKeySequence(QStringLiteral("Ctrl+1")));
        tools->addAction(QStringLiteral("&Command menu…"), QKeySequence(QStringLiteral("Ctrl+K")));
        tools->addAction(QStringLiteral("&Keyboard shortcuts…"));
        tools->addAction(QStringLiteral("S&cript follower…"), QKeySequence(QStringLiteral("Ctrl+Shift+S")));
        b.desk = tools->addAction(QStringLiteral("Lighting &Desk…"));
        b.armed = tools->addAction(QStringLiteral("Lighting &Triggers Armed"));
        b.armed->setCheckable(true);
        b.armed->setChecked(true);
        b.showMode = tools->addAction(QStringLiteral("&Show Mode (locked)"), QKeySequence(QStringLiteral("Ctrl+Shift+L")));
        b.showMode->setCheckable(true);
        auto *view = b.bar->addMenu(QStringLiteral("&View"));
        auto *theme = view->addMenu(QStringLiteral("&Theme"));
        b.themeDark = theme->addAction(QStringLiteral("&Dark"));
        theme->addAction(QStringLiteral("&Light"));
        theme->addAction(QStringLiteral("&Coffee"));
        auto *insp = view->addAction(QStringLiteral("&Inspector panel"), QKeySequence(QStringLiteral("Ctrl+I")));
        insp->setCheckable(true);
        insp->setChecked(true);
        auto *lp = view->addAction(QStringLiteral("&Lighting panel"));
        lp->setCheckable(true);
        view->addAction(QStringLiteral("&Soundboard"), QKeySequence(QStringLiteral("Ctrl+Shift+C")));
        auto *list = b.bar->addMenu(QStringLiteral("&List"));
        list->addAction(QStringLiteral("&New cue list…"));
        list->addAction(QStringLiteral("&Rename current…"));
        list->addAction(QStringLiteral("&Remove current"));
        auto *cue = b.bar->addMenu(QStringLiteral("&Cue"));
        cue->addAction(QStringLiteral("New &Memo"), QKeySequence(Qt::Key_M));
        cue->addAction(QStringLiteral("New &OSC"), QKeySequence(Qt::Key_O));
        b.newAudio = cue->addAction(QStringLiteral("New &Audio"), QKeySequence(Qt::Key_A));
        cue->addAction(QStringLiteral("New &Fade"), QKeySequence(Qt::Key_F));
        b.newLight = cue->addAction(QStringLiteral("New &Light"), QKeySequence(Qt::Key_L));
        cue->addAction(QStringLiteral("New Light Fa&de"), QKeySequence(QStringLiteral("Shift+L")));
        cue->addAction(QStringLiteral("New &Video"), QKeySequence(Qt::Key_V));
        cue->addAction(QStringLiteral("New &Wait"), QKeySequence(Qt::Key_W));
        cue->addAction(QStringLiteral("New S&tart"), QKeySequence(QStringLiteral("Shift+S")));
        cue->addAction(QStringLiteral("New Sto&p"), QKeySequence(QStringLiteral("Shift+X")));
        cue->addAction(QStringLiteral("New &Load"));
        cue->addAction(QStringLiteral("Import from &URL…"), QKeySequence(QStringLiteral("Ctrl+U")));
        cue->addAction(QStringLiteral("Toggle &Arm"), QKeySequence(Qt::Key_E));
        b.del = cue->addAction(QStringLiteral("&Delete"), QKeySequence::Delete);
        auto *help = b.bar->addMenu(QStringLiteral("&Help"));
        help->addAction(QStringLiteral("&Keyboard shortcuts…"));
        help->addAction(QStringLiteral("&Notifications…"));
        help->addAction(QStringLiteral("&About quewi…"));
        return b;
    }

    static CommandContext makeContext(const Bar &b, bool showMode = false)
    {
        CommandContext ctx;
        ctx.menuBar = b.bar;
        ctx.showMode = showMode;
        const QUuid id12 = QUuid::createUuid(), id112 = QUuid::createUuid(), id3 = QUuid::createUuid();
        ctx.cues = {
            { id3,   3,   QStringLiteral("Preshow music"),   QStringLiteral("Audio"), true },
            { id12,  12,  QStringLiteral("Defying Gravity"), QStringLiteral("Audio"), true },
            { id112, 112, QStringLiteral("Dragon flyover"),  QStringLiteral("Light"), false },
        };
        ctx.lists = {
            { QUuid::createUuid(), QStringLiteral("Act 1"),      QStringLiteral("cue list"),   true },
            { QUuid::createUuid(), QStringLiteral("Act 2"),      QStringLiteral("cue list"),   false },
            { QUuid::createUuid(), QStringLiteral("Soundboard"), QStringLiteral("soundboard"), false },
        };
        ctx.recentShows = { QStringLiteral("C:/shows/Wicked.quewi"), QStringLiteral("C:/shows/Panto 2025.quewi") };
        ctx.preferencePages = commandMenuPreferencePages();
        return ctx;
    }

    static int indexOfTitle(const QList<CommandItem> &items, const QString &title)
    {
        for (int i = 0; i < items.size(); ++i) if (items[i].title == title) return i;
        return -1;
    }

    static QChar keyFor(const MenuNode &parent, const QString &label)
    {
        for (const auto &c : parent.children) if (c.label == label) return c.key;
        return {};
    }
    static const MenuNode *child(const MenuNode &parent, const QString &label)
    {
        for (const auto &c : parent.children) if (c.label == label) return &c;
        return nullptr;
    }

    // The leader is a window shortcut, so the window must be Qt's active
    // one. Under ctest nothing guarantees the OS gives it the foreground, so
    // make it active from the inside rather than waiting on the OS.
    static bool activate(QWidget &w)
    {
        w.show();
        if (!QTest::qWaitForWindowExposed(&w)) return false;
        w.activateWindow();
        QT_WARNING_PUSH
        QT_WARNING_DISABLE_DEPRECATED
        QApplication::setActiveWindow(&w);
        QT_WARNING_POP
        for (int i = 0; i < 50 && QApplication::activeWindow() != &w; ++i) QTest::qWait(20);
        return QApplication::activeWindow() == &w;
    }

    // Every test starts from clean menu settings and leaves none behind.
    static void wipeSettings()
    {
        QSettings s(QStringLiteral("ServeGaming"), QStringLiteral("quewi"));
        s.remove(QStringLiteral("commandmenu"));
        s.remove(QStringLiteral("shortcuts/commandmenu.leader"));
    }
    QVariantMap m_savedCommandMenu;
    QVariant m_savedLeader;

private slots:
    void initTestCase()
    {
        QSettings s(QStringLiteral("ServeGaming"), QStringLiteral("quewi"));
        s.beginGroup(QStringLiteral("commandmenu"));
        for (const auto &k : s.allKeys()) m_savedCommandMenu.insert(k, s.value(k));
        s.endGroup();
        m_savedLeader = s.value(QStringLiteral("shortcuts/commandmenu.leader"));
        wipeSettings();
    }
    void cleanupTestCase()
    {
        wipeSettings();
        QSettings s(QStringLiteral("ServeGaming"), QStringLiteral("quewi"));
        s.beginGroup(QStringLiteral("commandmenu"));
        for (auto it = m_savedCommandMenu.cbegin(); it != m_savedCommandMenu.cend(); ++it) s.setValue(it.key(), it.value());
        s.endGroup();
        if (m_savedLeader.isValid()) s.setValue(QStringLiteral("shortcuts/commandmenu.leader"), m_savedLeader);
        s.sync();
    }
    void init() { wipeSettings(); }

    // ── Fuzzy ranking ──
    void fuzzyTiers()
    {
        const QString t = QStringLiteral("New Audio");
        const int prefix   = fuzzyMatch(QStringLiteral("new au"), t).score;
        const int acronym  = fuzzyMatch(QStringLiteral("na"), t).score;
        const int wordStart = fuzzyMatch(QStringLiteral("audio"), t).score;
        const int inside   = fuzzyMatch(QStringLiteral("udi"), t).score;
        const int gaps     = fuzzyMatch(QStringLiteral("nwdio"), t).score;
        QVERIFY(prefix > acronym);
        QVERIFY(acronym > wordStart);
        QVERIFY(wordStart > inside);
        QVERIFY(inside > gaps);
        QVERIFY(gaps > 0);
        QVERIFY(!fuzzyMatch(QStringLiteral("xyz"), t).matched);
        QVERIFY(!fuzzyMatch(QStringLiteral("audion"), t).matched);
        QCOMPARE(fuzzyMatch(QStringLiteral("nwdio"), t).positions, (QList<int>{0, 2, 6, 7, 8}));
        QCOMPARE(fuzzyMatch(QStringLiteral("na"), t).positions, (QList<int>{0, 4}));
        QVERIFY(fuzzyMatch(QString(), t).matched);
    }

    void fuzzyPrefersShorterAndMultiWord()
    {
        QVERIFY(fuzzyMatch(QStringLiteral("new"), QStringLiteral("New Memo")).score
                > fuzzyMatch(QStringLiteral("new"), QStringLiteral("New Light Fade")).score);
        const auto two = fuzzyMatch(QStringLiteral("light fade"), QStringLiteral("New Light Fade"));
        QVERIFY(two.matched);
        const auto swapped = fuzzyMatch(QStringLiteral("fade light"), QStringLiteral("New Light Fade"));
        QVERIFY(swapped.matched);
        QVERIFY(two.score >= swapped.score);
        QVERIFY(!fuzzyMatch(QStringLiteral("light video"), QStringLiteral("New Light Fade")).matched);
        QCOMPARE(wordStarts(QStringLiteral("CueList view")), (QList<int>{0, 3, 8}));
    }

    // ── Mnemonics ──
    void mnemonicPicking()
    {
        QSet<QChar> taken;
        QCOMPARE(pickMnemonic(QStringLiteral("New Audio"), QChar('A'), taken), QChar('A'));
        taken.insert(QChar('A'));
        QCOMPARE(pickMnemonic(QStringLiteral("New Audio"), QChar('A'), taken), QChar('N'));   // word start next
        taken.insert(QChar('N'));
        QCOMPARE(pickMnemonic(QStringLiteral("New Audio"), QChar('A'), taken), QChar('E'));   // any letter
        for (char c = 'A'; c <= 'Z'; ++c) taken.insert(QChar::fromLatin1(c));
        QCOMPARE(pickMnemonic(QStringLiteral("New Audio"), QChar('A'), taken), QChar('1'));   // digits last
        QCOMPARE(pickMnemonic(QStringLiteral("Light Fade"), QChar(), {}), QChar('L'));
    }

    // ── What's in it ──
    void itemsCoverEverythingButTransport()
    {
        QWidget host;
        Bar b = makeBar(&host);
        // Transport actions live on the window, never in the menu bar.
        auto *go = new QAction(QStringLiteral("GO"), &host);
        go->setShortcut(QKeySequence(Qt::Key_Space));
        host.addAction(go);
        auto *panic = new QAction(QStringLiteral("Panic"), &host);
        panic->setShortcut(QKeySequence(Qt::Key_Escape));
        host.addAction(panic);

        const auto items = buildCommandItems(makeContext(b));
        QCOMPARE(indexOfTitle(items, QStringLiteral("GO")), -1);
        QCOMPARE(indexOfTitle(items, QStringLiteral("Panic")), -1);
        QCOMPARE(indexOfTitle(items, QStringLiteral("Command menu…")), -1);      // not itself
        QCOMPARE(indexOfTitle(items, QStringLiteral("(none)")), -1);             // Open Recent replaced

        const int audio = indexOfTitle(items, QStringLiteral("New Audio"));
        QVERIFY(audio >= 0);
        QCOMPARE(items[audio].group, QStringLiteral("Cue"));
        QCOMPARE(items[audio].shortcut, QStringLiteral("A"));
        QVERIFY(items[audio].editing);
        QCOMPARE(items[audio].id, QStringLiteral("action:Cue/New Audio"));

        QVERIFY(indexOfTitle(items, QStringLiteral("Enter Show Mode")) >= 0);        // verb alias
        QVERIFY(indexOfTitle(items, QStringLiteral("Disarm lighting triggers")) >= 0); // checked → disarm
        const int desk = indexOfTitle(items, QStringLiteral("Lighting Desk…"));
        QVERIFY(desk >= 0);
        QCOMPARE(items[desk].group, QStringLiteral("Lights"));
        QVERIFY(!items[desk].editing);
        const int dark = indexOfTitle(items, QStringLiteral("Dark theme"));
        QVERIFY(dark >= 0);
        QCOMPARE(items[dark].group, QStringLiteral("View › Theme"));
        // One "Keyboard shortcuts…" even though it's in two menus.
        int shortcuts = 0;
        for (const auto &it : items) if (it.title == QStringLiteral("Keyboard shortcuts…")) ++shortcuts;
        QCOMPARE(shortcuts, 1);

        const int cue12 = indexOfTitle(items, QStringLiteral("12  Defying Gravity"));
        QVERIFY(cue12 >= 0);
        QCOMPARE(items[cue12].number, 12.0);
        QVERIFY(!items[cue12].editing);
        QVERIFY(static_cast<bool>(items[cue12].secondary) == false);   // no editCue callback given
        QVERIFY(indexOfTitle(items, QStringLiteral("Act 2")) >= 0);
        QVERIFY(!items[indexOfTitle(items, QStringLiteral("Act 1"))].enabled);   // already current
        QVERIFY(indexOfTitle(items, QStringLiteral("Wicked")) >= 0);
        QVERIFY(indexOfTitle(items, QStringLiteral("Command menu")) >= 0);       // the Preferences page
    }

    void runsTheAction()
    {
        QWidget host;
        Bar b = makeBar(&host);
        QSignalSpy spy(b.newAudio, &QAction::triggered);
        const auto items = buildCommandItems(makeContext(b));
        items[indexOfTitle(items, QStringLiteral("New Audio"))].run();
        QCOMPARE(spy.count(), 1);
    }

    void showModeKeepsOnlyRunSafeItems()
    {
        QWidget host;
        Bar b = makeBar(&host);
        b.undo->setEnabled(false);
        auto ctx = makeContext(b, true);
        bool edited = false;
        ctx.editCue = [&](const QUuid &) { edited = true; };
        const auto items = buildCommandItems(ctx);
        for (const auto &it : items) {
            QVERIFY2(isRunSafe(it), qPrintable(it.title));
            QVERIFY2(!it.editing, qPrintable(it.title));
        }
        QCOMPARE(indexOfTitle(items, QStringLiteral("New Audio")), -1);
        QCOMPARE(indexOfTitle(items, QStringLiteral("Delete")), -1);
        QCOMPARE(indexOfTitle(items, QStringLiteral("Undo")), -1);
        QCOMPARE(indexOfTitle(items, QStringLiteral("Act 2")), -1);
        QCOMPARE(indexOfTitle(items, QStringLiteral("Wicked")), -1);
        QCOMPARE(indexOfTitle(items, QStringLiteral("Audio")), -1);        // Preferences page
        QVERIFY(indexOfTitle(items, QStringLiteral("Enter Show Mode")) >= 0);
        QVERIFY(indexOfTitle(items, QStringLiteral("Pre-flight…")) >= 0);
        QVERIFY(indexOfTitle(items, QStringLiteral("Dark theme")) >= 0);
        const int cue12 = indexOfTitle(items, QStringLiteral("12  Defying Gravity"));
        QVERIFY(cue12 >= 0);
        QVERIFY(!items[cue12].secondary);                                   // no editor in Show Mode
        QVERIFY(items[cue12].secondaryLabel.isEmpty());
    }

    // ── The tree ──
    void treeKeysComeFromAcceleratorsAndStayPut()
    {
        QWidget host;
        Bar b = makeBar(&host);
        const auto ctx = makeContext(b);
        const auto items = buildCommandItems(ctx);
        const auto tree = buildMenuTree(items, ctx);

        QCOMPARE(keyFor(tree, QStringLiteral("Cue")), QChar('C'));
        QCOMPARE(keyFor(tree, QStringLiteral("Lights")), QChar('L'));
        QCOMPARE(keyFor(tree, QStringLiteral("Show")), QChar('S'));
        QCOMPARE(keyFor(tree, QStringLiteral("Go to")), QChar('G'));
        QCOMPARE(keyFor(tree, QStringLiteral("File")), QChar('F'));
        QCOMPARE(keyFor(tree, QStringLiteral("Edit")), QChar('E'));
        QCOMPARE(keyFor(tree, QStringLiteral("View")), QChar('V'));
        QCOMPARE(keyFor(tree, QStringLiteral("List")), QChar('I'));     // L is Lights
        QCOMPARE(keyFor(tree, QStringLiteral("Tools")), QChar('T'));
        QCOMPARE(keyFor(tree, QStringLiteral("Preferences")), QChar('P'));
        QCOMPARE(keyFor(tree, QStringLiteral("Help")), QChar('H'));

        const auto *cue = child(tree, QStringLiteral("Cue"));
        QVERIFY(cue);
        QCOMPARE(keyFor(*cue, QStringLiteral("New Audio")), QChar('A'));
        QCOMPARE(keyFor(*cue, QStringLiteral("New Light")), QChar('L'));
        QCOMPARE(keyFor(*cue, QStringLiteral("New Light Fade")), QChar('D'));
        QCOMPARE(keyFor(*cue, QStringLiteral("New Start")), QChar('T'));
        QCOMPARE(keyFor(*cue, QStringLiteral("New Stop")), QChar('P'));
        QCOMPARE(keyFor(*cue, QStringLiteral("New Load")), QChar('N'));     // L and O taken → word start N
        QCOMPARE(keyFor(*cue, QStringLiteral("Toggle Arm")), QChar('G'));   // T, A, O taken → first free letter
        QCOMPARE(keyFor(*cue, QStringLiteral("Delete")), QChar('E'));
        // Every sibling has a distinct key.
        std::function<void(const MenuNode &)> distinct = [&](const MenuNode &n) {
            QSet<QChar> seen;
            for (const auto &c : n.children) {
                QVERIFY2(!c.key.isNull(), qPrintable(c.label));
                QVERIFY2(!seen.contains(c.key), qPrintable(c.label));
                seen.insert(c.key);
                if (c.isSubmenu()) distinct(c);
            }
        };
        distinct(tree);

        const auto *lights = child(tree, QStringLiteral("Lights"));
        QVERIFY(lights);
        QCOMPARE(keyFor(*lights, QStringLiteral("Lighting Desk…")), QChar('D'));
        QCOMPARE(keyFor(*lights, QStringLiteral("Disarm lighting triggers")), QChar('A'));
        const auto *show = child(tree, QStringLiteral("Show"));
        QVERIFY(show);
        QCOMPARE(keyFor(*show, QStringLiteral("Enter Show Mode")), QChar('M'));
        const auto *view = child(tree, QStringLiteral("View"));
        QVERIFY(view && child(*view, QStringLiteral("Theme")));
        QCOMPARE(keyFor(*child(*view, QStringLiteral("Theme")), QStringLiteral("Coffee theme")), QChar('C'));
        const auto *go = child(tree, QStringLiteral("Go to"));
        QVERIFY(go && child(*go, QStringLiteral("Lists")) && child(*go, QStringLiteral("Recent shows")));
        QCOMPARE(child(*go, QStringLiteral("Lists"))->children.size(), 3);

        // Remembered: an override survives a rebuild, and a changed label
        // keeps its remembered key.
        CommandMenuSettings::setMnemonic(QStringLiteral("cue/new-audio"), QChar('Z'));
        const auto tree2 = buildMenuTree(items, ctx);
        QCOMPARE(keyFor(*child(tree2, QStringLiteral("Cue")), QStringLiteral("New Audio")), QChar('Z'));
        CommandMenuSettings::resetMnemonics();
        const auto tree3 = buildMenuTree(items, ctx);
        QCOMPARE(keyFor(*child(tree3, QStringLiteral("Cue")), QStringLiteral("New Audio")), QChar('A'));
    }

    // ── The widget ──
    void drillDownRunsALeaf()
    {
        QWidget host;
        Bar b = makeBar(&host);
        QSignalSpy spy(b.newAudio, &QAction::triggered);
        CommandMenu menu(makeContext(b));
        menu.openKeys();
        QCOMPARE(menu.view(), CommandMenu::View::Keys);
        QVERIFY(menu.breadcrumb().isEmpty());
        QVERIFY(menu.visibleNodes().size() >= 8);

        menu.typeKey(Qt::Key_C, Qt::NoModifier, QStringLiteral("c"));
        QCOMPARE(menu.breadcrumb(), QStringLiteral("Cue"));
        QCOMPARE(menu.visibleNodes().first().label, QStringLiteral("New Memo"));
        menu.typeKey(Qt::Key_Backspace);
        QVERIFY(menu.breadcrumb().isEmpty());
        menu.typeKey(Qt::Key_Backspace);                     // at the top: stays
        QCOMPARE(menu.view(), CommandMenu::View::Keys);

        menu.typeKey(Qt::Key_C, Qt::NoModifier, QStringLiteral("C"));
        menu.typeKey(Qt::Key_A, Qt::NoModifier, QStringLiteral("a"));
        QVERIFY(menu.hasPending());
        QCOMPARE(spy.count(), 0);                             // nothing ran yet
        menu.takePending()();
        QCOMPARE(spy.count(), 1);
        QCOMPARE(CommandMenuSettings::recent().first(), QStringLiteral("action:Cue/New Audio"));
    }

    void typedLettersOnOpenAndSubmenus()
    {
        QWidget host;
        Bar b = makeBar(&host);
        QSignalSpy spy(b.themeDark, &QAction::triggered);
        CommandMenu menu(makeContext(b));
        menu.openKeys(QStringLiteral("V"));
        QCOMPARE(menu.breadcrumb(), QStringLiteral("View"));
        menu.typeKey(Qt::Key_T, Qt::NoModifier, QStringLiteral("t"));
        QCOMPARE(menu.breadcrumb(), QStringLiteral("View › Theme"));
        menu.typeKey(Qt::Key_D, Qt::NoModifier, QStringLiteral("d"));
        QVERIFY(menu.hasPending());
        menu.takePending()();
        QCOMPARE(spy.count(), 1);

        CommandMenu menu2(makeContext(b));
        menu2.openKeys(QStringLiteral("LD"));                 // Lights › Lighting Desk…
        QVERIFY(menu2.hasPending());
    }

    void nonMnemonicFallsThroughToSearchAndEscCloses()
    {
        QWidget host;
        Bar b = makeBar(&host);
        CommandMenu menu(makeContext(b));
        menu.openKeys();
        menu.typeKey(Qt::Key_1, Qt::NoModifier, QStringLiteral("1"));
        QCOMPARE(menu.view(), CommandMenu::View::Search);
        QCOMPARE(menu.input()->text(), QStringLiteral("1"));
        QVERIFY(menu.visibleTitles().contains(QStringLiteral("12  Defying Gravity")));
        menu.typeKey(Qt::Key_2, Qt::NoModifier, QStringLiteral("2"));
        QCOMPARE(menu.visibleTitles().first(), QStringLiteral("12  Defying Gravity"));   // exact number first
        QVERIFY(menu.visibleTitles().indexOf(QStringLiteral("112  Dragon flyover")) > 0);

        // Space is GO everywhere else: in the key menu it does nothing.
        CommandMenu menu2(makeContext(b));
        menu2.openKeys();
        menu2.typeKey(Qt::Key_Space, Qt::NoModifier, QStringLiteral(" "));
        QCOMPARE(menu2.view(), CommandMenu::View::Keys);
        QVERIFY(!menu2.hasPending());

        menu2.show();
        QVERIFY(QTest::qWaitForWindowExposed(&menu2));
        menu2.typeKey(Qt::Key_Escape);
        QTRY_VERIFY(!menu2.isVisible());
        QCOMPARE(menu2.result(), int(QDialog::Rejected));
        QVERIFY(!menu2.hasPending());
    }

    void searchRanksRecentsAndRunsSecondary()
    {
        QWidget host;
        Bar b = makeBar(&host);
        auto ctx = makeContext(b);
        QUuid stood, edited;
        ctx.standByCue = [&](const QUuid &id) { stood = id; };
        ctx.editCue = [&](const QUuid &id) { edited = id; };

        CommandMenu menu(ctx);
        menu.openSearch(QStringLiteral("defy"));
        QCOMPARE(menu.visibleTitles().first(), QStringLiteral("12  Defying Gravity"));
        QVERIFY(menu.currentItem());
        QCOMPARE(menu.currentItem()->secondaryLabel, QStringLiteral("Open in editor"));
        menu.typeKey(Qt::Key_Return, Qt::ControlModifier);
        QVERIFY(menu.hasPending());
        menu.takePending()();
        QCOMPARE(edited, ctx.cues[1].id);
        QVERIFY(stood.isNull());

        CommandMenu menu2(ctx);
        menu2.openSearch(QStringLiteral("gravity"));
        menu2.typeKey(Qt::Key_Return);
        menu2.takePending()();
        QCOMPARE(stood, ctx.cues[1].id);

        // The pick is remembered: it heads the empty-query list and gets a
        // boost on a loose query.
        CommandMenu menu3(ctx);
        menu3.openSearch();
        QCOMPARE(menu3.visibleTitles().first(), QStringLiteral("12  Defying Gravity"));
        // Same tier as "112  Dragon flyover" (both start a word with D); the
        // recent pick wins. Delete and Dark theme are prefix matches and
        // rightly stay above both.
        menu3.openSearch(QStringLiteral("d"));
        QVERIFY(menu3.visibleTitles().indexOf(QStringLiteral("12  Defying Gravity"))
                < menu3.visibleTitles().indexOf(QStringLiteral("112  Dragon flyover")));

        // Arrow keys move over headers; Backspace on an empty search goes
        // to the key menu.
        menu3.openSearch();
        const int before = menu3.currentRow();
        menu3.typeKey(Qt::Key_Down);
        QCOMPARE(menu3.currentRow(), before + 1);
        menu3.typeKey(Qt::Key_Up);
        QCOMPARE(menu3.currentRow(), before);
        menu3.typeKey(Qt::Key_Backspace);
        QCOMPARE(menu3.view(), CommandMenu::View::Keys);
    }

    void disabledItemsSinkAndDoNotRun()
    {
        QWidget host;
        Bar b = makeBar(&host);
        b.newAudio->setEnabled(false);
        QSignalSpy spy(b.newAudio, &QAction::triggered);
        CommandMenu menu(makeContext(b));
        menu.openSearch(QStringLiteral("audio"));
        QVERIFY(menu.visibleTitles().contains(QStringLiteral("New Audio")));
        QVERIFY(menu.visibleTitles().first() != QStringLiteral("New Audio"));   // sunk below the enabled ones
        while (menu.currentItem() && menu.currentItem()->title != QStringLiteral("New Audio"))
            menu.typeKey(Qt::Key_Down);
        QVERIFY(menu.currentItem());
        menu.typeKey(Qt::Key_Return);
        QVERIFY(!menu.hasPending());
        QCOMPARE(spy.count(), 0);
        QVERIFY(menu.footerText().contains(QStringLiteral("isn't available")));
    }

    void pinsAndChords()
    {
        QWidget host;
        Bar b = makeBar(&host);
        QSignalSpy spy(b.desk, &QAction::triggered);
        CommandMenu menu(makeContext(b));
        menu.openSearch(QStringLiteral("lighting desk"));
        QCOMPARE(menu.currentItem()->title, QStringLiteral("Lighting Desk…"));
        menu.typeKey(Qt::Key_P, Qt::ControlModifier);
        QVERIFY(menu.footerText().contains(QStringLiteral("Leader + ?")));
        menu.typeKey(Qt::Key_D, Qt::NoModifier, QStringLiteral("d"));
        QCOMPARE(CommandMenuSettings::pins().size(), 1);
        QCOMPARE(CommandMenuSettings::pins().first().letter, QChar('D'));
        QCOMPARE(CommandMenuSettings::pins().first().title, QStringLiteral("Lighting Desk…"));
        QVERIFY(!menu.hasPending());

        CommandMenu menu2(makeContext(b));
        QVERIFY(menu2.runChord(QChar('d')));
        menu2.takePending()();
        QCOMPARE(spy.count(), 1);
        QVERIFY(!menu2.runChord(QChar('Q')));
        CommandMenuSettings::removePin(QChar('D'));
        QVERIFY(CommandMenuSettings::pins().isEmpty());
    }

    // ── The leader key ──
    void leaderTapAndChord()
    {
        QMainWindow w;
        auto *focusable = new QLabel(QStringLiteral("cue list"), &w);
        focusable->setFocusPolicy(Qt::StrongFocus);
        w.setCentralWidget(focusable);
        ShortcutManager mgr;
        LeaderKey leader(&mgr, &w);
        auto *newAudio = new QAction(QStringLiteral("New Audio"), &w);
        newAudio->setShortcut(QKeySequence(Qt::Key_A));
        newAudio->setShortcutContext(Qt::WindowShortcut);
        w.addAction(newAudio);
        QSignalSpy tapped(&leader, &LeaderKey::tapped), chord(&leader, &LeaderKey::chord),
                   audio(newAudio, &QAction::triggered);
        QVERIFY(activate(w));
        focusable->setFocus();
        QCOMPARE(leader.key(), QKeySequence(Qt::Key_QuoteLeft));

        QTest::keyPress(&w, '`');
        QVERIFY(leader.isHeld());
        QTest::keyRelease(&w, Qt::Key_QuoteLeft);
        QTRY_COMPARE(tapped.count(), 1);
        QCOMPARE(chord.count(), 0);

        QTest::keyPress(&w, '`');
        QTest::keyPress(&w, 'c');
        QTest::keyRelease(&w, Qt::Key_C);
        QTest::keyRelease(&w, Qt::Key_QuoteLeft);
        QTRY_COMPARE(chord.count(), 1);
        QCOMPARE(chord.first().first().toChar(), QChar('C'));
        QCOMPARE(tapped.count(), 1);                          // a chord isn't a tap

        // Held + A is a chord, never the cue list's bare-letter shortcut.
        QTest::keyPress(&w, '`');
        QTest::keyPress(&w, 'a');
        QTest::keyRelease(&w, Qt::Key_A);
        QTest::keyRelease(&w, Qt::Key_QuoteLeft);
        QTRY_COMPARE(chord.count(), 2);
        QCOMPARE(audio.count(), 0);
        QTest::keyClick(&w, Qt::Key_A, Qt::NoModifier);        // without the leader it still works
        QCOMPARE(audio.count(), 1);

        // Chords off: letters are swallowed, the release still opens the menu.
        CommandMenuSettings::setChordsEnabled(false);
        QTest::keyPress(&w, '`');
        QTest::keyPress(&w, 'a');
        QTest::keyRelease(&w, Qt::Key_A);
        QTest::keyRelease(&w, Qt::Key_QuoteLeft);
        QTRY_COMPARE(tapped.count(), 2);
        QCOMPARE(chord.count(), 2);
        QCOMPARE(audio.count(), 1);
        CommandMenuSettings::setChordsEnabled(true);
    }

    void leaderLeavesTextFieldsAlone()
    {
        QMainWindow w;
        auto *edit = new QLineEdit(&w);
        w.setCentralWidget(edit);
        ShortcutManager mgr;
        LeaderKey leader(&mgr, &w);
        QSignalSpy tapped(&leader, &LeaderKey::tapped), chord(&leader, &LeaderKey::chord);
        QVERIFY(activate(w));
        edit->setFocus();
        QTest::keyClick(edit, Qt::Key_QuoteLeft, Qt::NoModifier);
        QTest::keyClick(edit, Qt::Key_A, Qt::NoModifier);
        QCOMPARE(edit->text(), QStringLiteral("`a"));
        QTest::qWait(20);
        QCOMPARE(tapped.count(), 0);
        QCOMPARE(chord.count(), 0);
        QVERIFY(LeaderKey::isTextEntry(edit));
        QVERIFY(!LeaderKey::isTextEntry(&w));
    }

    void leaderIsRebindableFromSettings()
    {
        QMainWindow w;
        auto *focusable = new QLabel(QStringLiteral("x"), &w);
        focusable->setFocusPolicy(Qt::StrongFocus);
        w.setCentralWidget(focusable);
        ShortcutManager mgr;
        LeaderKey leader(&mgr, &w);
        QSignalSpy tapped(&leader, &LeaderKey::tapped);
        QSettings s(QStringLiteral("ServeGaming"), QStringLiteral("quewi"));
        s.setValue(QStringLiteral("shortcuts/commandmenu.leader"), QStringLiteral("F12"));
        leader.resyncFromSettings();
        QCOMPARE(leader.key(), QKeySequence(Qt::Key_F12));
        QVERIFY(activate(w));
        focusable->setFocus();
        QTest::keyPress(&w, Qt::Key_F12);
        QTest::keyRelease(&w, Qt::Key_F12);
        QTRY_COMPARE(tapped.count(), 1);
        QTest::keyClick(&w, '`');
        QTest::qWait(20);
        QCOMPARE(tapped.count(), 1);
    }

    // ── Preferences → Command menu ──
    void preferencesPageEditsTheSettings()
    {
        CommandMenuSettings::setPin(QChar('D'), QStringLiteral("action:Tools/Lighting Desk…"), QStringLiteral("Lighting Desk…"));
        PreferencesDialog dlg(nullptr, nullptr);
        dlg.showPage(QStringLiteral("Command menu"));
        auto *leader = dlg.findChild<QComboBox *>(QStringLiteral("prefLeaderKey"));
        auto *chords = dlg.findChild<QCheckBox *>(QStringLiteral("prefLeaderChords"));
        auto *pins = dlg.findChild<QListWidget *>(QStringLiteral("prefPinnedChords"));
        auto *clear = dlg.findChild<QPushButton *>(QStringLiteral("prefClearPins"));
        QVERIFY(leader && chords && pins && clear);
        QCOMPARE(leader->currentData().toString(), LeaderKey::defaultKey().toString());
        QVERIFY(chords->isChecked());
        QCOMPARE(pins->count(), 1);
        QVERIFY(pins->item(0)->text().contains(QStringLiteral("Leader + D")));

        leader->setCurrentIndex(leader->findData(QKeySequence(QStringLiteral("F12")).toString()));
        QSettings s(QStringLiteral("ServeGaming"), QStringLiteral("quewi"));
        QCOMPARE(s.value(QStringLiteral("shortcuts/commandmenu.leader")).toString(), QStringLiteral("F12"));
        chords->setChecked(false);
        QVERIFY(!CommandMenuSettings::chordsEnabled());
        clear->click();
        QVERIFY(CommandMenuSettings::pins().isEmpty());
        QCOMPARE(pins->count(), 1);                                  // the "(none)" placeholder
        QVERIFY(!(pins->item(0)->flags() & Qt::ItemIsEnabled));
    }

    // ── Design checks (not an assertion) ──
    void renderPngs()
    {
        const QString dir = qEnvironmentVariable("QUEWI_RENDER_DIR");
        if (dir.isEmpty()) QSKIP("QUEWI_RENDER_DIR not set");
        QDir().mkpath(dir);
        qApp->setStyleSheet(Theme::load(QStringLiteral("quewi-dark")));

        // A stand-in for the main window: menu bar, tab strip, cue list with
        // real cues, Inspector dock, transport bar.
        core::Workspace ws;
        core::CueListModel model;
        QMainWindow w;
        w.resize(1280, 800);
        Bar b = makeBar(&w);
        w.setMenuBar(b.bar);
        auto *list = ws.addCueList(std::make_unique<core::CueList>(QStringLiteral("Act 1")));
        struct Row { const char *type; double n; const char *name; };
        const Row rows[] = {
            {"memo", 1, "House to half"}, {"audio", 2, "Preshow music"}, {"light", 3, "LX 1 — preset"},
            {"audio", 10, "Overture"}, {"audio", 11, "No One Mourns the Wicked"}, {"fade", 11.5, "Fade underscore"},
            {"audio", 12, "Defying Gravity"}, {"light", 12.1, "LX 42 — fly"}, {"wait", 12.5, "Wait 3 s"},
            {"audio", 13, "Thunder"}, {"memo", 14, "INTERVAL"}, {"audio", 20, "Entr'acte"},
        };
        QList<CommandContext::CueEntry> cues;
        for (const auto &r : rows) {
            std::unique_ptr<cues::Cue> c;
            const QString t = QLatin1String(r.type);
            if (t == QLatin1String("audio"))      c = std::make_unique<audio::AudioCue>();
            else if (t == QLatin1String("light")) c = std::make_unique<lighting::LightCue>();
            else if (t == QLatin1String("fade"))  c = std::make_unique<cues::FadeCue>();
            else if (t == QLatin1String("wait"))  c = std::make_unique<cues::WaitCue>();
            else                                  c = std::make_unique<cues::MemoCue>();
            c->setField(QStringLiteral("number"), r.n);
            c->setField(QStringLiteral("name"), QString::fromUtf8(r.name));
            cues.append({c->id(), r.n, QString::fromUtf8(r.name), c->typeName(), t == QLatin1String("audio")});
            list->insertCue(list->cueCount(), std::move(c));
        }
        ws.setActiveCueList(list);
        model.setCueList(list);

        auto *central = new QWidget(&w);
        auto *col = new QVBoxLayout(central);
        col->setContentsMargins(8, 8, 8, 8);
        col->setSpacing(8);
        auto *tabs = new QTabBar(central);
        tabs->setObjectName(QStringLiteral("cueListTabs"));
        tabs->setExpanding(false);
        tabs->setDocumentMode(true);
        tabs->addTab(QStringLiteral("Act 1"));
        tabs->addTab(QStringLiteral("Act 2"));
        tabs->addTab(QStringLiteral("♪ Soundboard"));
        col->addWidget(tabs, 0);
        auto *filter = new QLineEdit(central);
        filter->setObjectName(QStringLiteral("cueListFilter"));
        filter->setPlaceholderText(QStringLiteral("Filter cues — name, type, or number"));
        col->addWidget(filter, 0);
        auto *view = new CueListView(central);
        view->setWorkspace(&ws);
        view->setModel(&model);
        view->setCurrentIndex(model.index(6, 0));
        col->addWidget(view, 1);
        auto *transport = new TransportBar(central);
        col->addWidget(transport, 0);
        w.setCentralWidget(central);
        auto *inspector = new Inspector(&w);
        inspector->setWorkspace(&ws);
        inspector->setCue(list->cueAt(6));
        auto *dock = new QDockWidget(QStringLiteral("Inspector"), &w);
        dock->setWidget(inspector);
        w.addDockWidget(Qt::RightDockWidgetArea, dock);
        w.resizeDocks({dock}, {420}, Qt::Horizontal);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        QTest::qWait(150);

        CommandContext ctx = makeContext(b);
        ctx.cues = cues;
        ctx.editCue = [](const QUuid &) {};
        CommandMenuSettings::noteRecent(QStringLiteral("action:Tools/Pre-flight…"));
        CommandMenuSettings::noteRecent(QStringLiteral("cue:%1").arg(cues[6].id.toString(QUuid::WithoutBraces)));
        CommandMenuSettings::setPin(QChar('D'), QStringLiteral("action:Tools/Lighting Desk…"), QStringLiteral("Lighting Desk…"));

        struct Scene { const char *name; std::function<void(CommandMenu &)> open; };
        const Scene scenes[] = {
            {"search-empty",  [](CommandMenu &m) { m.openSearch(); }},
            {"search-query",  [](CommandMenu &m) { m.openSearch(QStringLiteral("defy")); }},
            {"search-action", [](CommandMenu &m) { m.openSearch(QStringLiteral("light")); }},
            {"keys-top",      [](CommandMenu &m) { m.openKeys(); }},
            {"keys-cue",      [](CommandMenu &m) { m.openKeys(QStringLiteral("C")); }},
            {"keys-goto",     [](CommandMenu &m) { m.openKeys(QStringLiteral("G")); }},
        };
        const QPixmap base = w.grab();
        for (const auto &sc : scenes) {
            CommandMenu menu(ctx, &w);
            sc.open(menu);
            menu.show();
            QVERIFY(QTest::qWaitForWindowExposed(&menu));
            QTest::qWait(80);
            QImage out = base.toImage().convertToFormat(QImage::Format_ARGB32);
            QPainter p(&out);
            p.drawPixmap(menu.pos() - w.mapToGlobal(QPoint(0, 0)), menu.grab());
            p.end();
            out.save(QStringLiteral("%1/command-menu-%2.png").arg(dir, QLatin1String(sc.name)));
            menu.hide();
        }
        wipeSettings();
    }
};

QTEST_MAIN(CommandMenuTests)
#include "test_command_menu.moc"
