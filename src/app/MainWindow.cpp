#include "MainWindow.h"

#include "GoEngine.h"
#include "UpdateChecker.h"
#include "UpdateInstaller.h"

#include <QProgressDialog>

#include <QDesktopServices>

#include "audio/AudioCue.h"
#include "audio/AudioEngine.h"
#include "core/CartGrid.h"
#include "core/CueList.h"
#include "core/CueListModel.h"
#include "core/UndoCommands.h"
#include "core/Workspace.h"
#include "cues/FadeCue.h"
#include "cues/GroupCue.h"
#include "cues/MemoCue.h"
#include "cues/TargetingCue.h"
#include "cues/WaitCue.h"
#include "lighting/LightCue.h"
#include "lighting/LightingEngine.h"
#include "midi/MidiCue.h"
#include "midi/MidiEngine.h"
#include "midi/MidiInputEngine.h"
#include "osc/OscCue.h"
#include "osc/OscEngine.h"
#include "show/ShowFile.h"
#include "video/VideoCue.h"
#include "video/VideoEngine.h"
#include "ui/AboutDialog.h"
#include "ui/ActiveCuesPanel.h"
#include "ui/CartView.h"
#include "ui/AudioEditorWindow.h"
#include "ui/CommandPalette.h"
#include "ui/Notifications.h"
#include "ui/NotificationsDialog.h"
#include "ui/FindReplaceDialog.h"
#include "ui/ShortcutManager.h"
#include "ui/ShortcutsDialog.h"
#include "ui/ScriptWindow.h"
#include "ui/Theme.h"
#include "ui/CueListView.h"
#include "ui/PreflightDialog.h"
#include "ui/Inspector.h"
#include "ui/OscMonitor.h"
#include "ui/PatchEditorDialog.h"
#include "ui/ProjectionMappingDialog.h"
#include "ui/SpeakerPatchDialog.h"
#include "ui/PreferencesDialog.h"
#include "ui/TransportBar.h"

#include "core/PatchManager.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QAudioDevice>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMediaDevices>
#include <QItemSelectionModel>
#include <QSet>
#include <QSettings>
#include <QKeySequence>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QDir>
#include <QCryptographicHash>
#include <QInputDialog>
#include <QLineEdit>
#include <QPushButton>
#include <QSplitter>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTimer>
#include <QUuid>
#include <QStatusBar>
#include <QTabBar>
#include <QUndoStack>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

namespace quewi {

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    resize(1280, 800);
    setMinimumSize(720, 480);
    setAcceptDrops(true);
    m_oscEngine = std::make_unique<osc::OscEngine>(this);
    connect(m_oscEngine.get(), &osc::OscEngine::sendError, this, [this](const QString &reason) {
        statusBar()->showMessage(tr("OSC: %1").arg(reason), 4000);
        ui::Notifications::instance().post(
            ui::Notifications::Level::Warn, QStringLiteral("OSC"), reason);
    });
    registerOscRemoteHandlers();
    // Default remote-control port. Documented in docs/osc-remote-api.md.
    constexpr quint16 kDefaultRemotePort = 53535;
    if (m_oscEngine->listenUdp(kDefaultRemotePort)) {
        statusBar()->showMessage(tr("OSC remote listening on UDP %1")
            .arg(kDefaultRemotePort), 3000);
    }
    m_audioEngine = std::make_unique<audio::AudioEngine>(this);
    connect(m_audioEngine.get(), &audio::AudioEngine::engineError, this,
            [this](const QString &reason) {
                statusBar()->showMessage(tr("Audio: %1").arg(reason), 5000);
                ui::Notifications::instance().post(
                    ui::Notifications::Level::Error, QStringLiteral("Audio"), reason);
            });
    {
        QSettings s(QStringLiteral("ServeGaming"), QStringLiteral("quewi"));
        const auto savedId = s.value(QStringLiteral("audio/defaultOutputDeviceId"))
                              .toByteArray();
        if (!savedId.isEmpty()) {
            for (const auto &dev : QMediaDevices::audioOutputs()) {
                if (dev.id() == savedId) {
                    m_audioEngine->setDefaultOutputDevice(dev);
                    break;
                }
            }
        }
    }
    m_lightingEngine = std::make_unique<lighting::LightingEngine>(this);
    m_videoEngine    = std::make_unique<video::VideoEngine>(this);
    // Apply any projection-mapping warps the user saved last session
    // before any cue fires — the windows are hidden until the first
    // visual cue lands but the per-screen quads are pre-loaded.
    ui::ProjectionMappingDialog::applyPersistedQuads(m_videoEngine.get());
    m_midiEngine     = std::make_unique<midi::MidiEngine>(this);
    m_midiInput      = std::make_unique<midi::MidiInputEngine>(this);
    connect(m_midiInput.get(), &midi::MidiInputEngine::messageReceived,
            this, &MainWindow::onMidiTrigger);
    {
        // Open the persisted input port (set in Preferences). Ignored if
        // the device isn't connected; the user can pick it again later.
        QSettings s(QStringLiteral("ServeGaming"), QStringLiteral("quewi"));
        const auto port = s.value(QStringLiteral("midi/inputPort")).toString();
        if (!port.isEmpty()) m_midiInput->openPort(port);
    }

    m_goEngine = std::make_unique<GoEngine>(this);
    m_goEngine->setAudioEngine(m_audioEngine.get());
    m_goEngine->setLightingEngine(m_lightingEngine.get());
    m_goEngine->setVideoEngine(m_videoEngine.get());
    m_goEngine->setOscEngine(m_oscEngine.get());
    m_goEngine->setMidiEngine(m_midiEngine.get());
    connect(m_goEngine.get(), &GoEngine::statusMessage, this,
            [this](const QString &m) { statusBar()->showMessage(m, 2500); });
    connect(m_goEngine.get(), &GoEngine::gotoRequested, this,
            [this](core::CueId id) {
                if (!m_workspace) return;
                auto *list = m_workspace->activeCueList();
                if (!list) return;
                for (int row = 0; row < list->cueCount(); ++row) {
                    if (auto *c = list->cueAt(row); c && c->id() == id) {
                        m_cueListView->setCurrentIndex(m_model->index(row, 0));
                        return;
                    }
                }
            });

    m_shortcuts = new ui::ShortcutManager(this);

    // Transport QActions — rebindable. Spacebar GO is also still wired
    // directly on the cue list view as a key event (so it works when the
    // cue list has focus regardless of the global shortcut).
    m_actGo = new QAction(tr("GO"), this);
    addAction(m_actGo);
    m_actGo->setShortcutContext(Qt::WindowShortcut);
    connect(m_actGo, &QAction::triggered, this, &MainWindow::onGoRequested);

    m_actPanic = new QAction(tr("Panic"), this);
    addAction(m_actPanic);
    m_actPanic->setShortcutContext(Qt::ApplicationShortcut);
    connect(m_actPanic, &QAction::triggered, this, [this] {
        if (m_goEngine) m_goEngine->cancelAll(0.05);
        statusBar()->showMessage(tr("PANIC: all output stopped"), 2000);
    });

    m_actPause = new QAction(tr("Pause"), this);
    addAction(m_actPause);
    m_actPause->setShortcutContext(Qt::ApplicationShortcut);
    connect(m_actPause, &QAction::triggered, this, [this] {
        if (m_goEngine) m_goEngine->cancelAll(0.25);
        statusBar()->showMessage(tr("Paused (cancels pending continues)"), 2500);
    });

    m_actFadeAll = new QAction(tr("Fade All"), this);
    addAction(m_actFadeAll);
    m_actFadeAll->setShortcutContext(Qt::ApplicationShortcut);
    connect(m_actFadeAll, &QAction::triggered, this, [this] {
        if (m_goEngine) m_goEngine->cancelAll(2.0);
        statusBar()->showMessage(tr("Fade All: 2 s fade-out across every voice"), 3000);
    });

    // Register every meaningful shortcut. Defaults match what was
    // previously hard-coded; users can rebind via Tools → Shortcuts.
    auto regAction = [this](const char *id, const char *label, QAction *a, const QKeySequence &def) {
        if (!a) return;
        m_shortcuts->registerAction(QString::fromLatin1(id), tr(label), a, def);
    };
    regAction("transport.go",       "GO",       m_actGo,      QKeySequence(Qt::Key_Space));
    regAction("transport.panic",    "Panic",    m_actPanic,   QKeySequence(QStringLiteral("Esc")));
    regAction("transport.pause",    "Pause",    m_actPause,   QKeySequence(QStringLiteral("Ctrl+.")));
    regAction("transport.fadeall",  "Fade All", m_actFadeAll, QKeySequence(QStringLiteral("Ctrl+Shift+.")));

    buildLayout();
    buildMenus();

    resetWorkspace();
    statusBar()->showMessage(tr("Ready"));

    // Notification badge — clickable QPushButton on the right side of
    // the status bar. Shows the unread count whenever post() lands in
    // Notifications::instance() while the inbox isn't open.
    m_notifBadge = new QPushButton(this);
    m_notifBadge->setFlat(true);
    m_notifBadge->setCursor(Qt::PointingHandCursor);
    m_notifBadge->setStyleSheet(QStringLiteral(
        "QPushButton { color: #D7A24E; padding: 2px 8px; border: none; "
        "background: transparent; }"
        "QPushButton:hover { color: #E8C861; }"));
    m_notifBadge->setVisible(false);
    statusBar()->addPermanentWidget(m_notifBadge);
    connect(m_notifBadge, &QPushButton::clicked,
            this, &MainWindow::showNotifications);
    connect(&ui::Notifications::instance(), &ui::Notifications::posted,
            this, [this](const ui::Notifications::Entry &e) {
                ++m_unreadNotifs;
                refreshNotifBadge();
                if (e.level == ui::Notifications::Level::Error)
                    statusBar()->showMessage(tr("⚠ %1").arg(e.message), 4000);
            });

    // Offer recovery after the main window is on screen.
    QTimer::singleShot(0, this, &MainWindow::recoverFromJournalIfPresent);

    // Silent update check on startup. Three-second delay so it doesn't
    // contend with the cold-start path; the user-facing dialog only
    // appears if a newer release is actually published.
    QSettings s(QStringLiteral("ServeGaming"), QStringLiteral("quewi"));
    if (s.value(QStringLiteral("update/checkOnStartup"), true).toBool()) {
        QTimer::singleShot(3000, this, [this]{ checkForUpdates(false); });
    }
}

MainWindow::~MainWindow() = default;

void MainWindow::buildLayout()
{
    auto *central = new QWidget(this);
    auto *centralCol = new QVBoxLayout(central);
    centralCol->setContentsMargins(0, 0, 0, 0);
    centralCol->setSpacing(0);

    // Show Mode strip — full-bleed across the top so it can't be missed.
    m_showModeStrip = new QWidget(central);
    m_showModeStrip->setObjectName(QStringLiteral("showModeStrip"));
    {
        auto *hb = new QHBoxLayout(m_showModeStrip);
        hb->setContentsMargins(0, 0, 0, 0);
        hb->setSpacing(0);
        auto *lbl = new QLabel(tr("Show Mode  ·  Operator controls only"),
                               m_showModeStrip);
        lbl->setObjectName(QStringLiteral("showModeStripLabel"));
        lbl->setAlignment(Qt::AlignCenter);
        hb->addWidget(lbl);
    }
    m_showModeStrip->hide();
    centralCol->addWidget(m_showModeStrip, 0);

    // The actual content area is a margined container so the panes sit
    // as floating cards on the deep-bg canvas.
    auto *content = new QWidget(central);
    centralCol->addWidget(content, 1);
    auto *outer = new QVBoxLayout(content);
    outer->setContentsMargins(8, 8, 8, 8);
    outer->setSpacing(8);

    m_listTabs = new QTabBar(central);
    m_listTabs->setObjectName(QStringLiteral("cueListTabs"));
    m_listTabs->setExpanding(false);
    m_listTabs->setDocumentMode(true);
    m_listTabs->setTabsClosable(false);
    m_listTabs->setMovable(false);
    connect(m_listTabs, &QTabBar::currentChanged, this, &MainWindow::onTabSelected);
    connect(m_listTabs, &QTabBar::tabBarDoubleClicked, this, [this](int){ renameCueListTab(); });

    m_mainSplitter = new QSplitter(Qt::Horizontal, central);
    m_mainSplitter->setHandleWidth(8); // QSS width hint isn't always honoured

    // Cue list pane: filter line on top, view fills the rest. Wrapping
    // them in one widget keeps the splitter happy (it lays out per
    // child widget, not per pair).
    auto *cuePane = new QWidget(m_mainSplitter);
    cuePane->setMinimumWidth(280);
    auto *cuePaneV = new QVBoxLayout(cuePane);
    cuePaneV->setContentsMargins(0, 0, 0, 0);
    cuePaneV->setSpacing(4);

    auto *filterEdit = new QLineEdit(cuePane);
    filterEdit->setObjectName(QStringLiteral("cueListFilter"));
    filterEdit->setPlaceholderText(tr("Filter cues — name, type, or number"));
    filterEdit->setClearButtonEnabled(true);

    m_cueListView = new ui::CueListView(cuePane);
    cuePaneV->addWidget(filterEdit);
    cuePaneV->addWidget(m_cueListView, 1);
    connect(filterEdit, &QLineEdit::textChanged, m_cueListView,
            &ui::CueListView::setFilterText);

    // Cart view sits side-by-side with the cue list inside a
    // QStackedWidget. The View menu toggles which one's visible —
    // they share the workspace data, so the same GO logic, undo
    // stack, and inspector all work in either mode.
    m_cartView = new ui::CartView(m_mainSplitter);
    connect(m_cartView, &ui::CartView::fireRequested, this,
        [this](cues::Cue *c) { if (m_goEngine && c) m_goEngine->fire(c); });
    connect(m_cartView, &ui::CartView::fileDropped,
            this, &MainWindow::onCartFileDropped);

    m_centerStack = new QStackedWidget(m_mainSplitter);
    m_centerStack->addWidget(cuePane);     // index 0 = list view
    m_centerStack->addWidget(m_cartView);  // index 1 = cart view

    m_inspector = new ui::Inspector(m_mainSplitter);

    m_mainSplitter->addWidget(m_centerStack);
    m_mainSplitter->addWidget(m_inspector);
    m_mainSplitter->setStretchFactor(0, 3);
    m_mainSplitter->setStretchFactor(1, 2);
    m_mainSplitter->setSizes({800, 480});
    m_mainSplitter->setChildrenCollapsible(false);

    m_activePanel = new ui::ActiveCuesPanel(central);
    m_activePanel->setAudioEngine(m_audioEngine.get());
    m_activePanel->hide(); // shown when something starts playing
    connect(m_activePanel, &ui::ActiveCuesPanel::runningCueIdsChanged,
            this, [this](const QSet<QUuid> &ids) {
                if (m_model) m_model->setRunningCueIds(ids);
            });
    connect(m_activePanel, &ui::ActiveCuesPanel::peakLevelsChanged,
            this, [this](const QHash<QUuid, QPair<float, float>> &peaks) {
                if (m_model) m_model->setPeakLevels(peaks);
            });

    m_transport = new ui::TransportBar(central);

    outer->addWidget(m_listTabs, 0);
    outer->addWidget(m_mainSplitter, 1);
    outer->addWidget(m_activePanel, 0);
    outer->addWidget(m_transport, 0);

    setCentralWidget(central);

    connect(m_cueListView, &ui::CueListView::currentCueChanged,
            m_inspector,   &ui::Inspector::setCue);
    connect(m_cueListView, &ui::CueListView::currentCueChanged,
            this, [this](cues::Cue *) { onSelectionChanged(); });
    connect(m_cueListView, &ui::CueListView::goRequested,
            this, &MainWindow::onGoRequested);
    connect(m_cueListView, &ui::CueListView::filesDropped, this,
            [this](const QList<QUrl> &urls, int insertRow) {
                const int created = insertCuesFromUrls(urls, insertRow);
                if (created > 0) {
                    statusBar()->showMessage(tr("Added %1 cue%2 from drop")
                        .arg(created).arg(created == 1 ? QString() : QStringLiteral("s")), 2500);
                }
            });
    connect(m_cueListView, &ui::CueListView::cueDoubleClicked, this,
        [this](cues::Cue *cue) {
            if (auto *ac = qobject_cast<audio::AudioCue *>(cue)) {
                ac->prepare();
                auto *editor = new ui::AudioEditorWindow(ac, this);
                editor->show();
            }
        });
    // Right-click → Insert Above/Below — drops a Memo at the chosen
    // row. Memo is the no-op cue, fastest to retype into anything else
    // via the inspector. Opening a full picker dialog feels heavy for
    // a context-menu action.
    connect(m_cueListView, &ui::CueListView::insertRequested, this,
        [this](int row) {
            auto *list = m_workspace ? m_workspace->activeCueList() : nullptr;
            if (!list) return;
            const int target = std::clamp(row, 0, list->cueCount());
            auto cue = std::make_unique<cues::MemoCue>();
            cue->setField(QStringLiteral("name"), tr("New cue"));
            cue->setField(QStringLiteral("number"), static_cast<double>(target + 1));
            m_workspace->undoStack()->push(
                new core::InsertCueCommand(list, target, std::move(cue)));
            if (m_model->rowCount() > target)
                m_cueListView->setCurrentIndex(m_model->index(target, 0));
        });
    // Transport bar buttons trigger the rebindable QActions so they
    // share one source of truth with the keyboard shortcuts.
    connect(m_transport, &ui::TransportBar::goPressed,
            m_actGo,    &QAction::trigger);
    connect(m_transport, &ui::TransportBar::panicPressed,
            m_actPanic, &QAction::trigger);
    connect(m_transport, &ui::TransportBar::pausePressed,
            m_actPause, &QAction::trigger);
    connect(m_transport, &ui::TransportBar::fadeAllPressed,
            m_actFadeAll, &QAction::trigger);
}

void MainWindow::buildMenus()
{
    auto *file = menuBar()->addMenu(tr("&File"));
    file->addAction(tr("&New"),    QKeySequence::New,    this, &MainWindow::newShow);
    file->addAction(tr("&Open…"),  QKeySequence::Open,   this, &MainWindow::openShow);
    m_recentMenu = file->addMenu(tr("Open &Recent"));
    rebuildRecentMenu();
    file->addSeparator();
    m_actSave = file->addAction(tr("&Save"), QKeySequence::Save, this, [this]{ saveShow(); });
    file->addAction(tr("Save &As…"), QKeySequence::SaveAs, this, [this]{ saveShowAs(); });
    file->addSeparator();
    file->addAction(tr("&Preferences…"), this, &MainWindow::showPreferences);
    file->addAction(tr("Check for &updates…"),
                    this, [this]{ checkForUpdates(true); });
    file->addSeparator();
    file->addAction(tr("E&xit"), QKeySequence::Quit, this, &QWidget::close);

    auto *edit = menuBar()->addMenu(tr("&Edit"));
    m_actUndo = edit->addAction(tr("&Undo"));
    m_actUndo->setShortcut(QKeySequence::Undo);
    m_actRedo = edit->addAction(tr("&Redo"));
    m_actRedo->setShortcut(QKeySequence::Redo);

    auto *toolsMenu = menuBar()->addMenu(tr("&Tools"));
    toolsMenu->addAction(tr("&Pre-flight…"),
                         QKeySequence(QStringLiteral("Ctrl+P")),
                         this, &MainWindow::showPreflight);
    toolsMenu->addAction(tr("&OSC Monitor…"),
                         QKeySequence(QStringLiteral("Ctrl+1")),
                         this, &MainWindow::showOscMonitor);
    toolsMenu->addAction(tr("&Command palette…"),
                         QKeySequence(QStringLiteral("Ctrl+K")),
                         this, &MainWindow::showCommandPalette);
    toolsMenu->addAction(tr("&Keyboard shortcuts…"),
                         this, &MainWindow::showShortcutsDialog);
    toolsMenu->addAction(tr("&Patch Editor…"),
                         QKeySequence(QStringLiteral("Ctrl+Shift+P")),
                         this, &MainWindow::showPatchEditor);
    toolsMenu->addAction(tr("&Speaker Patch…"),
                         this, &MainWindow::showSpeakerPatch);
    toolsMenu->addAction(tr("Pro&jection Mapping…"),
                         this, &MainWindow::showProjectionMapping);
    toolsMenu->addAction(tr("S&cript follower…"),
                         QKeySequence(QStringLiteral("Ctrl+Shift+S")),
                         this, &MainWindow::showScriptWindow);
    toolsMenu->addSeparator();
    m_actShowMode = toolsMenu->addAction(tr("&Show Mode (locked)"));
    m_actShowMode->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+L")));
    m_actShowMode->setCheckable(true);
    connect(m_actShowMode, &QAction::triggered, this, &MainWindow::toggleShowMode);

    auto *editMenuExt = menuBar()->actions().value(1) ? menuBar()->actions().at(1)->menu() : nullptr;
    if (editMenuExt) {
        editMenuExt->addSeparator();
        editMenuExt->addAction(tr("&Find / Replace…"),
                               QKeySequence(QStringLiteral("Ctrl+F")), this, [this] {
            ui::FindReplaceDialog dlg(m_workspace.get(), this);
            dlg.exec();
        });
    }

    auto *viewMenu = menuBar()->addMenu(tr("&View"));
    auto *themeMenu = viewMenu->addMenu(tr("&Theme"));
    auto applyTheme = [this](const QString &name) {
        const auto qss = ui::Theme::load(name);
        if (qss.isEmpty()) return;

        // setStyleSheet() re-polishes every visible widget on the GUI
        // thread — perceptibly choppy on a dense show. Suppress paints
        // while the swap happens so the user sees a single clean redraw
        // instead of widgets rebuilding piecemeal. The wait cursor
        // signals the brief hitch is intentional.
        QGuiApplication::setOverrideCursor(Qt::WaitCursor);
        setUpdatesEnabled(false);
        qApp->setStyleSheet(qss);
        setUpdatesEnabled(true);
        update();
        QGuiApplication::restoreOverrideCursor();

        QSettings s(QStringLiteral("ServeGaming"), QStringLiteral("quewi"));
        s.setValue(QStringLiteral("ui/theme"), name);
    };
    themeMenu->addAction(tr("&Dark"),  this, [applyTheme]{ applyTheme(QStringLiteral("quewi-dark")); });
    themeMenu->addAction(tr("&Light"), this, [applyTheme]{ applyTheme(QStringLiteral("quewi-light")); });
    themeMenu->addAction(tr("&High contrast"),
                         this, [applyTheme]{ applyTheme(QStringLiteral("quewi-highcontrast")); });

    viewMenu->addSeparator();
    auto *cartToggle = viewMenu->addAction(tr("&Cart view"));
    cartToggle->setCheckable(true);
    cartToggle->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+C")));
    connect(cartToggle, &QAction::toggled, this, [this](bool on) {
        if (m_centerStack) m_centerStack->setCurrentIndex(on ? 1 : 0);
        QSettings s(QStringLiteral("ServeGaming"), QStringLiteral("quewi"));
        s.setValue(QStringLiteral("ui/cartView"), on);
    });
    {
        QSettings s(QStringLiteral("ServeGaming"), QStringLiteral("quewi"));
        cartToggle->setChecked(s.value(QStringLiteral("ui/cartView"), false).toBool());
    }

    auto *listMenu = menuBar()->addMenu(tr("&List"));
    listMenu->addAction(tr("&New cue list…"),    this, &MainWindow::addCueListTab);
    listMenu->addAction(tr("&Rename current…"),  this, &MainWindow::renameCueListTab);
    listMenu->addAction(tr("&Remove current"),   this, &MainWindow::removeCueListTab);

    auto *cueMenu = menuBar()->addMenu(tr("&Cue"));
    cueMenu->addAction(tr("New &Memo"),       QKeySequence(Qt::Key_M), this, &MainWindow::insertMemoCue);
    cueMenu->addAction(tr("New &OSC"),        QKeySequence(Qt::Key_O), this, &MainWindow::insertOscCue);
    cueMenu->addAction(tr("New &Audio"),      QKeySequence(Qt::Key_A), this, &MainWindow::insertAudioCue);
    cueMenu->addAction(tr("New &Fade"),       QKeySequence(Qt::Key_F), this, &MainWindow::insertFadeCue);
    cueMenu->addAction(tr("New &Light"),      QKeySequence(Qt::Key_L), this, &MainWindow::insertLightCue);
    cueMenu->addAction(tr("New Light Fa&de"), QKeySequence(QStringLiteral("Shift+L")), this, &MainWindow::insertLightFadeCue);
    cueMenu->addAction(tr("New &Video"),      QKeySequence(Qt::Key_V), this, &MainWindow::insertVideoCue);
    cueMenu->addAction(tr("New &Image"),      QKeySequence(Qt::Key_I), this, &MainWindow::insertImageCue);
    cueMenu->addAction(tr("New &Text"),       QKeySequence(Qt::Key_T), this, &MainWindow::insertTextCue);
    cueMenu->addSeparator();
    cueMenu->addAction(tr("New &Wait"),  QKeySequence(Qt::Key_W),                       this, &MainWindow::insertWaitCue);
    cueMenu->addAction(tr("New S&tart"), QKeySequence(QStringLiteral("Shift+S")),       this, &MainWindow::insertStartCue);
    cueMenu->addAction(tr("New Sto&p"),  QKeySequence(QStringLiteral("Shift+X")),       this, &MainWindow::insertStopCue);
    cueMenu->addAction(tr("New &Goto"),  QKeySequence(QStringLiteral("Shift+G")),       this, &MainWindow::insertGotoCue);
    cueMenu->addAction(tr("New Pa&use"), this, &MainWindow::insertPauseCue);
    cueMenu->addAction(tr("New &Load"),  this, &MainWindow::insertLoadCue);
    cueMenu->addAction(tr("New Re&set"), this, &MainWindow::insertResetCue);
    cueMenu->addAction(tr("New Devam&p"), this, &MainWindow::insertDevampCue);
    cueMenu->addAction(tr("New Gr&oup"), QKeySequence(QStringLiteral("Ctrl+G")),        this, &MainWindow::insertGroupCue);
    cueMenu->addAction(tr("New &MIDI"),  QKeySequence(QStringLiteral("Shift+M")),       this, &MainWindow::insertMidiCue);
    cueMenu->addAction(tr("New M&SC"),   QKeySequence(QStringLiteral("Ctrl+Shift+M")),  this, &MainWindow::insertMscCue);
    cueMenu->addSeparator();
    cueMenu->addAction(tr("&Delete"), QKeySequence::Delete, this, &MainWindow::deleteSelectedCue);

    auto *helpMenu = menuBar()->addMenu(tr("&Help"));
    helpMenu->addAction(tr("&Keyboard shortcuts…"),
                        this, &MainWindow::showShortcutsDialog);
    helpMenu->addAction(tr("&Notifications…"),
                        this, &MainWindow::showNotifications);
    helpMenu->addSeparator();
    helpMenu->addAction(tr("&About quewi…"),
                        QKeySequence(QStringLiteral("Ctrl+?")),
                        this, &MainWindow::showAbout);
}

void MainWindow::resetWorkspace()
{
    m_workspace = std::make_unique<core::Workspace>();
    m_workspace->setName(tr("Untitled Show"));
    auto list = std::make_unique<core::CueList>(tr("Main"));
    m_workspace->addCueList(std::move(list));

    m_model = std::make_unique<core::CueListModel>();
    rebindModel();

    m_inspector->setWorkspace(m_workspace.get());
    m_inspector->setAudioEngine(m_audioEngine.get());
    m_inspector->setMidiEngine(m_midiEngine.get());
    if (m_activePanel) m_activePanel->setWorkspace(m_workspace.get());
    if (m_goEngine)    m_goEngine->setWorkspace(m_workspace.get());
    rebuildListTabs();
    connect(m_workspace->undoStack(), &QUndoStack::indexChanged,
            this, [this](int){ scheduleJournal(); });

    if (m_actUndo) m_actUndo->disconnect();
    if (m_actRedo) m_actRedo->disconnect();
    auto *stack = m_workspace->undoStack();
    connect(m_actUndo, &QAction::triggered, stack, &QUndoStack::undo);
    connect(m_actRedo, &QAction::triggered, stack, &QUndoStack::redo);
    m_actUndo->setEnabled(stack->canUndo());
    m_actRedo->setEnabled(stack->canRedo());
    connect(stack, &QUndoStack::canUndoChanged, m_actUndo, &QAction::setEnabled);
    connect(stack, &QUndoStack::canRedoChanged, m_actRedo, &QAction::setEnabled);
    connect(m_workspace.get(), &core::Workspace::dirtyChanged, this, &MainWindow::updateTitle);

    m_currentPath.clear();
    updateTitle();
    onSelectionChanged();
}

void MainWindow::rebindModel()
{
    auto *list = m_workspace->activeCueList();
    m_model->setCueList(list);
    m_cueListView->setWorkspace(m_workspace.get());
    m_cueListView->setModel(m_model.get());
    if (m_cartView) {
        m_cartView->setWorkspace(m_workspace.get());
        m_cartView->setGoEngine(m_goEngine.get());
    }
    if (m_model->rowCount() > 0)
        m_cueListView->setCurrentIndex(m_model->index(0, 0));
}

bool MainWindow::maybeSaveChanges()
{
    if (!m_workspace || !m_workspace->isDirty()) return true;
    const auto button = QMessageBox::question(this, tr("Save changes?"),
        tr("The current show has unsaved changes. Save before closing?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (button == QMessageBox::Cancel)  return false;
    if (button == QMessageBox::Discard) return true;
    return saveShow();
}

void MainWindow::newShow()
{
    if (!maybeSaveChanges()) return;
    resetWorkspace();
}

void MainWindow::openShow()
{
    if (!maybeSaveChanges()) return;
    const auto path = QFileDialog::getOpenFileName(this, tr("Open show"),
        QString(), tr("quewi shows (*.quewi);;All files (*.*)"));
    if (path.isEmpty()) return;
    loadShowFromPath(path);
}

bool MainWindow::loadShowFromPath(const QString &path)
{
    auto fresh = std::make_unique<core::Workspace>();
    if (!show::ShowFile::load(path, *fresh)) {
        QMessageBox::warning(this, tr("Open failed"), show::ShowFile::lastError());
        return false;
    }
    if (const auto warn = show::ShowFile::lastWarning(); !warn.isEmpty()) {
        ui::Notifications::instance().post(
            ui::Notifications::Level::Warn,
            QStringLiteral("Show file"), warn);
    }
    m_workspace = std::move(fresh);
    m_model = std::make_unique<core::CueListModel>();
    rebindModel();
    m_inspector->setWorkspace(m_workspace.get());
    m_inspector->setAudioEngine(m_audioEngine.get());
    m_inspector->setMidiEngine(m_midiEngine.get());
    if (m_activePanel) m_activePanel->setWorkspace(m_workspace.get());
    if (m_goEngine)    m_goEngine->setWorkspace(m_workspace.get());
    rebuildListTabs();
    connect(m_workspace->undoStack(), &QUndoStack::indexChanged,
            this, [this](int){ scheduleJournal(); });

    if (m_actUndo) m_actUndo->disconnect();
    if (m_actRedo) m_actRedo->disconnect();
    auto *stack = m_workspace->undoStack();
    connect(m_actUndo, &QAction::triggered, stack, &QUndoStack::undo);
    connect(m_actRedo, &QAction::triggered, stack, &QUndoStack::redo);
    m_actUndo->setEnabled(stack->canUndo());
    m_actRedo->setEnabled(stack->canRedo());
    connect(stack, &QUndoStack::canUndoChanged, m_actUndo, &QAction::setEnabled);
    connect(stack, &QUndoStack::canRedoChanged, m_actRedo, &QAction::setEnabled);
    connect(m_workspace.get(), &core::Workspace::dirtyChanged, this, &MainWindow::updateTitle);

    m_currentPath = path;
    updateTitle();
    onSelectionChanged();
    noteRecentFile(path);
    statusBar()->showMessage(tr("Opened %1").arg(path), 3000);
    return true;
}

bool MainWindow::saveTo(const QString &path)
{
    if (!show::ShowFile::save(path, *m_workspace)) {
        QMessageBox::warning(this, tr("Save failed"), show::ShowFile::lastError());
        return false;
    }
    m_workspace->markClean();
    m_currentPath = path;
    updateTitle();
    noteRecentFile(path);
    statusBar()->showMessage(tr("Saved %1").arg(path), 3000);
    clearJournal();
    return true;
}

bool MainWindow::saveShow()
{
    if (m_currentPath.isEmpty()) return saveShowAs();
    return saveTo(m_currentPath);
}

bool MainWindow::saveShowAs()
{
    auto path = QFileDialog::getSaveFileName(this, tr("Save show as"),
        m_currentPath, tr("quewi shows (*.quewi);;All files (*.*)"));
    if (path.isEmpty()) return false;
    if (!path.endsWith(QStringLiteral(".quewi"), Qt::CaseInsensitive))
        path += QStringLiteral(".quewi");
    return saveTo(path);
}

void MainWindow::showPreferences()
{
    ui::PreferencesDialog dlg(m_audioEngine.get(), m_midiInput.get(), this);
    connect(&dlg, &ui::PreferencesDialog::cueListColumnsChanged,
            this, [this] { if (m_cueListView) m_cueListView->applyColumnVisibility(); });
    dlg.exec();
}

void MainWindow::showPatchEditor()
{
    if (!m_workspace || !m_workspace->patches()) return;
    ui::PatchEditorDialog dlg(m_workspace->patches(), this);
    dlg.exec();
}

void MainWindow::showSpeakerPatch()
{
    if (!m_workspace || !m_workspace->patches()) return;
    ui::SpeakerPatchDialog dlg(m_workspace->patches(), this);
    dlg.exec();
}

void MainWindow::showProjectionMapping()
{
    if (!m_videoEngine) return;
    ui::ProjectionMappingDialog dlg(m_videoEngine.get(), this);
    dlg.exec();
}

void MainWindow::showOscMonitor()
{
    if (!m_oscMonitor) {
        // Independent top-level window — outlives any single show.
        m_oscMonitor = new ui::OscMonitor(m_oscEngine.get());
        m_oscMonitor->setAttribute(Qt::WA_DeleteOnClose, false);
    }
    m_oscMonitor->show();
    m_oscMonitor->raise();
    m_oscMonitor->activateWindow();
}

void MainWindow::showScriptWindow()
{
    if (!m_scriptWindow) {
        m_scriptWindow = new ui::ScriptWindow();
        m_scriptWindow->setAttribute(Qt::WA_DeleteOnClose, false);
        m_scriptWindow->setWorkspace(m_workspace.get());
        m_scriptWindow->setGoEngine(m_goEngine.get());
        // Keep the selected cue in sync so click-to-bind works.
        if (m_cueListView) {
            connect(m_cueListView, &ui::CueListView::currentCueChanged,
                    this, [this](cues::Cue *cue) {
                        if (m_scriptWindow)
                            m_scriptWindow->setSelectedCue(cue ? cue->id() : QUuid());
                    });
        }
    }
    // After resetWorkspace the m_workspace pointer is fresh — re-bind.
    m_scriptWindow->setWorkspace(m_workspace.get());
    m_scriptWindow->setGoEngine(m_goEngine.get());
    m_scriptWindow->show();
    m_scriptWindow->raise();
    m_scriptWindow->activateWindow();
}

void MainWindow::insertMemoCue()
{
    auto *list = m_workspace->activeCueList();
    if (!list) return;
    const auto idx = m_cueListView->currentIndex();
    const int insertRow = idx.isValid() ? idx.row() + 1 : list->cueCount();

    auto cue = std::make_unique<cues::MemoCue>();
    cue->setField(QStringLiteral("name"), tr("Memo"));
    cue->setField(QStringLiteral("number"), static_cast<double>(insertRow + 1));

    m_workspace->undoStack()->push(
        new core::InsertCueCommand(list, insertRow, std::move(cue)));

    if (m_model->rowCount() > insertRow)
        m_cueListView->setCurrentIndex(m_model->index(insertRow, 0));
}

void MainWindow::insertOscCue()
{
    auto *list = m_workspace->activeCueList();
    if (!list) return;
    const auto idx = m_cueListView->currentIndex();
    const int insertRow = idx.isValid() ? idx.row() + 1 : list->cueCount();

    auto cue = std::make_unique<osc::OscCue>();
    cue->setField(QStringLiteral("name"), tr("OSC"));
    cue->setField(QStringLiteral("number"), static_cast<double>(insertRow + 1));

    m_workspace->undoStack()->push(
        new core::InsertCueCommand(list, insertRow, std::move(cue)));

    if (m_model->rowCount() > insertRow)
        m_cueListView->setCurrentIndex(m_model->index(insertRow, 0));
}

void MainWindow::insertAudioCue()
{
    auto *list = m_workspace->activeCueList();
    if (!list) return;
    const auto idx = m_cueListView->currentIndex();
    const int insertRow = idx.isValid() ? idx.row() + 1 : list->cueCount();

    auto cue = std::make_unique<audio::AudioCue>();
    cue->setField(QStringLiteral("name"), tr("Audio"));
    cue->setField(QStringLiteral("number"), static_cast<double>(insertRow + 1));

    m_workspace->undoStack()->push(
        new core::InsertCueCommand(list, insertRow, std::move(cue)));

    if (m_model->rowCount() > insertRow)
        m_cueListView->setCurrentIndex(m_model->index(insertRow, 0));
}

void MainWindow::insertFadeCue()
{
    auto *list = m_workspace->activeCueList();
    if (!list) return;
    const auto idx = m_cueListView->currentIndex();
    const int insertRow = idx.isValid() ? idx.row() + 1 : list->cueCount();

    auto cue = std::make_unique<cues::FadeCue>();
    cue->setField(QStringLiteral("name"), tr("Fade"));
    cue->setField(QStringLiteral("number"), static_cast<double>(insertRow + 1));

    m_workspace->undoStack()->push(
        new core::InsertCueCommand(list, insertRow, std::move(cue)));

    if (m_model->rowCount() > insertRow)
        m_cueListView->setCurrentIndex(m_model->index(insertRow, 0));
}

void MainWindow::insertLightCue()
{
    auto *list = m_workspace->activeCueList();
    if (!list) return;
    const auto idx = m_cueListView->currentIndex();
    const int insertRow = idx.isValid() ? idx.row() + 1 : list->cueCount();
    auto cue = std::make_unique<lighting::LightCue>();
    cue->setField(QStringLiteral("name"), tr("Light"));
    cue->setField(QStringLiteral("number"), static_cast<double>(insertRow + 1));
    m_workspace->undoStack()->push(
        new core::InsertCueCommand(list, insertRow, std::move(cue)));
    if (m_model->rowCount() > insertRow)
        m_cueListView->setCurrentIndex(m_model->index(insertRow, 0));
}

void MainWindow::insertLightFadeCue()
{
    auto *list = m_workspace->activeCueList();
    if (!list) return;
    const auto idx = m_cueListView->currentIndex();
    const int insertRow = idx.isValid() ? idx.row() + 1 : list->cueCount();
    auto cue = std::make_unique<lighting::LightFadeCue>();
    cue->setField(QStringLiteral("name"), tr("Light Fade"));
    cue->setField(QStringLiteral("number"), static_cast<double>(insertRow + 1));
    m_workspace->undoStack()->push(
        new core::InsertCueCommand(list, insertRow, std::move(cue)));
    if (m_model->rowCount() > insertRow)
        m_cueListView->setCurrentIndex(m_model->index(insertRow, 0));
}

void MainWindow::insertVideoCue()
{
    auto *list = m_workspace->activeCueList();
    if (!list) return;
    const auto idx = m_cueListView->currentIndex();
    const int insertRow = idx.isValid() ? idx.row() + 1 : list->cueCount();
    auto cue = std::make_unique<video::VideoCue>();
    cue->setField(QStringLiteral("name"), tr("Video"));
    cue->setField(QStringLiteral("number"), static_cast<double>(insertRow + 1));
    m_workspace->undoStack()->push(
        new core::InsertCueCommand(list, insertRow, std::move(cue)));
    if (m_model->rowCount() > insertRow)
        m_cueListView->setCurrentIndex(m_model->index(insertRow, 0));
}

void MainWindow::insertImageCue()
{
    auto *list = m_workspace->activeCueList();
    if (!list) return;
    const auto idx = m_cueListView->currentIndex();
    const int insertRow = idx.isValid() ? idx.row() + 1 : list->cueCount();
    auto cue = std::make_unique<video::ImageCue>();
    cue->setField(QStringLiteral("name"), tr("Image"));
    cue->setField(QStringLiteral("number"), static_cast<double>(insertRow + 1));
    m_workspace->undoStack()->push(
        new core::InsertCueCommand(list, insertRow, std::move(cue)));
    if (m_model->rowCount() > insertRow)
        m_cueListView->setCurrentIndex(m_model->index(insertRow, 0));
}

void MainWindow::insertTextCue()
{
    auto *list = m_workspace->activeCueList();
    if (!list) return;
    const auto idx = m_cueListView->currentIndex();
    const int insertRow = idx.isValid() ? idx.row() + 1 : list->cueCount();
    auto cue = std::make_unique<video::TextCue>();
    cue->setField(QStringLiteral("name"), tr("Text"));
    cue->setField(QStringLiteral("number"), static_cast<double>(insertRow + 1));
    cue->setField(QStringLiteral("text"), tr("Title"));
    m_workspace->undoStack()->push(
        new core::InsertCueCommand(list, insertRow, std::move(cue)));
    if (m_model->rowCount() > insertRow)
        m_cueListView->setCurrentIndex(m_model->index(insertRow, 0));
}

void MainWindow::insertWaitCue()
{
    auto *list = m_workspace->activeCueList();
    if (!list) return;
    const auto idx = m_cueListView->currentIndex();
    const int insertRow = idx.isValid() ? idx.row() + 1 : list->cueCount();
    auto cue = std::make_unique<cues::WaitCue>();
    cue->setField(QStringLiteral("name"), tr("Wait"));
    cue->setField(QStringLiteral("number"), static_cast<double>(insertRow + 1));
    m_workspace->undoStack()->push(new core::InsertCueCommand(list, insertRow, std::move(cue)));
    if (m_model->rowCount() > insertRow)
        m_cueListView->setCurrentIndex(m_model->index(insertRow, 0));
}

void MainWindow::insertStartCue()
{
    auto *list = m_workspace->activeCueList();
    if (!list) return;
    const auto idx = m_cueListView->currentIndex();
    const int insertRow = idx.isValid() ? idx.row() + 1 : list->cueCount();
    auto cue = std::make_unique<cues::StartCue>();
    cue->setField(QStringLiteral("name"), tr("Start"));
    cue->setField(QStringLiteral("number"), static_cast<double>(insertRow + 1));
    m_workspace->undoStack()->push(new core::InsertCueCommand(list, insertRow, std::move(cue)));
    if (m_model->rowCount() > insertRow)
        m_cueListView->setCurrentIndex(m_model->index(insertRow, 0));
}

void MainWindow::insertStopCue()
{
    auto *list = m_workspace->activeCueList();
    if (!list) return;
    const auto idx = m_cueListView->currentIndex();
    const int insertRow = idx.isValid() ? idx.row() + 1 : list->cueCount();
    auto cue = std::make_unique<cues::StopCue>();
    cue->setField(QStringLiteral("name"), tr("Stop"));
    cue->setField(QStringLiteral("number"), static_cast<double>(insertRow + 1));
    m_workspace->undoStack()->push(new core::InsertCueCommand(list, insertRow, std::move(cue)));
    if (m_model->rowCount() > insertRow)
        m_cueListView->setCurrentIndex(m_model->index(insertRow, 0));
}

void MainWindow::insertGotoCue()
{
    auto *list = m_workspace->activeCueList();
    if (!list) return;
    const auto idx = m_cueListView->currentIndex();
    const int insertRow = idx.isValid() ? idx.row() + 1 : list->cueCount();
    auto cue = std::make_unique<cues::GotoCue>();
    cue->setField(QStringLiteral("name"), tr("Goto"));
    cue->setField(QStringLiteral("number"), static_cast<double>(insertRow + 1));
    m_workspace->undoStack()->push(new core::InsertCueCommand(list, insertRow, std::move(cue)));
    if (m_model->rowCount() > insertRow)
        m_cueListView->setCurrentIndex(m_model->index(insertRow, 0));
}

// Pause/Load/Reset/Devamp share the StartCue shape — the GoEngine
// switches behaviour by cue subclass at fire time.
namespace {
template <typename CueT>
std::unique_ptr<cues::Cue> makeNamedTargeting(const QString &name, int row) {
    auto cue = std::make_unique<CueT>();
    cue->setField(QStringLiteral("name"), name);
    cue->setField(QStringLiteral("number"), static_cast<double>(row + 1));
    return cue;
}
} // namespace

void MainWindow::insertPauseCue()
{
    auto *list = m_workspace->activeCueList(); if (!list) return;
    const auto idx = m_cueListView->currentIndex();
    const int row = idx.isValid() ? idx.row() + 1 : list->cueCount();
    m_workspace->undoStack()->push(new core::InsertCueCommand(
        list, row, makeNamedTargeting<cues::PauseCue>(tr("Pause"), row)));
    if (m_model->rowCount() > row) m_cueListView->setCurrentIndex(m_model->index(row, 0));
}

void MainWindow::insertLoadCue()
{
    auto *list = m_workspace->activeCueList(); if (!list) return;
    const auto idx = m_cueListView->currentIndex();
    const int row = idx.isValid() ? idx.row() + 1 : list->cueCount();
    m_workspace->undoStack()->push(new core::InsertCueCommand(
        list, row, makeNamedTargeting<cues::LoadCue>(tr("Load"), row)));
    if (m_model->rowCount() > row) m_cueListView->setCurrentIndex(m_model->index(row, 0));
}

void MainWindow::insertResetCue()
{
    auto *list = m_workspace->activeCueList(); if (!list) return;
    const auto idx = m_cueListView->currentIndex();
    const int row = idx.isValid() ? idx.row() + 1 : list->cueCount();
    m_workspace->undoStack()->push(new core::InsertCueCommand(
        list, row, makeNamedTargeting<cues::ResetCue>(tr("Reset"), row)));
    if (m_model->rowCount() > row) m_cueListView->setCurrentIndex(m_model->index(row, 0));
}

void MainWindow::insertDevampCue()
{
    auto *list = m_workspace->activeCueList(); if (!list) return;
    const auto idx = m_cueListView->currentIndex();
    const int row = idx.isValid() ? idx.row() + 1 : list->cueCount();
    m_workspace->undoStack()->push(new core::InsertCueCommand(
        list, row, makeNamedTargeting<cues::DevampCue>(tr("Devamp"), row)));
    if (m_model->rowCount() > row) m_cueListView->setCurrentIndex(m_model->index(row, 0));
}

void MainWindow::insertGroupCue()
{
    auto *list = m_workspace->activeCueList();
    if (!list) return;
    const auto idx = m_cueListView->currentIndex();
    const int insertRow = idx.isValid() ? idx.row() + 1 : list->cueCount();
    auto cue = std::make_unique<cues::GroupCue>();
    cue->setField(QStringLiteral("name"), tr("Group"));
    cue->setField(QStringLiteral("number"), static_cast<double>(insertRow + 1));
    m_workspace->undoStack()->push(new core::InsertCueCommand(list, insertRow, std::move(cue)));
    if (m_model->rowCount() > insertRow)
        m_cueListView->setCurrentIndex(m_model->index(insertRow, 0));
}

void MainWindow::insertMidiCue()
{
    auto *list = m_workspace->activeCueList();
    if (!list) return;
    const auto idx = m_cueListView->currentIndex();
    const int insertRow = idx.isValid() ? idx.row() + 1 : list->cueCount();
    auto cue = std::make_unique<midi::MidiCue>();
    cue->setField(QStringLiteral("name"), tr("MIDI"));
    cue->setField(QStringLiteral("number"), static_cast<double>(insertRow + 1));
    m_workspace->undoStack()->push(new core::InsertCueCommand(list, insertRow, std::move(cue)));
    if (m_model->rowCount() > insertRow)
        m_cueListView->setCurrentIndex(m_model->index(insertRow, 0));
}

void MainWindow::insertMscCue()
{
    auto *list = m_workspace->activeCueList();
    if (!list) return;
    const auto idx = m_cueListView->currentIndex();
    const int insertRow = idx.isValid() ? idx.row() + 1 : list->cueCount();
    auto cue = std::make_unique<midi::MscCue>();
    cue->setField(QStringLiteral("name"), tr("MSC"));
    cue->setField(QStringLiteral("number"), static_cast<double>(insertRow + 1));
    m_workspace->undoStack()->push(new core::InsertCueCommand(list, insertRow, std::move(cue)));
    if (m_model->rowCount() > insertRow)
        m_cueListView->setCurrentIndex(m_model->index(insertRow, 0));
}

void MainWindow::deleteSelectedCue()
{
    auto *list = m_workspace->activeCueList();
    if (!list) return;

    // Collect every selected row (the view is in ExtendedSelection mode).
    // Falls back to the current row if nothing is explicitly selected so
    // single-row Delete keeps working.
    QSet<int> rowSet;
    if (auto *sel = m_cueListView->selectionModel()) {
        for (const auto &idx : sel->selectedRows()) {
            if (idx.isValid()) rowSet.insert(idx.row());
        }
    }
    if (rowSet.isEmpty()) {
        const auto idx = m_cueListView->currentIndex();
        if (!idx.isValid()) return;
        rowSet.insert(idx.row());
    }

    // Sort descending so each removal doesn't shift the rows still to come.
    QList<int> rows(rowSet.begin(), rowSet.end());
    std::sort(rows.begin(), rows.end(), std::greater<int>());

    auto *stack = m_workspace->undoStack();
    if (rows.size() > 1) {
        stack->beginMacro(tr("Delete %1 Cues").arg(rows.size()));
        for (int row : rows) stack->push(new core::RemoveCueCommand(list, row));
        stack->endMacro();
        statusBar()->showMessage(tr("Deleted %1 cues").arg(rows.size()), 2500);
    } else {
        stack->push(new core::RemoveCueCommand(list, rows.first()));
    }
}

// ---------- Auto-save journal -------------------------------------------
//
// Strategy: every undo-stack change schedules a debounced write (1.5 s).
// The journal is a full ShowFile snapshot in
//   <AppDataLocation>/journals/<sessionId>.journal
// On clean save, close, or load, we delete the journal. On startup we
// scan that folder; any leftover journal means the previous session
// died unexpectedly, and we offer to recover.

void MainWindow::scheduleJournal()
{
    if (!m_journalTimer) {
        m_journalTimer = new QTimer(this);
        m_journalTimer->setSingleShot(true);
        m_journalTimer->setInterval(1500);
        connect(m_journalTimer, &QTimer::timeout, this, &MainWindow::writeJournal);
    }
    if (m_journalPath.isEmpty()) {
        const auto dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                            + QStringLiteral("/journals");
        QDir().mkpath(dir);
        m_journalPath = dir + QStringLiteral("/")
                      + QUuid::createUuid().toString(QUuid::WithoutBraces)
                      + QStringLiteral(".journal");
    }
    m_journalTimer->start();
}

void MainWindow::writeJournal()
{
    if (!m_workspace || m_journalPath.isEmpty()) return;
    show::ShowFile::save(m_journalPath, *m_workspace);
}

void MainWindow::clearJournal()
{
    if (m_journalTimer) m_journalTimer->stop();
    if (!m_journalPath.isEmpty()) {
        QFile::remove(m_journalPath);
        m_journalPath.clear();
    }
}

void MainWindow::recoverFromJournalIfPresent()
{
    const auto dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                        + QStringLiteral("/journals");
    QDir d(dir);
    const auto journals = d.entryList({QStringLiteral("*.journal")}, QDir::Files);
    if (journals.isEmpty()) return;

    // Newest first.
    QStringList paths;
    for (const auto &j : journals) paths << d.filePath(j);
    std::sort(paths.begin(), paths.end(), [](const QString &a, const QString &b) {
        return QFileInfo(a).lastModified() > QFileInfo(b).lastModified();
    });

    const auto answer = QMessageBox::question(this,
        tr("Recover unsaved work?"),
        tr("quewi found %1 unsaved show%2 from a previous session. Recover the most "
           "recent? You can save it under a new name once it's open.")
            .arg(paths.size()).arg(paths.size() == 1 ? QString() : QStringLiteral("s")),
        QMessageBox::Yes | QMessageBox::No);

    if (answer == QMessageBox::Yes) {
        if (loadShowFromPath(paths.first())) {
            m_currentPath.clear(); // force Save As; this isn't a real .quewi yet
            updateTitle();
            statusBar()->showMessage(tr("Recovered from journal"), 4000);
        }
    }

    // Whether they recovered or not, clean every leftover journal so we
    // don't ask again. (User declined → they're fine losing it.)
    for (const auto &p : paths) QFile::remove(p);
}

void MainWindow::showPreflight()
{
    ui::PreflightDialog dlg(m_workspace.get(), this);
    dlg.exec();
}

void MainWindow::showCommandPalette()
{
    ui::CommandPalette dlg(menuBar(), this);
    dlg.exec();
}

void MainWindow::showShortcutsDialog()
{
    ui::ShortcutsDialog dlg(m_shortcuts, this);
    dlg.exec();
}

void MainWindow::showAbout()
{
    ui::AboutDialog dlg(this);
    dlg.exec();
}

void MainWindow::showNotifications()
{
    ui::NotificationsDialog dlg(this);
    dlg.exec();
    m_unreadNotifs = 0;
    refreshNotifBadge();
}

void MainWindow::refreshNotifBadge()
{
    if (!m_notifBadge) return;
    if (m_unreadNotifs <= 0) {
        m_notifBadge->setVisible(false);
        return;
    }
    m_notifBadge->setVisible(true);
    m_notifBadge->setText(tr("⚠ %1").arg(m_unreadNotifs));
}

// ── Recent files ───────────────────────────────────────────────────────
// MRU list is persisted in QSettings under ui/recentFiles. We cap it at
// 8 visible entries and prune missing files on every rebuild — operators
// tend to rename / move show files and stale entries are noise.
namespace { constexpr int kMaxRecentFiles = 8; }

void MainWindow::noteRecentFile(const QString &path)
{
    if (path.isEmpty()) return;
    QSettings s(QStringLiteral("ServeGaming"), QStringLiteral("quewi"));
    auto list = s.value(QStringLiteral("ui/recentFiles")).toStringList();
    const auto canonical = QFileInfo(path).absoluteFilePath();
    list.removeAll(canonical);
    list.prepend(canonical);
    while (list.size() > kMaxRecentFiles) list.removeLast();
    s.setValue(QStringLiteral("ui/recentFiles"), list);
    rebuildRecentMenu();
}

void MainWindow::rebuildRecentMenu()
{
    if (!m_recentMenu) return;
    m_recentMenu->clear();
    QSettings s(QStringLiteral("ServeGaming"), QStringLiteral("quewi"));
    auto list = s.value(QStringLiteral("ui/recentFiles")).toStringList();

    // Drop entries whose files no longer exist so the menu stays honest.
    QStringList live;
    for (const auto &p : list)
        if (QFileInfo::exists(p)) live << p;
    if (live != list) s.setValue(QStringLiteral("ui/recentFiles"), live);

    if (live.isEmpty()) {
        auto *empty = m_recentMenu->addAction(tr("(none)"));
        empty->setEnabled(false);
        return;
    }
    for (const auto &p : live) {
        const auto label = QFileInfo(p).fileName();
        auto *act = m_recentMenu->addAction(label);
        act->setToolTip(p);
        connect(act, &QAction::triggered, this, [this, p]{ openRecent(p); });
    }
    m_recentMenu->addSeparator();
    auto *clear = m_recentMenu->addAction(tr("Clear list"));
    connect(clear, &QAction::triggered, this, [this]{
        QSettings s(QStringLiteral("ServeGaming"), QStringLiteral("quewi"));
        s.remove(QStringLiteral("ui/recentFiles"));
        rebuildRecentMenu();
    });
}

void MainWindow::openRecent(const QString &path)
{
    if (!maybeSaveChanges()) return;
    if (!QFileInfo::exists(path)) {
        statusBar()->showMessage(tr("File no longer exists: %1").arg(path), 4000);
        rebuildRecentMenu();
        return;
    }
    loadShowFromPath(path);
}

void MainWindow::toggleShowMode()
{
    const bool wantOn = m_actShowMode ? m_actShowMode->isChecked() : !m_showMode;

    // Password gate. The operator can set a 4+ digit code in QSettings
    // (or via the prompt the first time they enter Show Mode) so an
    // accidental Ctrl+Shift+L mid-show doesn't drop them back into
    // Edit. Empty stored hash = no password, behaviour matches v0.6.
    QSettings s(QStringLiteral("ServeGaming"), QStringLiteral("quewi"));
    const QByteArray storedHash =
        s.value(QStringLiteral("showMode/passwordHash")).toByteArray();

    auto hashOf = [](const QString &pw) -> QByteArray {
        return QCryptographicHash::hash(pw.toUtf8(),
            QCryptographicHash::Sha256).toHex();
    };

    if (wantOn && !m_showMode) {
        // Entering: if no password set, offer to set one (skippable).
        if (storedHash.isEmpty()) {
            const auto answer = QMessageBox::question(this, tr("Set Show Mode password?"),
                tr("Set a password to require confirmation when leaving Show Mode "
                   "during a show. Skip if you don't want a lock."),
                QMessageBox::Yes | QMessageBox::No);
            if (answer == QMessageBox::Yes) {
                bool ok = false;
                const auto pw = QInputDialog::getText(this, tr("Set Show Mode password"),
                    tr("Enter a password (4+ characters):"),
                    QLineEdit::Password, QString(), &ok).trimmed();
                if (ok && pw.size() >= 4) {
                    s.setValue(QStringLiteral("showMode/passwordHash"), hashOf(pw));
                } else if (ok) {
                    QMessageBox::warning(this, tr("Show Mode password"),
                        tr("Password must be at least 4 characters. No password set."));
                }
            }
        }
        m_showMode = true;
    } else if (!wantOn && m_showMode) {
        // Exiting: require password if one is set.
        if (!storedHash.isEmpty()) {
            bool ok = false;
            const auto pw = QInputDialog::getText(this, tr("Exit Show Mode"),
                tr("Enter Show Mode password:"),
                QLineEdit::Password, QString(), &ok);
            if (!ok) {
                if (m_actShowMode) m_actShowMode->setChecked(true);   // revert
                return;
            }
            if (hashOf(pw) != storedHash) {
                QMessageBox::warning(this, tr("Show Mode"),
                    tr("Incorrect password. Show Mode stays locked."));
                if (m_actShowMode) m_actShowMode->setChecked(true);   // revert
                return;
            }
        }
        m_showMode = false;
    } else {
        m_showMode = wantOn;
    }
    applyShowMode();
}

void MainWindow::applyShowMode()
{
    // Disable everything destructive while in Show Mode. The transport
    // bar (GO / Pause / Fade All / Panic) and the cue list selection
    // remain live so the operator can still drive the show.
    const bool editable = !m_showMode;
    if (m_inspector) m_inspector->setEnabled(editable);
    if (m_listTabs)  m_listTabs->setEnabled(editable);

    // Disable Edit, Cue, List menus.
    for (QAction *act : menuBar()->actions()) {
        const auto t = act->text().remove(QChar('&'));
        if (t == tr("Edit") || t == tr("Cue") || t == tr("List") || t == tr("File")) {
            act->setEnabled(editable);
        }
    }
    if (m_actShowMode) m_actShowMode->setEnabled(true); // always allow toggle
    if (m_showModeStrip) m_showModeStrip->setVisible(m_showMode);
    updateTitle();
    statusBar()->showMessage(m_showMode ? tr("SHOW MODE — editing locked")
                                         : tr("Edit mode"), 2500);
}

void MainWindow::rebuildListTabs()
{
    if (!m_listTabs) return;
    QSignalBlocker blocker(m_listTabs);
    while (m_listTabs->count() > 0) m_listTabs->removeTab(0);
    if (!m_workspace) return;
    int currentIdx = 0;
    int idx = 0;
    for (const auto &list : m_workspace->cueLists()) {
        m_listTabs->addTab(list->name());
        m_listTabs->setTabData(idx, QVariant::fromValue(list->id()));
        if (list.get() == m_workspace->activeCueList()) currentIdx = idx;
        ++idx;
    }
    if (m_listTabs->count() > 0) m_listTabs->setCurrentIndex(currentIdx);
}

void MainWindow::onTabSelected(int index)
{
    if (!m_workspace || index < 0) return;
    const auto id = m_listTabs->tabData(index).toUuid();
    for (const auto &list : m_workspace->cueLists()) {
        if (list->id() == id) {
            m_workspace->setActiveCueList(list.get());
            m_model->setCueList(list.get());
            if (m_model->rowCount() > 0)
                m_cueListView->setCurrentIndex(m_model->index(0, 0));
            return;
        }
    }
}

void MainWindow::addCueListTab()
{
    if (!m_workspace) return;
    bool ok = false;
    const auto name = QInputDialog::getText(this, tr("New cue list"),
        tr("Name:"), QLineEdit::Normal,
        tr("List %1").arg(int(m_workspace->cueLists().size()) + 1), &ok);
    if (!ok || name.isEmpty()) return;
    auto list = std::make_unique<core::CueList>(name);
    auto *raw = m_workspace->addCueList(std::move(list));
    m_workspace->setActiveCueList(raw);
    rebuildListTabs();
    m_model->setCueList(raw);
}

void MainWindow::renameCueListTab()
{
    if (!m_workspace || !m_workspace->activeCueList()) return;
    auto *list = m_workspace->activeCueList();
    bool ok = false;
    const auto name = QInputDialog::getText(this, tr("Rename cue list"),
        tr("Name:"), QLineEdit::Normal, list->name(), &ok);
    if (!ok || name.isEmpty()) return;
    m_workspace->undoStack()->push(new core::RenameCueListCommand(list, name));
    rebuildListTabs();
}

void MainWindow::removeCueListTab()
{
    if (!m_workspace || m_workspace->cueLists().size() <= 1) {
        statusBar()->showMessage(tr("Can't remove the only cue list"), 2500);
        return;
    }
    auto *list = m_workspace->activeCueList();
    if (!list) return;
    if (QMessageBox::question(this, tr("Remove cue list"),
            tr("Remove \"%1\" and all its cues?").arg(list->name()))
        != QMessageBox::Yes) return;
    m_workspace->takeCueList(list->id());
    auto *active = m_workspace->activeCueList();
    rebuildListTabs();
    if (active) m_model->setCueList(active);
}

void MainWindow::onSelectionChanged()
{
    auto *cue = m_cueListView->nextCue();
    m_transport->setNextCue(cue);
}

void MainWindow::updateTitle()
{
    const QString fileName = m_currentPath.isEmpty()
        ? tr("Untitled")
        : QFileInfo(m_currentPath).fileName();
    const QString dirty = m_workspace && m_workspace->isDirty() ? QStringLiteral("*") : QString();
    setWindowTitle(QStringLiteral("%1%2 — quewi v%3")
        .arg(fileName, dirty, QStringLiteral(QUEWI_VERSION)));
}

void MainWindow::runInAppInstall(const QString &msiUrl)
{
    auto *installer = new UpdateInstaller(this);
    auto *prog = new QProgressDialog(tr("Downloading update…"),
                                     tr("Cancel"), 0, 100, this);
    prog->setWindowTitle(tr("Installing update"));
    prog->setWindowModality(Qt::ApplicationModal);
    prog->setMinimumDuration(0);
    prog->setAutoClose(false);
    prog->setAutoReset(false);

    connect(installer, &UpdateInstaller::progress, prog,
        [prog](qint64 received, qint64 total) {
            if (total > 0) {
                prog->setMaximum(int(total / 1024));
                prog->setValue(int(received / 1024));
                prog->setLabelText(tr("Downloading update… %1 / %2 KB")
                    .arg(received / 1024).arg(total / 1024));
            } else {
                prog->setLabelText(tr("Downloading update… %1 KB")
                    .arg(received / 1024));
            }
        });
    connect(installer, &UpdateInstaller::downloadFinished, this,
        [this, prog, installer](const QString &localPath) {
            prog->close();
            installer->deleteLater();
            const auto answer = QMessageBox::question(this, tr("Install update"),
                tr("Download complete. Quewi will close and the Windows "
                   "installer will start. Continue?"),
                QMessageBox::Yes | QMessageBox::No);
            if (answer == QMessageBox::Yes) {
                UpdateInstaller::launchAndQuit(localPath);
            }
        });
    connect(installer, &UpdateInstaller::downloadFailed, this,
        [this, prog, installer](const QString &reason) {
            prog->close();
            installer->deleteLater();
            QMessageBox::warning(this, tr("Update download failed"),
                tr("Couldn't download the installer:\n%1").arg(reason));
        });
    connect(prog, &QProgressDialog::canceled, installer,
        [installer]{ installer->deleteLater(); });

    installer->download(msiUrl);
}

void MainWindow::onCartFileDropped(int row, int col, const QString &path)
{
    if (!m_workspace) return;
    auto *list = m_workspace->activeCueList();
    if (!list) return;

    // Re-use the existing drag-import path that knows how to make a
    // cue from any supported file type, then bind whichever cue id
    // came back to the dropped cart cell.
    const auto added = insertCuesFromUrls({ QUrl::fromLocalFile(path) }, -1);
    if (added <= 0) return;
    auto *newCue = list->cueAt(list->cueCount() - 1);
    if (!newCue) return;
    if (auto *cart = m_workspace->cart()) {
        cart->setCell(row, col, newCue->id());
    }
}

void MainWindow::onMidiTrigger(quint8 status, const QByteArray &bytes)
{
    if (bytes.size() < 2) return;
    // Note Off (0x80) and zero-velocity Note On (0x90 with byte 2 == 0)
    // both mean "key released" — ignore so press-then-hold doesn't
    // double-fire when the operator lifts their finger.
    const quint8 high = status & 0xF0;
    if (high == 0x80) return;
    if (high == 0x90 && bytes.size() >= 3 && quint8(bytes[2]) == 0) return;

    // Match on (status nibble, channel, second byte). Channel 0 is
    // encoded as 0 in the lower nibble; we keep it in the binding key
    // so different controllers on different channels don't clash.
    const QString key = QStringLiteral("%1:%2")
        .arg(int(status), 2, 16, QChar('0'))
        .arg(int(quint8(bytes[1])));

    QSettings s(QStringLiteral("ServeGaming"), QStringLiteral("quewi"));
    auto fire = [&](const QString &binding, QAction *act) {
        if (!act) return;
        const auto stored = s.value(QStringLiteral("midi/binding/%1").arg(binding))
                              .toString();
        if (stored == key) act->trigger();
    };
    fire(QStringLiteral("go"),      m_actGo);
    fire(QStringLiteral("pause"),   m_actPause);
    fire(QStringLiteral("fadeAll"), m_actFadeAll);
    fire(QStringLiteral("panic"),   m_actPanic);
}

void MainWindow::checkForUpdates(bool manual)
{
    if (!m_updateChecker) {
        m_updateChecker = new UpdateChecker(this);
        connect(m_updateChecker, &UpdateChecker::updateAvailable, this,
            [this](const QString &version, const QString &download,
                   const QString &page, UpdateChecker::Mode /*mode*/)
        {
            QMessageBox box(this);
            box.setWindowTitle(tr("Update available"));
            box.setIcon(QMessageBox::Information);
            box.setText(tr("quewi <b>v%1</b> is available.<br>"
                           "You're running v%2.")
                .arg(version, QStringLiteral(QUEWI_VERSION)));
            box.setInformativeText(tr("Install downloads the new version and "
                                       "launches the Windows installer; quewi "
                                       "will close so the new files can be "
                                       "written. \"Open in browser\" is "
                                       "available if you'd rather download "
                                       "manually."));
            auto *install = box.addButton(tr("Install update"), QMessageBox::AcceptRole);
            auto *manual  = box.addButton(tr("Open in browser"), QMessageBox::ActionRole);
            auto *notes   = box.addButton(tr("Release notes"),  QMessageBox::HelpRole);
            box.addButton(tr("Later"), QMessageBox::RejectRole);
            box.exec();
            if (box.clickedButton() == install) {
                runInAppInstall(download);
            } else if (box.clickedButton() == manual) {
                QDesktopServices::openUrl(QUrl(download));
            } else if (box.clickedButton() == notes) {
                QDesktopServices::openUrl(QUrl(page));
            }
        });
        connect(m_updateChecker, &UpdateChecker::upToDate, this,
            [this](UpdateChecker::Mode mode) {
                if (mode == UpdateChecker::Mode::Verbose) {
                    QMessageBox::information(this, tr("Up to date"),
                        tr("quewi v%1 is the latest release.")
                            .arg(QStringLiteral(QUEWI_VERSION)));
                }
            });
        connect(m_updateChecker, &UpdateChecker::checkFailed, this,
            [this](const QString &reason, UpdateChecker::Mode mode) {
                if (mode == UpdateChecker::Mode::Verbose) {
                    QMessageBox::warning(this, tr("Update check failed"),
                        tr("Couldn't reach GitHub:\n%1").arg(reason));
                }
                // Silent failures stay quiet — the operator doesn't need
                // a popup if their FOH laptop is offline at boot.
            });
    }
    m_updateChecker->start(manual ? UpdateChecker::Mode::Verbose
                                   : UpdateChecker::Mode::Silent);
}

void MainWindow::onGoRequested()
{
    auto *cue = m_cueListView->nextCue();
    if (!cue) {
        statusBar()->showMessage(tr("No cue to fire"), 2000);
        return;
    }

    if (m_goEngine) m_goEngine->fire(cue);

    const auto idx = m_cueListView->currentIndex();
    const int curRow = idx.isValid() ? idx.row() : -1;
    const int nextRow = curRow + 1;
    if (nextRow < m_model->rowCount()) {
        m_cueListView->setCurrentIndex(m_model->index(nextRow, 0));
    }
    if (auto *upcoming = m_cueListView->nextCue()) {
        if (auto *ac = qobject_cast<audio::AudioCue *>(upcoming)) ac->prepare();
    }
}

#if 0
// Removed: legacy dispatch superseded by GoEngine. Kept disabled below
// only to keep the diff small for review; safe to delete.
static void legacy_dispatch_keep_diff_small() {
    quewi::cues::Cue *cue = nullptr;
    if (auto *oscCue = qobject_cast<osc::OscCue *>(cue)) {
        const auto dv = oscCue->destination();
        osc::Destination dest{
            dv.id, dv.name, dv.host, dv.port,
            static_cast<osc::Destination::Transport>(dv.transport)
        };
        if (m_oscEngine->send(dest, oscCue->buildMessage())) {
            statusBar()->showMessage(tr("GO: %1 → %2:%3 %4")
                .arg(QString::number(cue->number(), 'f', 2),
                     dest.host, QString::number(dest.port),
                     oscCue->field(QStringLiteral("address")).toString()),
                2000);
        }
    } else if (auto *audioCue = qobject_cast<audio::AudioCue *>(cue)) {
        audioCue->prepare(); // idempotent; loads file if not already
        auto file = audioCue->audioFile();
        if (!file) {
            statusBar()->showMessage(tr("GO: no file selected"), 3000);
        } else if (file->state() == audio::AudioFile::State::Failed) {
            statusBar()->showMessage(tr("GO: decode failed — %1").arg(file->errorString()), 6000);
        } else if (file->state() != audio::AudioFile::State::Loaded) {
            statusBar()->showMessage(tr("GO: audio still decoding — try again in a moment"), 3000);
        } else {
            audio::VoiceParams p;
            p.gainDb = audioCue->gainDb();
            p.fadeInSeconds  = audioCue->fadeInSeconds();
            p.fadeOutSeconds = audioCue->fadeOutSeconds();
            p.trimInSeconds  = audioCue->trimInSeconds();
            p.trimOutSeconds = audioCue->trimOutSeconds();
            p.pan            = audioCue->pan();
            p.loop = audioCue->loop();
            p.outputDeviceId = audioCue->outputDeviceId();
            const auto vid = m_audioEngine->fire(file, p);
            audioCue->setCurrentVoiceId(vid);
            if (vid == 0) {
                statusBar()->showMessage(tr("GO: audio engine failed — %1")
                    .arg(m_audioEngine->lastError()), 5000);
            } else {
                statusBar()->showMessage(tr("GO: ▶ %1 (%2 s)")
                    .arg(cue->name().isEmpty() ? cue->typeName() : cue->name(),
                         QString::number(file->durationSeconds(), 'f', 2)),
                    2000);
            }
        }
    } else if (auto *lightCue = qobject_cast<lighting::LightCue *>(cue)) {
        QHash<int, int> values;
        const auto &chs = lightCue->channels();
        for (auto it = chs.constBegin(); it != chs.constEnd(); ++it) {
            values.insert(it.key(), it.value());
        }
        m_lightingEngine->applyChannels(lightCue->universe(), values);
        statusBar()->showMessage(tr("GO: ⚡ Light U%1 (%2 channels)")
            .arg(lightCue->universe()).arg(values.size()), 2000);
    } else if (auto *lfadeCue = qobject_cast<lighting::LightFadeCue *>(cue)) {
        // Resolve target Light cue.
        auto *list = m_workspace->activeCueList();
        lighting::LightCue *target = nullptr;
        if (list) {
            for (int row = 0; row < list->cueCount(); ++row) {
                auto *c = list->cueAt(row);
                if (c && c->id() == lfadeCue->targetId()) {
                    target = qobject_cast<lighting::LightCue *>(c);
                    break;
                }
            }
        }
        if (!target) {
            statusBar()->showMessage(tr("Light Fade: target not found"), 2000);
        } else {
            QHash<int, int> values;
            const auto &chs = target->channels();
            for (auto it = chs.constBegin(); it != chs.constEnd(); ++it) {
                values.insert(it.key(), it.value());
            }
            m_lightingEngine->fadeChannels(target->universe(), values,
                                           lfadeCue->durationSeconds());
            statusBar()->showMessage(tr("Light Fade: U%1 → %2 channels over %3 s")
                .arg(target->universe()).arg(values.size())
                .arg(lfadeCue->durationSeconds()), 2000);
        }
    } else if (auto *visualCue = qobject_cast<video::VisualCue *>(cue)) {
        video::VideoVoiceParams p;
        p.screenIndex = visualCue->screenIndex();
        p.geometry = QRectF(visualCue->posX(), visualCue->posY(),
                            visualCue->posW(), visualCue->posH());
        p.opacity = visualCue->opacity();
        if (auto *vc = qobject_cast<video::VideoCue *>(cue)) {
            p.kind = video::VideoVoiceParams::Video;
            p.filePath = vc->filePath();
            p.loop = vc->loop();
        } else if (auto *ic = qobject_cast<video::ImageCue *>(cue)) {
            p.kind = video::VideoVoiceParams::Image;
            p.filePath = ic->filePath();
        } else if (auto *tc = qobject_cast<video::TextCue *>(cue)) {
            p.kind = video::VideoVoiceParams::Text;
            p.text = tc->text();
            p.fontPixelSize = tc->fontPixelSize();
            p.textColor = tc->textColor();
        }
        m_videoEngine->fire(p);
        statusBar()->showMessage(tr("GO: ▶ %1 on screen %2")
            .arg(cue->name().isEmpty() ? cue->typeName() : cue->name())
            .arg(visualCue->screenIndex()), 2000);
    } else if (auto *fadeCue = qobject_cast<cues::FadeCue *>(cue)) {
        // Resolve target: must be an AudioCue in the active list.
        auto *list = m_workspace->activeCueList();
        audio::AudioCue *target = nullptr;
        if (list) {
            for (int row = 0; row < list->cueCount(); ++row) {
                auto *c = list->cueAt(row);
                if (c && c->id() == fadeCue->targetId()) {
                    target = qobject_cast<audio::AudioCue *>(c);
                    break;
                }
            }
        }
        if (!target) {
            statusBar()->showMessage(tr("Fade: target not found"), 2000);
        } else if (target->currentVoiceId() == 0) {
            statusBar()->showMessage(tr("Fade: target not playing"), 2000);
        } else if (fadeCue->parameter() == QLatin1String("gainDb")) {
            m_audioEngine->fadeGain(target->currentVoiceId(),
                                    fadeCue->targetValue(),
                                    fadeCue->durationSeconds());
            statusBar()->showMessage(tr("Fade: → %1 dB over %2 s")
                .arg(fadeCue->targetValue())
                .arg(fadeCue->durationSeconds()), 2000);
        }
    } else {
        statusBar()->showMessage(tr("GO: %1 %2")
            .arg(QString::number(cue->number(), 'f', 2), cue->name()), 2000);
    }

    // Advance selection one row forward (or wrap to end if already at the
    // last cue). Pre-load the now-selected audio cue so its file is
    // decoded by the time GO fires next.
    const auto idx = m_cueListView->currentIndex();
    const int curRow = idx.isValid() ? idx.row() : -1;
    const int nextRow = curRow + 1;
    if (nextRow < m_model->rowCount()) {
        m_cueListView->setCurrentIndex(m_model->index(nextRow, 0));
    }
    if (auto *upcoming = m_cueListView->nextCue()) {
        if (auto *ac = qobject_cast<audio::AudioCue *>(upcoming)) ac->prepare();
    }
}
#endif // legacy dispatch

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (maybeSaveChanges()) {
        clearJournal();
        event->accept();
    } else {
        event->ignore();
    }
}

// ---------- Drag and drop ------------------------------------------------

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls()) event->acceptProposedAction();
}

void MainWindow::dragMoveEvent(QDragMoveEvent *event)
{
    if (event->mimeData()->hasUrls()) event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent *event)
{
    const auto urls = event->mimeData()->urls();
    if (urls.isEmpty()) return;

    // Single .quewi file → open as show (with the usual save-changes prompt).
    if (urls.size() == 1) {
        const auto path = urls.first().toLocalFile();
        if (path.endsWith(QStringLiteral(".quewi"), Qt::CaseInsensitive)) {
            event->acceptProposedAction();
            if (!maybeSaveChanges()) return;
            loadShowFromPath(path);
            return;
        }
    }

    // Otherwise build cues from each URL.
    const int created = insertCuesFromUrls(urls);
    if (created > 0) {
        statusBar()->showMessage(tr("Added %1 cue%2 from drop")
            .arg(created).arg(created == 1 ? QString() : QStringLiteral("s")), 2500);
    } else {
        statusBar()->showMessage(tr("No supported file types in drop"), 2500);
    }
    event->acceptProposedAction();
}

int MainWindow::insertCuesFromUrls(const QList<QUrl> &urls, int startRow)
{
    auto *list = m_workspace ? m_workspace->activeCueList() : nullptr;
    if (!list) return 0;

    int insertRow;
    if (startRow >= 0) {
        insertRow = std::min(startRow, list->cueCount());
    } else {
        const auto sel = m_cueListView->currentIndex();
        insertRow = sel.isValid() ? sel.row() + 1 : list->cueCount();
    }
    int created = 0;
    int firstNewRow = -1;

    for (const auto &url : urls) {
        const auto path = url.toLocalFile();
        if (path.isEmpty()) continue;

        auto cue = cueFromFile(path);
        if (!cue) continue;

        cue->setField(QStringLiteral("number"), static_cast<double>(insertRow + 1));
        // Capture as raw pointer — InsertCueCommand takes ownership but we
        // want to call prepare() after the cue is parented to the list.
        auto *raw = cue.get();
        m_workspace->undoStack()->push(
            new core::InsertCueCommand(list, insertRow, std::move(cue)));

        if (auto *audioCue = qobject_cast<audio::AudioCue *>(raw)) {
            audioCue->prepare();
        }

        if (firstNewRow < 0) firstNewRow = insertRow;
        ++insertRow;
        ++created;
    }

    if (firstNewRow >= 0 && firstNewRow < m_model->rowCount()) {
        m_cueListView->setCurrentIndex(m_model->index(firstNewRow, 0));
    }
    return created;
}

std::unique_ptr<cues::Cue> MainWindow::cueFromFile(const QString &path)
{
    static const QStringList audioExts = {
        QStringLiteral("wav"),  QStringLiteral("mp3"),  QStringLiteral("flac"),
        QStringLiteral("aiff"), QStringLiteral("aif"),  QStringLiteral("ogg"),
        QStringLiteral("m4a"),  QStringLiteral("opus"), QStringLiteral("aac"),
        QStringLiteral("wma"),  QStringLiteral("oga"),
    };
    // Recognised but not yet implemented — flagged for the user so they
    // know quewi *saw* the drop but the cue type isn't wired yet.
    static const QStringList videoExts = {
        QStringLiteral("mp4"), QStringLiteral("mov"), QStringLiteral("mkv"),
        QStringLiteral("avi"), QStringLiteral("webm"), QStringLiteral("m4v"),
    };
    static const QStringList imageExts = {
        QStringLiteral("png"),  QStringLiteral("jpg"), QStringLiteral("jpeg"),
        QStringLiteral("gif"),  QStringLiteral("bmp"), QStringLiteral("tiff"),
        QStringLiteral("webp"),
    };

    const QFileInfo info(path);
    const QString ext = info.suffix().toLower();
    const QString stem = info.completeBaseName();

    if (audioExts.contains(ext)) {
        auto cue = std::make_unique<audio::AudioCue>();
        cue->setField(QStringLiteral("name"), stem);
        cue->setField(QStringLiteral("filePath"), path);
        return cue;
    }

    if (videoExts.contains(ext)) {
        auto cue = std::make_unique<video::VideoCue>();
        cue->setField(QStringLiteral("name"), stem);
        cue->setField(QStringLiteral("filePath"), path);
        return cue;
    }
    if (imageExts.contains(ext)) {
        auto cue = std::make_unique<video::ImageCue>();
        cue->setField(QStringLiteral("name"), stem);
        cue->setField(QStringLiteral("filePath"), path);
        return cue;
    }

    return nullptr;
}

// ---------- OSC remote API -----------------------------------------------
//
// Public addresses (full spec in docs/osc-remote-api.md):
//
//   /quewi/go               (no args)            — fire next cue (= space)
//   /quewi/panic            (no args)            — stop everything
//   /quewi/stop             (no args)            — alias for panic
//   /quewi/pause            (no args)            — soft pause
//   /quewi/cue/select <num> (float)              — select cue by number
//   /quewi/cue/start  <num> (float)              — fire that specific cue
//   /quewi/cue/stop   <num> (float)              — stop the engine voice
//                                                  bound to that cue
//   /quewi/heartbeat        (no args)            — no-op, useful for
//                                                  remote connection check

void MainWindow::registerOscRemoteHandlers()
{
    auto sub = [this](const QString &pattern, auto &&fn) {
        m_oscEngine->subscribe(pattern, std::forward<decltype(fn)>(fn));
    };

    sub("/quewi/go", [this](const osc::Message &) {
        QMetaObject::invokeMethod(this, [this]{ onGoRequested(); }, Qt::QueuedConnection);
    });
    sub("/quewi/panic", [this](const osc::Message &) {
        QMetaObject::invokeMethod(this, [this]{
            m_audioEngine->stopAll(0.05);
            m_lightingEngine->blackout();
            m_videoEngine->stopAll();
            statusBar()->showMessage(tr("PANIC: remote OSC"), 2000);
        }, Qt::QueuedConnection);
    });
    sub("/quewi/stop", [this](const osc::Message &) {
        QMetaObject::invokeMethod(this, [this]{
            m_audioEngine->stopAll(0.05);
            m_lightingEngine->blackout();
            m_videoEngine->stopAll();
        }, Qt::QueuedConnection);
    });
    sub("/quewi/pause", [this](const osc::Message &) {
        QMetaObject::invokeMethod(this, [this]{
            m_audioEngine->stopAll(0.25);
            statusBar()->showMessage(tr("Paused via OSC"), 2000);
        }, Qt::QueuedConnection);
    });
    sub("/quewi/cue/select", [this](const osc::Message &m) {
        if (m.args.empty()) return;
        double num = 0.0;
        const auto &a = m.args.front();
        switch (a.tag) {
        case osc::Argument::Tag::Int32:   num = std::get<qint32>(a.value); break;
        case osc::Argument::Tag::Float32: num = std::get<float>(a.value);  break;
        case osc::Argument::Tag::Int64:   num = static_cast<double>(std::get<qint64>(a.value)); break;
        case osc::Argument::Tag::Double:  num = std::get<double>(a.value); break;
        default: return;
        }
        QMetaObject::invokeMethod(this, [this, num]{ selectCueByNumber(num); },
                                  Qt::QueuedConnection);
    });
    sub("/quewi/cue/start", [this](const osc::Message &m) {
        if (m.args.empty()) return;
        double num = 0.0;
        const auto &a = m.args.front();
        switch (a.tag) {
        case osc::Argument::Tag::Int32:   num = std::get<qint32>(a.value); break;
        case osc::Argument::Tag::Float32: num = std::get<float>(a.value);  break;
        case osc::Argument::Tag::Int64:   num = static_cast<double>(std::get<qint64>(a.value)); break;
        case osc::Argument::Tag::Double:  num = std::get<double>(a.value); break;
        default: return;
        }
        QMetaObject::invokeMethod(this, [this, num]{ fireCueByNumber(num); },
                                  Qt::QueuedConnection);
    });
    sub("/quewi/cue/stop", [this](const osc::Message &m) {
        if (m.args.empty()) return;
        double num = 0.0;
        const auto &a = m.args.front();
        switch (a.tag) {
        case osc::Argument::Tag::Int32:   num = std::get<qint32>(a.value); break;
        case osc::Argument::Tag::Float32: num = std::get<float>(a.value);  break;
        case osc::Argument::Tag::Int64:   num = static_cast<double>(std::get<qint64>(a.value)); break;
        case osc::Argument::Tag::Double:  num = std::get<double>(a.value); break;
        default: return;
        }
        QMetaObject::invokeMethod(this, [this, num]{
            auto *list = m_workspace ? m_workspace->activeCueList() : nullptr;
            if (!list) return;
            for (int row = 0; row < list->cueCount(); ++row) {
                if (auto *c = list->cueAt(row); c && qFuzzyCompare(c->number(), num)) {
                    if (auto *ac = qobject_cast<audio::AudioCue *>(c); ac && ac->currentVoiceId()) {
                        m_audioEngine->stop(ac->currentVoiceId());
                    }
                    break;
                }
            }
        }, Qt::QueuedConnection);
    });
    sub("/quewi/heartbeat", [](const osc::Message &) {});
}

void MainWindow::selectCueByNumber(double number)
{
    auto *list = m_workspace ? m_workspace->activeCueList() : nullptr;
    if (!list) return;
    for (int row = 0; row < list->cueCount(); ++row) {
        auto *c = list->cueAt(row);
        if (c && qFuzzyCompare(c->number(), number)) {
            m_cueListView->setCurrentIndex(m_model->index(row, 0));
            return;
        }
    }
}

void MainWindow::fireCueByNumber(double number)
{
    // With QLab-style semantics (GO fires the selected cue), simply
    // selecting and firing produces the right behaviour.
    selectCueByNumber(number);
    onGoRequested();
}

} // namespace quewi
