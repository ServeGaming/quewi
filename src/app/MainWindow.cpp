#include "MainWindow.h"

#include "GoEngine.h"
#include "UpdateChecker.h"
#include "UpdateInstaller.h"

#include <QProgressDialog>
#include <QProcess>

#include <QDesktopServices>

#include "audio/AudioCue.h"
#include "audio/AudioEngine.h"
#include "audio/AudioFile.h"
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
#include "osc/OscPattern.h"
#include "show/ShowFile.h"
#include "video/VideoCue.h"
#include "video/VideoEngine.h"
#include "ui/AboutDialog.h"
#include "ui/ActiveCuesPanel.h"
#include "ui/CartView.h"
#include "ui/AudioEditorWindow.h"
#include "ui/CommandPalette.h"
#include "ui/MediaImportDialog.h"
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
#include <QCheckBox>
#include <QSettings>
#include <QKeySequence>
#include <QMenu>
#include <QToolButton>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QDir>
#include <QCryptographicHash>
#include <QInputDialog>
#include <QLineEdit>
#include <QPushButton>
#include <QDockWidget>
#include <QSplitter>
#include <QStackedWidget>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>
#include <QUuid>
#include <QStatusBar>
#include <QTabBar>
#include <QUndoStack>
#include <QUrl>
#include <QUrlQuery>
#include <QSysInfo>
#include <QLocale>
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
    const auto &tkB = ui::Theme::tokens();
    m_notifBadge->setStyleSheet(QStringLiteral(
        "QPushButton { color: %1; padding: 2px 8px; border: none; "
        "background: transparent; }"
        "QPushButton:hover { color: %2; }")
        .arg(tkB.warn.name(), tkB.warnBright.name()));
    m_notifBadge->setVisible(false);
    statusBar()->addPermanentWidget(m_notifBadge);

    // Audio memory readout. Sits permanently on the right of the
    // status bar so the operator sees decoded-residency at a glance
    // and notices when the show is approaching the budget. Polled at
    // 0.5 Hz — cheap walk of the workspace.
    m_memLabel = new QLabel(this);
    m_memLabel->setStyleSheet(QStringLiteral(
        "color:%1; padding:2px 8px;").arg(ui::Theme::tokens().ink40.name()));
    statusBar()->addPermanentWidget(m_memLabel);
    m_memTimer = new QTimer(this);
    m_memTimer->setInterval(2000);
    connect(m_memTimer, &QTimer::timeout, this, &MainWindow::refreshMemReadout);
    m_memTimer->start();
    refreshMemReadout();
    connect(m_notifBadge, &QPushButton::clicked,
            this, &MainWindow::showNotifications);
    connect(&ui::Notifications::instance(), &ui::Notifications::posted,
            this, [this](const ui::Notifications::Entry &e) {
                ++m_unreadNotifs;
                refreshNotifBadge();
                if (e.level == ui::Notifications::Level::Error)
                    statusBar()->showMessage(tr("⚠ %1").arg(e.message), 4000);
            });

    // Restore window geometry + dock layout from the previous session.
    // Done after buildLayout/buildMenus so every dock and toolbar
    // exists for restoreState() to match by objectName. Guard against
    // a stale state blob (eg. dock object renamed) by ignoring failure
    // — the default layout from buildLayout() stays in effect.
    {
        QSettings s(QStringLiteral("ServeGaming"), QStringLiteral("quewi"));
        const auto geom  = s.value(QStringLiteral("ui/mainGeometry")).toByteArray();
        const auto state = s.value(QStringLiteral("ui/mainState")).toByteArray();
        if (!geom.isEmpty())  restoreGeometry(geom);
        if (!state.isEmpty()) restoreState(state);
    }

    // Offer recovery after the main window is on screen.
    QTimer::singleShot(0, this, &MainWindow::recoverFromJournalIfPresent);

    // Silent update check on startup. Three-second delay so it doesn't
    // contend with the cold-start path; the user-facing dialog only
    // appears if a newer release is actually published.
    QSettings s(QStringLiteral("ServeGaming"), QStringLiteral("quewi"));
    if (s.value(QStringLiteral("update/checkOnStartup"), true).toBool()) {
        QTimer::singleShot(3000, this, [this]{ checkForUpdates(false); });
    }

    // If a prior update install failed, the elevated helper left a flag
    // file (it can't pop its own UI). Surface it now so a silent failure
    // can't happen twice — and point the user at the installer it kept.
    QTimer::singleShot(1500, this, [this]{
        const QString flag = QStandardPaths::writableLocation(
            QStandardPaths::AppDataLocation)
            + QStringLiteral("/update-failed.flag");
        QFile f(flag);
        if (!f.exists()) return;
        QString msg = tr("The last update didn't finish installing.");
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            const QString detail = QString::fromUtf8(f.readAll()).trimmed();
            f.close();
            if (!detail.isEmpty()) msg = detail;
        }
        QFile::remove(flag);
        QMessageBox::warning(this, tr("Update didn't install"), msg);
    });
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

    // Cue list pane: filter line on top, view fills the rest. Wrapping
    // them in one widget keeps the central layout clean.
    auto *cuePane = new QWidget(central);
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

    // Cart view sits next to the cue list inside a QStackedWidget. The
    // View menu toggles which one's visible — they share the workspace
    // data, so the same GO logic, undo stack, and inspector all work
    // in either mode.
    m_cartView = new ui::CartView(central);
    connect(m_cartView, &ui::CartView::fireRequested, this,
        [this](cues::Cue *c) {
            if (!m_goEngine || !c) return;
            // Soundboard pads route to the board's chosen output device
            // (e.g. a virtual cable), overriding each cue's own device.
            const QByteArray dev = (m_workspace && m_workspace->cart())
                ? m_workspace->cart()->outputDeviceId() : QByteArray();
            m_goEngine->fire(c, dev);
        });
    connect(m_cartView, &ui::CartView::fileDropped,
            this, &MainWindow::onCartFileDropped);
    connect(m_cartView, &ui::CartView::stopAllRequested, this,
        [this] { if (m_goEngine) m_goEngine->cancelAll(); });
    connect(m_cartView, &ui::CartView::editCueRequested, this,
        [this](cues::Cue *cue) {
            if (auto *ac = qobject_cast<audio::AudioCue *>(cue)) {
                ac->prepare();
                (new ui::AudioEditorWindow(ac, this))->show();
            }
        });

    m_centerStack = new QStackedWidget(central);
    m_centerStack->addWidget(cuePane);     // index 0 = list view
    m_centerStack->addWidget(m_cartView);  // index 1 = cart view

    // Inspector lives inside a QDockWidget so users can tear it off
    // onto a second monitor (the "I want my edit surface big" case)
    // or hide it entirely (operator-only desk). The dock state is
    // persisted in QSettings via saveState/restoreState, so the
    // layout survives restarts.
    m_inspector = new ui::Inspector(this);
    m_inspectorDock = new QDockWidget(tr("Inspector"), this);
    m_inspectorDock->setObjectName(QStringLiteral("inspectorDock"));
    m_inspectorDock->setWidget(m_inspector);
    m_inspectorDock->setAllowedAreas(Qt::LeftDockWidgetArea
                                     | Qt::RightDockWidgetArea);
    m_inspectorDock->setFeatures(QDockWidget::DockWidgetMovable
                                 | QDockWidget::DockWidgetFloatable
                                 | QDockWidget::DockWidgetClosable);
    addDockWidget(Qt::RightDockWidgetArea, m_inspectorDock);
    // Width hint for first launch — Qt clamps to size hints otherwise.
    // 420 matches the original splitter ratio (800 : 480 → 480 ≈ 38 %).
    m_inspectorDock->resize(420, m_inspectorDock->height());

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

    // Tab strip + a "+" button to add another cue list or a soundboard.
    {
        auto *tabRow = new QHBoxLayout();
        tabRow->setContentsMargins(0, 0, 0, 0);
        tabRow->setSpacing(2);
        tabRow->addWidget(m_listTabs, 1);
        auto *addTabBtn = new QToolButton(central);
        addTabBtn->setText(QStringLiteral("+"));
        addTabBtn->setToolTip(tr("Add a cue list or soundboard"));
        addTabBtn->setCursor(Qt::PointingHandCursor);
        addTabBtn->setAutoRaise(true);
        auto *addMenu = new QMenu(addTabBtn);
        addMenu->addAction(tr("New cue list"),  this, &MainWindow::addCueListTab);
        addMenu->addAction(tr("New soundboard"), this, &MainWindow::addSoundboardTab);
        addTabBtn->setMenu(addMenu);
        addTabBtn->setPopupMode(QToolButton::InstantPopup);
        tabRow->addWidget(addTabBtn, 0);
        outer->addLayout(tabRow, 0);
    }
    outer->addWidget(m_centerStack, 1);
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
    connect(m_cueListView, &ui::CueListView::emptyAreaContextMenuRequested,
            this, &MainWindow::showCueListContextMenu);
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
    file->addAction(tr("&Close show"), QKeySequence(QStringLiteral("Ctrl+W")),
                    this, &MainWindow::closeShow);
    file->addAction(tr("Reveal in Explorer/Finder"),
                    this, &MainWindow::revealShowInFolder);
    file->addSeparator();
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
        editMenuExt->addAction(tr("Renumber selection…"), this,
                               &MainWindow::renumberSelection);
    }

    auto *viewMenu = menuBar()->addMenu(tr("&View"));
    auto *themeMenu = viewMenu->addMenu(tr("&Theme"));
    themeMenu->addAction(tr("&Dark"),  this, [this]{ applyTheme(QStringLiteral("quewi-dark")); });
    themeMenu->addAction(tr("&Light"), this, [this]{ applyTheme(QStringLiteral("quewi-light")); });
    themeMenu->addAction(tr("&High contrast"),
                         this, [this]{ applyTheme(QStringLiteral("quewi-highcontrast")); });
    themeMenu->addSeparator();
    themeMenu->addAction(tr("&Midnight"),
                         this, [this]{ applyTheme(QStringLiteral("quewi-midnight")); });
    themeMenu->addAction(tr("&Forest"),
                         this, [this]{ applyTheme(QStringLiteral("quewi-forest")); });
    themeMenu->addAction(tr("&Synthwave"),
                         this, [this]{ applyTheme(QStringLiteral("quewi-synthwave")); });

    viewMenu->addSeparator();
    // QDockWidget gives us a ready-made toggleViewAction whose checked
    // state mirrors visibility — wire it in directly so the menu and
    // the dock's own close button stay in sync.
    if (m_inspectorDock) {
        auto *insp = m_inspectorDock->toggleViewAction();
        insp->setText(tr("&Inspector panel"));
        insp->setShortcut(QKeySequence(QStringLiteral("Ctrl+I")));
        viewMenu->addAction(insp);
    }
    viewMenu->addAction(tr("&Reset panel layout"), this,
                       &MainWindow::resetLayout);
    viewMenu->addSeparator();
    // The soundboard is its own tab now (not a center-stack mode toggle).
    // This jumps to it, creating it if the show doesn't have one yet.
    viewMenu->addAction(tr("&Soundboard"),
                        QKeySequence(QStringLiteral("Ctrl+Shift+C")),
                        this, &MainWindow::addSoundboardTab);

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
    cueMenu->addAction(tr("Import from &URL…"),
                       QKeySequence(QStringLiteral("Ctrl+U")),
                       this, &MainWindow::showMediaImport);
    cueMenu->addSeparator();
    cueMenu->addAction(tr("Toggle &Arm"), QKeySequence(Qt::Key_E),
                       this, &MainWindow::toggleArmSelectedCue);
    cueMenu->addAction(tr("&Delete"), QKeySequence::Delete, this, &MainWindow::deleteSelectedCue);

    auto *helpMenu = menuBar()->addMenu(tr("&Help"));
    helpMenu->addAction(tr("&Keyboard shortcuts…"),
                        this, &MainWindow::showShortcutsDialog);
    helpMenu->addAction(tr("&Notifications…"),
                        this, &MainWindow::showNotifications);
    helpMenu->addSeparator();
    helpMenu->addAction(tr("&Report a bug…"),
                        this, &MainWindow::reportBug);
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
    m_inspector->setVideoEngine(m_videoEngine.get());
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
    wireOscNotifications();
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
    m_inspector->setVideoEngine(m_videoEngine.get());
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
    prewarmAudioCues();
    wireOscNotifications();
    statusBar()->showMessage(tr("Opened %1").arg(path), 3000);

    // Preferences → Show Mode → "Enter Show Mode automatically when
    // opening a show". Honoured here (previously written but never
    // read). Only auto-enter if not already in Show Mode and a show
    // actually loaded.
    {
        QSettings autoSettings(QStringLiteral("ServeGaming"),
                               QStringLiteral("quewi"));
        if (!m_showMode
            && autoSettings.value(QStringLiteral("showmode/autoEnterOnOpen"),
                                  false).toBool()) {
            m_showMode = true;
            if (m_actShowMode) {
                QSignalBlocker blocker(m_actShowMode);
                m_actShowMode->setChecked(true);
            }
            applyShowMode();
        }
    }
    return true;
}

void MainWindow::reportBug()
{
    const QString tmpl = QStringLiteral(
        "**What happened?**\n"
        "<!-- Steps to reproduce, expected vs. actual behaviour -->\n\n"
        "**Show file**\n"
        "<!-- If relevant, attach the .quewi file or a minimal repro -->\n\n"
        "**Logs**\n"
        "<!-- Paste contents of %TEMP%/quewi-debug.log if a crash was captured -->\n\n"
        "---\n"
        "- quewi: v%1\n"
        "- OS: %2 %3 (%4)\n"
        "- Qt runtime: %5\n"
        "- Locale: %6\n")
        .arg(QStringLiteral(QUEWI_VERSION),
             QSysInfo::productType(),
             QSysInfo::productVersion(),
             QSysInfo::currentCpuArchitecture(),
             QString::fromLatin1(qVersion()),
             QLocale().name());

    QUrl url(QStringLiteral("https://github.com/ServeGaming/quewi/issues/new"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("title"),
                   QStringLiteral("[bug] "));
    q.addQueryItem(QStringLiteral("body"), tmpl);
    q.addQueryItem(QStringLiteral("labels"), QStringLiteral("bug"));
    url.setQuery(q);
    QDesktopServices::openUrl(url);
}

void MainWindow::prewarmAudioCues()
{
    if (!m_workspace) return;
    const qint64 budget = audioMemoryBudgetBytes();
    qint64 used = 0;
    int    skipped = 0;
    QString firstSkippedName;
    for (const auto &list : m_workspace->cueLists()) {
        if (!list) continue;
        const int n = list->cueCount();
        for (int i = 0; i < n; ++i) {
            auto *ac = qobject_cast<audio::AudioCue *>(list->cueAt(i));
            if (!ac) continue;
            // Estimate residency cheaply from file size on disk. Real
            // bytesUsed is 4× int16 / 2× int24 (decoded float32), but
            // disk size is a useful upper-bound proxy that doesn't
            // require touching the audio decoder. Once a file is
            // actually decoded its bytesUsed() is authoritative.
            const QString p = ac->filePath();
            if (p.isEmpty()) continue;
            qint64 estimate = ac->audioFile() ? ac->audioFile()->bytesUsed() : 0;
            if (estimate <= 0) {
                const QFileInfo fi(p);
                if (fi.exists()) {
                    // Compressed (mp3/aac/ogg) decompresses ~10×; PCM ~1×.
                    // Pick a generous 8× to err on the safe side.
                    estimate = fi.size() * 8;
                }
            }
            if (used + estimate > budget) {
                ++skipped;
                if (firstSkippedName.isEmpty())
                    firstSkippedName = ac->name().isEmpty() ? QFileInfo(p).fileName()
                                                            : ac->name();
                continue;
            }
            ac->prepare();
            // Surface decode failures in the Notifications inbox so a
            // file that won't play (unsupported codec, corrupt
            // download) is a clear message instead of a silent
            // mystery — the operator otherwise has no idea why GO
            // produced no sound.
            if (auto f = ac->audioFile()) {
                const QString fileName = QFileInfo(p).fileName();
                connect(f.get(), &audio::AudioFile::stateChanged, this,
                    [fileName](audio::AudioFile::State st) {
                        if (st == audio::AudioFile::State::Failed) {
                            ui::Notifications::instance().post(
                                ui::Notifications::Level::Error,
                                tr("Audio decode failed"),
                                tr("Couldn't decode \"%1\". Its format may be "
                                   "unsupported, or the file may be corrupt or "
                                   "incomplete.").arg(fileName));
                        }
                    }, Qt::UniqueConnection);
            }
            used += estimate;
        }
    }
    if (skipped > 0) {
        ui::Notifications::instance().post(
            ui::Notifications::Level::Warn,
            tr("Audio memory budget"),
            tr("%1 cue%2 left un-prewarmed (would exceed %3 MB cap; "
               "first: %4). They'll decode lazily on GO. Raise the "
               "cap in Preferences → Audio if your show fits.")
                .arg(skipped)
                .arg(skipped == 1 ? QString() : QStringLiteral("s"))
                .arg(budget / (1024 * 1024))
                .arg(firstSkippedName));
    }
    refreshMemReadout();
}

qint64 MainWindow::currentAudioMemoryBytes() const
{
    if (!m_workspace) return 0;
    qint64 total = 0;
    for (const auto &list : m_workspace->cueLists()) {
        if (!list) continue;
        const int n = list->cueCount();
        for (int i = 0; i < n; ++i) {
            if (auto *ac = qobject_cast<audio::AudioCue *>(list->cueAt(i))) {
                if (auto file = ac->audioFile())
                    total += file->bytesUsed();
            }
        }
    }
    return total;
}

qint64 MainWindow::audioMemoryBudgetBytes() const
{
    QSettings s(QStringLiteral("ServeGaming"), QStringLiteral("quewi"));
    const int mb = s.value(QStringLiteral("audio/memoryBudgetMB"), 512).toInt();
    return qint64(mb) * 1024 * 1024;
}

void MainWindow::refreshMemReadout()
{
    if (!m_memLabel) return;
    const qint64 used   = currentAudioMemoryBytes();
    const qint64 budget = audioMemoryBudgetBytes();
    const int usedMB    = int(used / (1024 * 1024));
    const int budgetMB  = int(budget / (1024 * 1024));
    const int voices    = m_audioEngine ? m_audioEngine->activeVoiceCount() : 0;
    m_memLabel->setText(tr("Audio: %1 / %2 MB · %3 voice%4")
        .arg(usedMB).arg(budgetMB).arg(voices)
        .arg(voices == 1 ? QString() : QStringLiteral("s")));
    // Tooltip explains what each number means so the operator can
    // tell the difference between "decoded files in RAM" (mostly
    // static) and "voices currently mixing" (rises with each fire,
    // should fall back to 0 after PANIC).
    m_memLabel->setToolTip(tr(
        "Decoded audio in RAM: %1 MB of %2 MB cap.\n"
        "Active voices: %3 — should fall to 0 within ~50 ms of PANIC.")
        .arg(usedMB).arg(budgetMB).arg(voices));
    // Warn-tinted when within 10% of the cap, red over.
    const float frac = budget > 0 ? float(used) / float(budget) : 0.f;
    const auto &tkM = ui::Theme::tokens();
    QString colour = tkM.ink40.name();
    if (frac >= 1.0f)      colour = tkM.err.name();
    else if (frac >= 0.9f) colour = tkM.warn.name();
    m_memLabel->setStyleSheet(QStringLiteral("color:%1; padding:2px 8px;").arg(colour));
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

void MainWindow::applyTheme(const QString &name)
{
    const auto qss = ui::Theme::load(name);
    if (qss.isEmpty()) return;
    // setStyleSheet() re-polishes every visible widget on the GUI
    // thread — choppy on a dense show. Suppress paints during the
    // swap so the user sees one clean redraw rather than widgets
    // rebuilding piecemeal.
    QGuiApplication::setOverrideCursor(Qt::WaitCursor);
    setUpdatesEnabled(false);
    qApp->setStyleSheet(qss);
    setUpdatesEnabled(true);
    update();
    QGuiApplication::restoreOverrideCursor();
    QSettings s(QStringLiteral("ServeGaming"), QStringLiteral("quewi"));
    s.setValue(QStringLiteral("ui/theme"), name);
    s.setValue(QStringLiteral("theme/name"), name);  // canonical key
}

void MainWindow::closeShow()
{
    if (!maybeSaveChanges()) return;
    resetWorkspace();
    rebindModel();
    m_currentPath.clear();
    setWindowTitle(QStringLiteral("quewi"));
    if (m_activePanel) m_activePanel->setWorkspace(m_workspace.get());
}

void MainWindow::revealShowInFolder()
{
    if (m_currentPath.isEmpty()) {
        QMessageBox::information(this, tr("Reveal show"),
            tr("No show file open yet. Save the show first."));
        return;
    }
    const QString dir = QFileInfo(m_currentPath).absolutePath();
    QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

void MainWindow::showPreferences()
{
    ui::PreferencesDialog dlg(m_audioEngine.get(), m_midiInput.get(), this);
    connect(&dlg, &ui::PreferencesDialog::themeChanged,
            this, &MainWindow::applyTheme);
    connect(&dlg, &ui::PreferencesDialog::rowDensityChanged, this,
            [this](const QString &) {
                // Re-apply current theme so any density-driven QSS
                // tweaks repolish. Cue row delegate respects the
                // density setting on next view rebuild.
                QSettings s(QStringLiteral("ServeGaming"),
                            QStringLiteral("quewi"));
                applyTheme(s.value(QStringLiteral("theme/name"),
                                   QStringLiteral("quewi-dark")).toString());
            });
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
        // Independent top-level window — outlives any single show, but
        // parented to MainWindow (with the Window flag so it still
        // floats free) so it's destroyed with the app instead of
        // leaking for the process lifetime.
        m_oscMonitor = new ui::OscMonitor(m_oscEngine.get(), this);
        m_oscMonitor->setWindowFlag(Qt::Window);
        m_oscMonitor->setAttribute(Qt::WA_DeleteOnClose, false);
    }
    m_oscMonitor->show();
    m_oscMonitor->raise();
    m_oscMonitor->activateWindow();
}

void MainWindow::showScriptWindow()
{
    if (!m_scriptWindow) {
        // Parented to MainWindow (Window flag keeps it free-floating)
        // so it's cleaned up with the app rather than leaking.
        m_scriptWindow = new ui::ScriptWindow(this);
        m_scriptWindow->setWindowFlag(Qt::Window);
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

void MainWindow::insertCueOfType(std::unique_ptr<cues::Cue> cue,
                                 const QString &name)
{
    auto *list = m_workspace ? m_workspace->activeCueList() : nullptr;
    if (!list || !cue) return;
    const auto idx = m_cueListView->currentIndex();
    const int insertRow = idx.isValid() ? idx.row() + 1 : list->cueCount();
    cue->setField(QStringLiteral("name"), name);
    cue->setField(QStringLiteral("number"), static_cast<double>(insertRow + 1));
    m_workspace->undoStack()->push(
        new core::InsertCueCommand(list, insertRow, std::move(cue)));
    if (m_model->rowCount() > insertRow)
        m_cueListView->setCurrentIndex(m_model->index(insertRow, 0));
}

void MainWindow::populateNewCueMenu(QMenu *menu)
{
    // Each action clears the current index first so the new cue appends at
    // the END of the list (the user right-clicked empty space below the
    // cues), then runs the same insert slot the menu bar uses.
    auto add = [this, menu](const QString &label, void (MainWindow::*slot)()) {
        menu->addAction(label, this, [this, slot]{
            m_cueListView->setCurrentIndex(QModelIndex());
            (this->*slot)();
        });
    };
    add(tr("Memo"),       &MainWindow::insertMemoCue);
    add(tr("OSC"),        &MainWindow::insertOscCue);
    add(tr("Audio"),      &MainWindow::insertAudioCue);
    add(tr("Fade"),       &MainWindow::insertFadeCue);
    add(tr("Light"),      &MainWindow::insertLightCue);
    add(tr("Light Fade"), &MainWindow::insertLightFadeCue);
    add(tr("Video"),      &MainWindow::insertVideoCue);
    add(tr("Image"),      &MainWindow::insertImageCue);
    add(tr("Text"),       &MainWindow::insertTextCue);
    menu->addSeparator();
    add(tr("Wait"),       &MainWindow::insertWaitCue);
    add(tr("Start"),      &MainWindow::insertStartCue);
    add(tr("Stop"),       &MainWindow::insertStopCue);
    add(tr("Goto"),       &MainWindow::insertGotoCue);
    add(tr("Pause"),      &MainWindow::insertPauseCue);
    add(tr("Load"),       &MainWindow::insertLoadCue);
    add(tr("Reset"),      &MainWindow::insertResetCue);
    add(tr("Devamp"),     &MainWindow::insertDevampCue);
    add(tr("Group"),      &MainWindow::insertGroupCue);
    add(tr("MIDI"),       &MainWindow::insertMidiCue);
    add(tr("MSC"),        &MainWindow::insertMscCue);
}

void MainWindow::showCueListContextMenu(const QPoint &globalPos)
{
    QMenu menu(this);

    auto *newMenu = menu.addMenu(tr("New Cue"));
    populateNewCueMenu(newMenu);

    menu.addSeparator();
    auto *pasteAct = menu.addAction(tr("Paste"), this,
        [this]{ m_cueListView->pasteCuesAtEnd(); });
    pasteAct->setEnabled(m_cueListView->canPasteCues());
    menu.addAction(tr("Import from URL…"), this, &MainWindow::showMediaImport);

    menu.addSeparator();
    menu.addAction(tr("Preferences…"), this, &MainWindow::showPreferences);

    menu.exec(globalPos);
}

// Each New-<type> menu action is a one-liner over insertCueOfType.
// The GoEngine switches behaviour by cue subclass at fire time, so
// Start / Stop / Goto / Pause / Load / Reset / Devamp all just need
// the right subclass constructed here.
void MainWindow::insertMemoCue()  { insertCueOfType(std::make_unique<cues::MemoCue>(),       tr("Memo")); }
void MainWindow::insertOscCue()   { insertCueOfType(std::make_unique<osc::OscCue>(),         tr("OSC")); }
void MainWindow::insertAudioCue() { insertCueOfType(std::make_unique<audio::AudioCue>(),     tr("Audio")); }
void MainWindow::insertFadeCue()  { insertCueOfType(std::make_unique<cues::FadeCue>(),       tr("Fade")); }
void MainWindow::insertLightCue() { insertCueOfType(std::make_unique<lighting::LightCue>(),  tr("Light")); }
void MainWindow::insertLightFadeCue() { insertCueOfType(std::make_unique<lighting::LightFadeCue>(), tr("Light Fade")); }
void MainWindow::insertVideoCue() { insertCueOfType(std::make_unique<video::VideoCue>(),     tr("Video")); }
void MainWindow::insertImageCue() { insertCueOfType(std::make_unique<video::ImageCue>(),     tr("Image")); }
void MainWindow::insertWaitCue()  { insertCueOfType(std::make_unique<cues::WaitCue>(),       tr("Wait")); }
void MainWindow::insertStartCue() { insertCueOfType(std::make_unique<cues::StartCue>(),      tr("Start")); }
void MainWindow::insertStopCue()  { insertCueOfType(std::make_unique<cues::StopCue>(),       tr("Stop")); }
void MainWindow::insertGotoCue()  { insertCueOfType(std::make_unique<cues::GotoCue>(),       tr("Goto")); }
void MainWindow::insertPauseCue() { insertCueOfType(std::make_unique<cues::PauseCue>(),      tr("Pause")); }
void MainWindow::insertLoadCue()  { insertCueOfType(std::make_unique<cues::LoadCue>(),       tr("Load")); }
void MainWindow::insertResetCue() { insertCueOfType(std::make_unique<cues::ResetCue>(),      tr("Reset")); }
void MainWindow::insertDevampCue(){ insertCueOfType(std::make_unique<cues::DevampCue>(),     tr("Devamp")); }
void MainWindow::insertGroupCue() { insertCueOfType(std::make_unique<cues::GroupCue>(),      tr("Group")); }
void MainWindow::insertMidiCue()  { insertCueOfType(std::make_unique<midi::MidiCue>(),       tr("MIDI")); }
void MainWindow::insertMscCue()   { insertCueOfType(std::make_unique<midi::MscCue>(),        tr("MSC")); }

void MainWindow::insertTextCue()
{
    // Text cue gets a default body string in addition to the name.
    auto cue = std::make_unique<video::TextCue>();
    cue->setField(QStringLiteral("text"), tr("Title"));
    insertCueOfType(std::move(cue), tr("Text"));
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

    // Optional confirmation — opt-out via Preferences → General. Only
    // prompt when actually deleting more than one cue OR when the user
    // hasn't disabled the prompt; single-cue Delete with the prompt
    // off is one tap, undo-able, no friction needed.
    {
        QSettings s(QStringLiteral("ServeGaming"), QStringLiteral("quewi"));
        const bool confirm = s.value(QStringLiteral("general/confirmDelete"),
                                     true).toBool();
        if (confirm) {
            const QString msg = rows.size() == 1
                ? tr("Delete this cue?")
                : tr("Delete %1 cues?").arg(rows.size());
            const auto ans = QMessageBox::question(this, tr("Delete cue"),
                msg + QStringLiteral("\n\n") + tr("This is undoable."),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
            if (ans != QMessageBox::Yes) return;
        }
    }

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

void MainWindow::toggleArmSelectedCue()
{
    auto *list = m_workspace->activeCueList();
    if (!list) return;

    // Collect selected rows, falling back to the current row so the
    // hotkey works on a single highlighted cue with nothing multi-selected.
    QList<int> rows;
    if (auto *sel = m_cueListView->selectionModel()) {
        for (const auto &idx : sel->selectedRows())
            if (idx.isValid()) rows << idx.row();
    }
    if (rows.isEmpty()) {
        const auto idx = m_cueListView->currentIndex();
        if (!idx.isValid()) return;
        rows << idx.row();
    }

    // Flip the whole selection to one uniform state, derived from the first
    // cue (arm-all if it's disarmed, else disarm-all), so a mixed selection
    // resolves predictably. The batch is a single undoable macro.
    auto *first = list->cueAt(rows.first());
    if (!first) return;
    const bool newArmed = !first->isArmed();

    auto *stack = m_workspace->undoStack();
    stack->beginMacro(newArmed ? tr("Arm cues") : tr("Disarm cues"));
    int changed = 0;
    for (int row : rows) {
        auto *c = list->cueAt(row);
        if (!c || c->isArmed() == newArmed) continue;
        stack->push(new core::EditCueFieldCommand(
            c, QStringLiteral("armed"),
            QVariant::fromValue(c->isArmed()),
            QVariant::fromValue(newArmed)));
        ++changed;
    }
    stack->endMacro();

    statusBar()->showMessage(
        newArmed ? tr("Armed %1 cue(s)").arg(changed)
                 : tr("Disarmed %1 cue(s)").arg(changed), 2500);
}

void MainWindow::renumberSelection()
{
    auto *list = m_workspace ? m_workspace->activeCueList() : nullptr;
    if (!list) return;

    // Collect selected rows in ascending order so renumbering walks
    // top-to-bottom matching the operator's mental model.
    QList<int> rows;
    if (auto *sel = m_cueListView->selectionModel()) {
        for (const auto &idx : sel->selectedRows()) {
            if (idx.isValid()) rows.append(idx.row());
        }
    }
    if (rows.isEmpty()) {
        const auto idx = m_cueListView->currentIndex();
        if (idx.isValid()) rows.append(idx.row());
    }
    if (rows.isEmpty()) {
        QMessageBox::information(this, tr("Renumber selection"),
            tr("Select one or more cues in the list first."));
        return;
    }
    std::sort(rows.begin(), rows.end());

    // Prompt for start + step. Default start = first selected cue's
    // current number, default step = the configured cue-numbering
    // step from Preferences → General (which defaults to 1.0).
    auto *firstCue = list->cueAt(rows.first());
    if (!firstCue) return;

    QSettings s(QStringLiteral("ServeGaming"), QStringLiteral("quewi"));
    const double defaultStep = s.value(
        QStringLiteral("general/cueNumberStep"), 1.0).toDouble();

    QDialog dlg(this);
    dlg.setWindowTitle(tr("Renumber %1 cue(s)").arg(rows.size()));
    auto *form = new QFormLayout(&dlg);

    auto *startSpin = new QDoubleSpinBox(&dlg);
    startSpin->setRange(0.0, 99999.0);
    startSpin->setDecimals(2);
    startSpin->setValue(firstCue->number());
    form->addRow(tr("Start at"), startSpin);

    auto *stepSpin = new QDoubleSpinBox(&dlg);
    stepSpin->setRange(0.01, 1000.0);
    stepSpin->setDecimals(2);
    stepSpin->setValue(defaultStep);
    form->addRow(tr("Step"), stepSpin);

    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                    &dlg);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(bb);
    if (dlg.exec() != QDialog::Accepted) return;

    const double start = startSpin->value();
    const double step  = stepSpin->value();

    // Single undo macro so one Ctrl-Z reverts the whole renumber.
    auto *stack = m_workspace->undoStack();
    stack->beginMacro(tr("Renumber %1 cues").arg(rows.size()));
    double n = start;
    for (int row : rows) {
        if (auto *c = list->cueAt(row)) {
            stack->push(new core::EditCueFieldCommand(c,
                QStringLiteral("number"),
                QVariant(c->number()),
                QVariant(n)));
            n += step;
        }
    }
    stack->endMacro();
    statusBar()->showMessage(tr("Renumbered %1 cues from %2 step %3")
        .arg(rows.size()).arg(start, 0, 'f', 2).arg(step, 0, 'f', 2), 3000);
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

void MainWindow::showMediaImport()
{
    // One-time legal disclaimer before the importer is usable.
    if (!ui::MediaImportDialog::confirmDisclaimer(this)) return;

    // Download into a 'media' folder next to the saved show so the
    // show stays self-contained and portable; fall back to a default
    // under the user's Music folder for an untitled show.
    QString destDir;
    if (!m_currentPath.isEmpty()) {
        destDir = QFileInfo(m_currentPath).absolutePath()
                + QStringLiteral("/media");
    } else {
        destDir = QStandardPaths::writableLocation(
                      QStandardPaths::MusicLocation)
                + QStringLiteral("/quewi-imports");
    }

    ui::MediaImportDialog dlg(destDir, this);
    if (dlg.exec() != QDialog::Accepted) return;
    const QString path = dlg.importedPath();
    if (path.isEmpty()) return;
    const QString base = QFileInfo(path).completeBaseName();

    // Turn the download into the matching cue type, dropped after the
    // current selection like any other New-cue action.
    if (dlg.importedIsAudio()) {
        auto cue = std::make_unique<audio::AudioCue>();
        cue->setField(QStringLiteral("filePath"), path);
        insertCueOfType(std::move(cue), base);
        prewarmAudioCues();   // decode it so it's ready to fire
    } else {
        auto cue = std::make_unique<video::VideoCue>();
        cue->setField(QStringLiteral("filePath"), path);
        insertCueOfType(std::move(cue), base);
    }
    statusBar()->showMessage(tr("Imported %1").arg(QFileInfo(path).fileName()),
                             4000);
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

    // Password gate. The operator sets an unlock PIN in
    // Preferences → Show Mode (stored plaintext at "showmode/pin" —
    // this is a fat-finger guard against accidental edits mid-show,
    // not a security boundary, so plaintext in per-user QSettings is
    // fine). A legacy SHA-256 hash at "showMode/passwordHash" set by
    // older builds' inline prompt is still honoured for verification
    // so nobody's existing lock breaks on upgrade.
    QSettings s(QStringLiteral("ServeGaming"), QStringLiteral("quewi"));
    const QString  storedPin  =
        s.value(QStringLiteral("showmode/pin")).toString();
    const QByteArray legacyHash =
        s.value(QStringLiteral("showMode/passwordHash")).toByteArray();
    const bool hasLock = !storedPin.isEmpty() || !legacyHash.isEmpty();

    auto pinMatches = [&](const QString &entered) -> bool {
        if (!storedPin.isEmpty()) return entered == storedPin;
        const QByteArray h = QCryptographicHash::hash(
            entered.toUtf8(), QCryptographicHash::Sha256).toHex();
        return h == legacyHash;
    };

    if (wantOn && !m_showMode) {
        // Entering: if no lock configured, point the operator at the
        // Preferences page rather than running a second, divergent
        // password-set flow (the old inline prompt wrote a different
        // settings key, so a PIN set here never matched the one set
        // in Preferences — that bug is why this is now a one-way nudge).
        if (!hasLock) {
            statusBar()->showMessage(
                tr("Show Mode on. Set an unlock PIN in "
                   "Preferences → Show Mode to require confirmation on exit."),
                5000);
        }
        m_showMode = true;
    } else if (!wantOn && m_showMode) {
        // Exiting: require the PIN if one is set.
        if (hasLock) {
            bool ok = false;
            const auto entered = QInputDialog::getText(this, tr("Exit Show Mode"),
                tr("Enter Show Mode PIN:"),
                QLineEdit::Password, QString(), &ok);
            if (!ok) {
                if (m_actShowMode) m_actShowMode->setChecked(true);   // revert
                return;
            }
            if (!pinMatches(entered)) {
                QMessageBox::warning(this, tr("Show Mode"),
                    tr("Incorrect PIN. Show Mode stays locked."));
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

    // Cue list itself stays selectable + GO-able but rejects edits.
    // The lock blocks drag-reorder, external file drops, cut/paste/
    // duplicate/delete shortcuts, and the right-click menu — all the
    // routes by which the operator could accidentally clobber the
    // show while pressing GO. See CueListView::setShowModeLocked.
    if (m_cueListView) m_cueListView->setShowModeLocked(m_showMode);

    // Disable Edit, Cue, List menus.
    for (QAction *act : menuBar()->actions()) {
        const auto t = act->text().remove(QChar('&'));
        if (t == tr("Edit") || t == tr("Cue") || t == tr("List") || t == tr("File")) {
            act->setEnabled(editable);
        }
    }
    if (m_actShowMode) m_actShowMode->setEnabled(true); // always allow toggle
    if (m_showModeStrip) m_showModeStrip->setVisible(m_showMode);
    // Window-level drag-drop also dies in Show Mode — otherwise the
    // operator could drop a file anywhere on the window outside the
    // cue list and bypass the lock. setAcceptDrops false makes Qt
    // skip our dragEnter/drop overrides entirely.
    setAcceptDrops(!m_showMode);

    // Honour the Preferences → Show Mode toggles (previously written
    // but never read). "Hide the menu bar" gives the operator a
    // cleaner, harder-to-fat-finger surface during a run.
    QSettings smSettings(QStringLiteral("ServeGaming"),
                         QStringLiteral("quewi"));
    const bool hideMenu = smSettings
        .value(QStringLiteral("showmode/hideMenuBar"), false).toBool();
    menuBar()->setVisible(!(m_showMode && hideMenu));

    // "Allowed during Show Mode: Pause / resume" — when off, the
    // Pause action is disabled during a run so the operator can't
    // accidentally freeze a cue. GO and Panic are always live (hard
    // rule, see the Preferences hint).
    const bool allowPause = smSettings
        .value(QStringLiteral("showmode/allowPause"), true).toBool();
    if (m_actPause) m_actPause->setEnabled(!m_showMode || allowPause);

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
        const bool sb = list->kind() == core::CueList::Kind::Soundboard;
        m_listTabs->addTab(sb ? QStringLiteral("♪ %1").arg(list->name())
                              : list->name());
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
        if (list->id() != id) continue;
        if (list->kind() == core::CueList::Kind::Soundboard) {
            // Show the board but DON'T change the active cue list or model —
            // the set list stays the GO context so opening the soundboard
            // never disturbs the running show.
            if (m_centerStack && m_cartView)
                m_centerStack->setCurrentWidget(m_cartView);
        } else {
            if (m_centerStack) m_centerStack->setCurrentIndex(0);
            m_workspace->setActiveCueList(list.get());
            m_model->setCueList(list.get());
            if (m_model->rowCount() > 0)
                m_cueListView->setCurrentIndex(m_model->index(0, 0));
        }
        return;
    }
}

core::CueList *MainWindow::getOrCreateSoundboardList()
{
    if (!m_workspace) return nullptr;
    for (const auto &list : m_workspace->cueLists())
        if (list->kind() == core::CueList::Kind::Soundboard)
            return list.get();
    auto list = std::make_unique<core::CueList>(tr("Soundboard"));
    list->setKind(core::CueList::Kind::Soundboard);
    return m_workspace->addCueList(std::move(list));
}

void MainWindow::addSoundboardTab()
{
    if (!m_workspace) return;
    auto *sb = getOrCreateSoundboardList();
    if (!sb) return;
    rebuildListTabs();
    for (int i = 0; i < m_listTabs->count(); ++i)
        if (m_listTabs->tabData(i).toUuid() == sb->id()) {
            m_listTabs->setCurrentIndex(i);
            // setCurrentIndex only emits currentChanged on an actual change;
            // force the page switch in case the soundboard tab was already
            // current (e.g. just created at index 0).
            onTabSelected(i);
            break;
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
    const QString displayName = m_currentPath.isEmpty()
        ? tr("Untitled")
        : QFileInfo(m_currentPath).completeBaseName();
    const QString fileName = m_currentPath.isEmpty()
        ? tr("Untitled")
        : QFileInfo(m_currentPath).fileName();
    const QString dirty = m_workspace && m_workspace->isDirty() ? QStringLiteral("*") : QString();
    setWindowTitle(QStringLiteral("%1%2 — quewi v%3")
        .arg(fileName, dirty, QStringLiteral(QUEWI_VERSION)));

    // Push the OSC remotes their copy of the title so they don't
    // have to poll /quewi/query/showName to keep their badges in
    // sync. Re-pushes on dirty-state toggle too — operators see the
    // asterisk in the GUI title bar, so remotes should see it
    // through the same signal. pushOscNotify no-ops when there
    // are no subscribers; no need to gate this call.
    pushOscNotify(QStringLiteral("/quewi/notify/showName/changed"),
        { osc::Argument::s(QStringLiteral("%1%2").arg(displayName, dirty)) });
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
            const QString installPrompt =
#if defined(Q_OS_WIN)
                tr("Download complete. quewi will close, install the update "
                   "(you'll see one Windows permission prompt and a short "
                   "progress bar), then reopen automatically. Continue?");
#else
                tr("Download complete. quewi will close, install the update, "
                   "and reopen automatically. Continue?");
#endif
            QMessageBox confirm(this);
            confirm.setIcon(QMessageBox::Question);
            confirm.setWindowTitle(tr("Install update"));
            confirm.setText(installPrompt);
            confirm.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
            confirm.setDefaultButton(QMessageBox::Yes);
            QSettings updSettings(QStringLiteral("ServeGaming"),
                                  QStringLiteral("quewi"));
            auto *reopenCheck = new QCheckBox(
                tr("Reopen quewi when the update finishes"), &confirm);
            reopenCheck->setChecked(updSettings.value(
                QStringLiteral("update/reopenAfter"), true).toBool());
            confirm.setCheckBox(reopenCheck);
            const auto answer = confirm.exec();
            if (answer == QMessageBox::Yes) {
                const bool reopen = reopenCheck->isChecked();
                updSettings.setValue(QStringLiteral("update/reopenAfter"), reopen);
                const bool launched =
                    UpdateInstaller::launchInstaller(localPath, reopen);
                if (launched) {
                    // The installer/helper waits for quewi to exit before it
                    // swaps files and relaunches — so we MUST quit now, or
                    // the helper waits forever and "nothing installs" (the
                    // exact reported bug). Deferred a tick so this lambda
                    // unwinds first; the normal close path still offers to
                    // save any unsaved show.
                    QTimer::singleShot(0, this, [] { QCoreApplication::quit(); });
                } else {
                    // Two recurring bug reports said this dialog showed
                    // a corrupted-looking path. Defensive rewrite:
                    //   1. Don't put the path in the dialog at all —
                    //      derive the folder fresh from QStandardPaths
                    //      so it's guaranteed-valid even if localPath
                    //      itself is somehow garbage.
                    //   2. Log the raw localPath bytes to a known file
                    //      so future reports include actual evidence
                    //      instead of relying on dialog screenshots.
                    //   3. Open folder uses the SAME guaranteed-valid
                    //      Downloads path, not localPath.
                    const QString downloadsDir =
                        QStandardPaths::writableLocation(
                            QStandardPaths::DownloadLocation);
                    const QString fileName =
                        QFileInfo(localPath).fileName();
                    const QString logDir =
                        QStandardPaths::writableLocation(
                            QStandardPaths::AppDataLocation);
                    QDir().mkpath(logDir);
                    const QString logPath =
                        logDir + QStringLiteral("/update-debug.log");
                    if (QFile log(logPath); log.open(QIODevice::Append
                                                    | QIODevice::Text)) {
                        QTextStream ts(&log);
                        ts << QDateTime::currentDateTime().toString(Qt::ISODate)
                           << " launch failed\n"
                           << "  localPath bytes: " << localPath << "\n"
                           << "  localPath utf8 hex: "
                           << localPath.toUtf8().toHex(' ') << "\n"
                           << "  downloadsDir: " << downloadsDir << "\n"
                           << "  fileName: " << fileName << "\n\n";
                    }
                    qWarning() << "UpdateInstaller: launch failed."
                               << "localPath=" << localPath
                               << "downloads=" << downloadsDir
                               << "fileName=" << fileName;

                    QMessageBox box(this);
                    box.setIcon(QMessageBox::Warning);
                    box.setWindowTitle(tr("Couldn't launch installer"));
                    box.setTextFormat(Qt::PlainText);
                    box.setText(tr("Quewi couldn't start the installer "
                                   "automatically."));
                    box.setInformativeText(tr(
                        "The installer is in your Downloads folder. "
                        "Click Open folder to find it and double-click "
                        "the .msi to install."));
                    box.setDetailedText(tr(
                        "Windows may prompt for administrator permission "
                        "and warn about an unsigned installer; that's "
                        "expected for now.\n\nA debug log was written to:\n%1")
                        .arg(QDir::toNativeSeparators(logPath)));
                    auto *openBtn = box.addButton(tr("Open folder"),
                                                  QMessageBox::ActionRole);
                    auto *logBtn = box.addButton(tr("Open log"),
                                                 QMessageBox::ActionRole);
                    box.addButton(QMessageBox::Ok);
                    box.exec();
                    if (box.clickedButton() == openBtn) {
                        QDesktopServices::openUrl(
                            QUrl::fromLocalFile(downloadsDir));
                    } else if (box.clickedButton() == logBtn) {
                        QDesktopServices::openUrl(
                            QUrl::fromLocalFile(logPath));
                    }
                }
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
    // Soundboard cues live in the dedicated soundboard list so they stay out
    // of the set list (created on first drop if the show has no board yet).
    auto *list = getOrCreateSoundboardList();
    if (!list) return;

    // Re-use the existing drag-import path that knows how to make a
    // cue from any supported file type, then bind whichever cue id
    // came back to the dropped cart cell.
    const auto added = insertCuesFromUrls({ QUrl::fromLocalFile(path) }, -1, list);
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

    // Soundboard: a Note On fires (or MIDI-learns) the matching pad when the
    // cart is the active view — so a Launchpad / pad controller drives it
    // directly. If the board consumes the note, don't also run a transport
    // binding for it.
    if (high == 0x90 && m_cartView && m_centerStack
        && m_centerStack->currentWidget() == m_cartView) {
        if (m_cartView->handleMidiNote(int(quint8(bytes[1])))) return;
    }

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
            // No platform installer attached yet (UpdateChecker no
            // longer falls back to the release-page URL — that just
            // produced a confusing 'file looks incomplete' error
            // because the HTML page is ~200 KB and fails the MSI
            // size floor). Tell the user the truth: CI is still
            // building, and they can either wait or hit the release
            // page to download whatever's there.
            if (download.isEmpty()) {
                box.setInformativeText(tr(
                    "The installer for your platform hasn't been "
                    "attached to the release yet — the build "
                    "pipeline usually finishes within ~10 minutes "
                    "of a tag push. Try again shortly, or open the "
                    "release page to see what's available."));
                auto *notes = box.addButton(tr("Open release page"),
                                            QMessageBox::ActionRole);
                box.addButton(tr("Later"), QMessageBox::RejectRole);
                box.exec();
                if (box.clickedButton() == notes) {
                    QDesktopServices::openUrl(QUrl(page));
                }
                return;
            }
            box.setInformativeText(tr("Install downloads the new version and "
                                       "launches the installer. "
                                       "\"Open in browser\" is "
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

    // Advance the standby past the cue we just fired, then skip over any
    // disarmed cues so the playhead lands on the next ARMED target (QLab
    // "skip disarmed on GO" semantics). nextCue() returned the first armed
    // cue at/after the playhead, which may be ahead of the selected row, so
    // resume from that fired cue's row rather than the raw selection.
    int firedRow = -1;
    for (int r = 0; r < m_model->rowCount(); ++r) {
        if (m_model->cueAt(m_model->index(r, 0)) == cue) { firedRow = r; break; }
    }
    int nextRow = firedRow + 1;
    while (nextRow < m_model->rowCount()) {
        auto *c = m_model->cueAt(m_model->index(nextRow, 0));
        if (c && c->isArmed()) break;
        ++nextRow;
    }
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
        } else if (file->state() == audio::AudioFile::State::Empty
                   || !file->snapshot()) {
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
        visualCue->setCurrentVoiceId(m_videoEngine->fire(p));
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
        // Persist window geometry + dock layout so the next launch
        // restores exactly where the user left things — including
        // a torn-off Inspector on a second monitor.
        QSettings s(QStringLiteral("ServeGaming"), QStringLiteral("quewi"));
        s.setValue(QStringLiteral("ui/mainGeometry"), saveGeometry());
        s.setValue(QStringLiteral("ui/mainState"),    saveState());
        clearJournal();
        event->accept();
    } else {
        event->ignore();
    }
}

void MainWindow::resetLayout()
{
    // Discard the persisted geometry/state and put the Inspector back
    // in its default position. Useful when the user has dragged a dock
    // off the visible desktop area on a monitor they no longer have.
    QSettings s(QStringLiteral("ServeGaming"), QStringLiteral("quewi"));
    s.remove(QStringLiteral("ui/mainGeometry"));
    s.remove(QStringLiteral("ui/mainState"));
    if (m_inspectorDock) {
        m_inspectorDock->setFloating(false);
        addDockWidget(Qt::RightDockWidgetArea, m_inspectorDock);
        m_inspectorDock->show();
        m_inspectorDock->resize(420, m_inspectorDock->height());
    }
    resize(1280, 800);
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

int MainWindow::insertCuesFromUrls(const QList<QUrl> &urls, int startRow,
                                   core::CueList *targetList)
{
    auto *list = targetList ? targetList
                            : (m_workspace ? m_workspace->activeCueList() : nullptr);
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

    // Only move the cue-list selection when inserting into the list the
    // view is actually showing (a soundboard-targeted drop must not yank
    // the visible set-list selection).
    if (firstNewRow >= 0 && firstNewRow < m_model->rowCount()
        && list == (m_workspace ? m_workspace->activeCueList() : nullptr)) {
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

QString MainWindow::cueToJsonString(const cues::Cue *c)
{
    if (!c) return QString();
    QJsonObject o = c->toPayload();
    // Common fields aren't in toPayload by convention — add them
    // explicitly so the remote sees one self-contained record.
    o.insert(QStringLiteral("id"), c->id().toString());
    o.insert(QStringLiteral("number"), c->number());
    o.insert(QStringLiteral("name"), c->name());
    o.insert(QStringLiteral("type"), c->typeKey());
    o.insert(QStringLiteral("preWait"), c->preWait());
    o.insert(QStringLiteral("postWait"), c->postWait());
    o.insert(QStringLiteral("notes"), c->notes());
    o.insert(QStringLiteral("armed"), c->isArmed());
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

audio::AudioCue *MainWindow::audioCueForVoice(core::CueList *list,
                                              quint64 voiceId)
{
    if (!list || voiceId == 0) return nullptr;
    for (int r = 0; r < list->cueCount(); ++r) {
        if (auto *ac = qobject_cast<audio::AudioCue *>(list->cueAt(r));
            ac && ac->currentVoiceId() == voiceId) {
            return ac;
        }
    }
    return nullptr;
}

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
    // /quewi/pause — *real* pause (voice keeps its read position),
    // not a fade-out-stop like the old behaviour. Walk every audio cue
    // in the active list and pause whichever ones currently own a
    // voice; /quewi/resume undoes the same set.
    sub("/quewi/pause", [this](const osc::Message &) {
        QMetaObject::invokeMethod(this, [this]{
            if (!m_workspace) return;
            auto *list = activeOscList();
            if (!list) return;
            int n = 0;
            for (int r = 0; r < list->cueCount(); ++r) {
                if (auto *ac = qobject_cast<audio::AudioCue *>(list->cueAt(r));
                    ac && ac->currentVoiceId()) {
                    if (m_audioEngine->pause(ac->currentVoiceId())) ++n;
                }
            }
            statusBar()->showMessage(tr("Paused %1 voices via OSC").arg(n), 2000);
        }, Qt::QueuedConnection);
    });
    sub("/quewi/resume", [this](const osc::Message &) {
        QMetaObject::invokeMethod(this, [this]{
            if (!m_workspace) return;
            auto *list = activeOscList();
            if (!list) return;
            int n = 0;
            for (int r = 0; r < list->cueCount(); ++r) {
                if (auto *ac = qobject_cast<audio::AudioCue *>(list->cueAt(r));
                    ac && ac->currentVoiceId()) {
                    if (m_audioEngine->resume(ac->currentVoiceId())) ++n;
                }
            }
            statusBar()->showMessage(tr("Resumed %1 voices via OSC").arg(n), 2000);
        }, Qt::QueuedConnection);
    });

    // ── Soundboard (cart) ───────────────────────────────────────────
    // Fire a pad by flat row-major index, or by explicit row + column:
    //   /quewi/cart/fire  i(index)
    //   /quewi/cart/fire  i(row) i(col)
    sub("/quewi/cart/fire", [this](const osc::Message &m) {
        if (m.args.empty()) return;
        const auto a0 = osc::toNumber(m.args[0]);
        const auto a1 = m.args.size() >= 2 ? osc::toNumber(m.args[1]) : std::nullopt;
        if (!a0) return;
        const int x = int(*a0);
        const int y = a1 ? int(*a1) : -1;
        QMetaObject::invokeMethod(this, [this, x, y]{
            if (!m_cartView) return;
            if (y >= 0) m_cartView->firePadAt(x, y);
            else        m_cartView->firePadIndex(x);
        }, Qt::QueuedConnection);
    });
    // Stop everything the board started (panic-lite).
    sub("/quewi/cart/stop", [this](const osc::Message &) {
        QMetaObject::invokeMethod(this, [this]{
            if (m_goEngine) m_goEngine->cancelAll();
        }, Qt::QueuedConnection);
    });
    // Bring the soundboard to the front (selects its tab, creating one if
    // the show has no board yet).
    sub("/quewi/cart/show", [this](const osc::Message &) {
        QMetaObject::invokeMethod(this, [this]{ addSoundboardTab(); },
                                  Qt::QueuedConnection);
    });

    sub("/quewi/cue/select", [this](const osc::Message &m) {
        const auto numOpt = osc::firstNumber(m);
        if (!numOpt) return;
        const double num = *numOpt;
        QMetaObject::invokeMethod(this, [this, num]{ selectCueByNumber(num); },
                                  Qt::QueuedConnection);
    });
    sub("/quewi/cue/start", [this](const osc::Message &m) {
        const auto numOpt = osc::firstNumber(m);
        if (!numOpt) return;
        const double num = *numOpt;
        QMetaObject::invokeMethod(this, [this, num]{ fireCueByNumber(num); },
                                  Qt::QueuedConnection);
    });
    sub("/quewi/cue/stop", [this](const osc::Message &m) {
        const auto numOpt = osc::firstNumber(m);
        if (!numOpt) return;
        const double num = *numOpt;
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

    // ============================================================
    // Remote API v2 — query / subscribe / push
    // ============================================================
    //
    // Address scheme (full spec in docs/osc-remote-api.md):
    //
    //   Queries (peer sends, server replies on /quewi/reply/...):
    //     /quewi/query/version          → reply (s)
    //     /quewi/query/showName         → reply (s)
    //     /quewi/query/cueLists         → reply (s s s s …)  id, name pairs
    //     /quewi/query/cues             → reply (s) — JSON array of every
    //                                       cue in the active list
    //     /quewi/query/cue <number f>   → reply (s) — JSON of one cue
    //
    //   Subscribe / unsubscribe (reply target = packet's source):
    //     /quewi/subscribe   [pattern s]   — default "/quewi/notify/*"
    //     /quewi/unsubscribe [pattern s]
    //
    //   Field setters (per cue, by number):
    //     /quewi/cue/<num>/set/<field> <value>
    //
    // Subscribers receive pushes on:
    //     /quewi/notify/cue/state <cueId s> <state s>
    //     /quewi/notify/cue/changed <cueId s> <json s>
    //     /quewi/notify/cue/added <cueId s> <row i>
    //     /quewi/notify/cue/removed <cueId s>
    //     /quewi/notify/cueList/active <listId s> <name s>
    //     /quewi/notify/workspace/changed

    // Helper: reply to whoever sent the current packet. UDP only for
    // now — TCP/WS reply would need the original socket pointer, which
    // the engine doesn't surface yet (Phase 7).
    auto replyToSender = [this](const QString &address,
                                std::vector<osc::Argument> args) {
        osc::Destination d;
        d.host = m_oscEngine->lastSenderHost();
        d.port = m_oscEngine->lastSenderPort();
        d.transport = osc::Destination::Udp;
        osc::Message msg;
        msg.address = address;
        msg.args = std::move(args);
        m_oscEngine->send(d, msg);
    };

    // Helper: serialise a cue + its payload to a JSON string suitable
    // for transport as a single OSC string argument.

    sub("/quewi/query/version", [this, replyToSender](const osc::Message &) {
        replyToSender(QStringLiteral("/quewi/reply/version"),
            { osc::Argument::s(QStringLiteral(QUEWI_VERSION)) });
    });

    // What the user actually sees in the window title bar. Picks the
    // file name (if a show is open) over Workspace::name(), which
    // tends to stay at the "Untitled Show" default unless explicitly
    // set in Preferences — and that mismatch had remotes reporting
    // "Untitled show" for every loaded file regardless of the actual
    // filename on disk.
    auto displayShowName = [this]() -> QString {
        if (!m_currentPath.isEmpty()) {
            return QFileInfo(m_currentPath).completeBaseName();
        }
        const QString wsName = m_workspace ? m_workspace->name() : QString();
        if (!wsName.isEmpty()) return wsName;
        return tr("Untitled");
    };

    sub("/quewi/query/showName",
        [this, replyToSender, displayShowName](const osc::Message &) {
        replyToSender(QStringLiteral("/quewi/reply/showName"),
            { osc::Argument::s(displayShowName()) });
    });

    sub("/quewi/query/cueLists", [this, replyToSender](const osc::Message &) {
        std::vector<osc::Argument> args;
        if (m_workspace) {
            for (const auto &up : m_workspace->cueLists()) {
                args.push_back(osc::Argument::s(up->id().toString()));
                args.push_back(osc::Argument::s(up->name()));
            }
        }
        replyToSender(QStringLiteral("/quewi/reply/cueLists"), std::move(args));
    });

    // (See MainWindow::activeOscList — single source of truth used
    // by every OSC handler that needs "the cue list the operator is
    // actually looking at." Defined as a method so all lambda
    // captures of `this` can call it without a separate capture.)

    sub("/quewi/query/cues",
        [this, replyToSender](const osc::Message &) {
        QJsonArray arr;
        if (auto *list = activeOscList()) {
            for (int r = 0; r < list->cueCount(); ++r) {
                if (auto *c = list->cueAt(r)) {
                    arr.append(QJsonDocument::fromJson(
                        cueToJsonString(c).toUtf8()).object());
                }
            }
        }
        const QString fullJson = QString::fromUtf8(
            QJsonDocument(arr).toJson(QJsonDocument::Compact));

        // UDP datagrams have a practical ~64 KB ceiling — and most
        // network stacks reject single packets above ~9 KB even
        // though IP fragmentation theoretically allows more. A
        // show with a few hundred cues easily blows past that as
        // a single JSON blob, and the QUdpSocket return value
        // surfaces as 'OSC datagram was too large to send' in the
        // engine's error signal.
        //
        // Small replies still ship as one /quewi/reply/cues (s)
        // message (existing on-the-wire behaviour, all remotes
        // already speak it). Large replies are split into
        // /quewi/reply/cues/chunk (i i s) — chunk index, total
        // chunks, partial JSON. Remotes concatenate chunks in
        // order (0..total-1) and parse the assembled string.
        //
        // Slicing happens at the QString level (UTF-16 code units),
        // not raw UTF-8 bytes, so we can't cut a multi-byte UTF-8
        // sequence in half — the OSC codec re-encodes each slice
        // to valid UTF-8 before it goes on the wire.
        //
        // Safe-payload sizing: 16 K code units per chunk gives
        // ~16-48 KB UTF-8 bytes after encoding (worst case for
        // BMP text). Leaves headroom under typical 64 KB UDP
        // ceilings even after OSC envelope overhead.
        constexpr int kSingleMessageMax = 16 * 1024;
        constexpr int kChunkPayloadSize = 16 * 1024;

        if (fullJson.size() <= kSingleMessageMax) {
            replyToSender(QStringLiteral("/quewi/reply/cues"),
                { osc::Argument::s(fullJson) });
        } else {
            const int total = (fullJson.size() + kChunkPayloadSize - 1)
                              / kChunkPayloadSize;
            for (int i = 0; i < total; ++i) {
                const QString slice = fullJson.mid(
                    i * kChunkPayloadSize, kChunkPayloadSize);
                replyToSender(
                    QStringLiteral("/quewi/reply/cues/chunk"),
                    { osc::Argument::i(i),
                      osc::Argument::i(total),
                      osc::Argument::s(slice) });
            }
        }
    });

    sub("/quewi/query/cue",
        [this, replyToSender](const osc::Message &m) {
        const auto numOpt = osc::firstNumber(m);
        if (!numOpt) return;
        const double num = *numOpt;
        auto *list = activeOscList();
        if (!list) return;
        for (int r = 0; r < list->cueCount(); ++r) {
            if (auto *c = list->cueAt(r); c && qFuzzyCompare(c->number(), num)) {
                replyToSender(QStringLiteral("/quewi/reply/cue"),
                    { osc::Argument::s(cueToJsonString(c)) });
                return;
            }
        }
    });

    // ────────────────────────────────────────────────────────────
    // /quewi/query/playingCues
    //
    // Reply: /quewi/reply/playingCues  s  (JSON array)
    //   [ { id, type, number, state }, ... ]
    //
    // state ∈ { "playing", "paused", "fading-out" } today; only
    // audio cues report state right now because the audio engine
    // has the cleanest active-voice model. Light fades and video
    // playback will join as their engines surface per-cue
    // running state — keep that future in mind when consuming.
    //
    // Designed so a remote that just reconnected can rebuild its
    // "now playing" view without replaying the notification stream.
    // ────────────────────────────────────────────────────────────
    sub("/quewi/query/playingCues",
        [this, replyToSender](const osc::Message &) {
        QJsonArray arr;
        if (m_audioEngine && m_workspace) {
            auto *list = m_workspace->activeCueList();
            const auto voices = m_audioEngine->activeVoices();
            for (const auto &v : voices) {
                // Match voice to its owning cue via currentVoiceId
                // — set at fire() time, cleared on voiceFinished.
                cues::Cue *owner = audioCueForVoice(list, v.id);
                if (!owner) continue;
                QJsonObject o;
                o.insert(QStringLiteral("id"), owner->id().toString());
                o.insert(QStringLiteral("type"), owner->typeKey());
                o.insert(QStringLiteral("number"), owner->number());
                o.insert(QStringLiteral("state"),
                    m_audioEngine->isPaused(v.id)
                        ? QStringLiteral("paused")
                        : QStringLiteral("playing"));
                arr.append(o);
            }
        }
        const QString json = QString::fromUtf8(
            QJsonDocument(arr).toJson(QJsonDocument::Compact));
        replyToSender(QStringLiteral("/quewi/reply/playingCues"),
            { osc::Argument::s(json) });
    });

    // Subscribe / unsubscribe — peer wants notifications. Subscriber
    // identity = host:port of the source UDP packet. Optional first
    // arg is the address pattern (default "/quewi/notify/*").
    sub("/quewi/subscribe", [this, replyToSender](const osc::Message &m) {
        QString pattern = QStringLiteral("/quewi/notify/*");
        if (!m.args.empty() && m.args.front().tag == osc::Argument::Tag::String) {
            pattern = std::get<QString>(m.args.front().value);
        }
        const QString host = m_oscEngine->lastSenderHost();
        const quint16 port = m_oscEngine->lastSenderPort();
        // Dedup by host:port + pattern
        for (const auto &s : m_oscSubscribers) {
            if (s.host == host && s.port == port && s.pattern == pattern)
                return;
        }
        m_oscSubscribers.push_back({ host, port, pattern });
        statusBar()->showMessage(
            tr("OSC subscriber: %1:%2 → %3").arg(host).arg(port).arg(pattern),
            2500);
        // If audio is already playing when the remote subscribes,
        // start (or keep running) the 4 Hz playback heartbeat so the
        // new subscriber sees progress immediately instead of waiting
        // for the next cue fire to kick the timer.
        maybeStartPlaybackPush();
        // Confirmation reply — handles the "did my subscribe packet
        // make it?" question without forcing the remote to wait for
        // the first notify event (which might be hours away on a
        // pre-show cue list). Pattern echoes the value the server
        // actually registered (with default substituted if blank);
        // count is the total number of distinct (host, port, pattern)
        // entries currently in the subscribers list, so a remote can
        // surface "subscribed (3 active)" status.
        replyToSender(QStringLiteral("/quewi/reply/subscribe"),
            { osc::Argument::s(pattern),
              osc::Argument::i(static_cast<int>(m_oscSubscribers.size())) });
    });
    sub("/quewi/unsubscribe", [this](const osc::Message &m) {
        const QString host = m_oscEngine->lastSenderHost();
        const quint16 port = m_oscEngine->lastSenderPort();
        QString pattern;
        if (!m.args.empty() && m.args.front().tag == osc::Argument::Tag::String) {
            pattern = std::get<QString>(m.args.front().value);
        }
        m_oscSubscribers.erase(std::remove_if(m_oscSubscribers.begin(),
            m_oscSubscribers.end(),
            [&](const OscSubscriberRec &s) {
                return s.host == host && s.port == port
                    && (pattern.isEmpty() || s.pattern == pattern);
            }), m_oscSubscribers.end());
    });

    // ============================================================
    // Remote API v3 — engine targeting, cue editing, workspace ops
    // ============================================================

    // Per-engine stops. /quewi/stop and /quewi/panic kill everything;
    // these let a controller blackout lights without cutting audio,
    // or freeze video while music plays through.
    sub("/quewi/lighting/blackout", [this](const osc::Message &) {
        QMetaObject::invokeMethod(this, [this]{
            if (m_lightingEngine) m_lightingEngine->blackout();
            statusBar()->showMessage(tr("Lighting blackout via OSC"), 2000);
        }, Qt::QueuedConnection);
    });
    sub("/quewi/video/stop", [this](const osc::Message &) {
        QMetaObject::invokeMethod(this, [this]{
            if (m_videoEngine) m_videoEngine->stopAll();
            statusBar()->showMessage(tr("Video stop via OSC"), 2000);
        }, Qt::QueuedConnection);
    });

    // Graceful fade-outs per engine. Takes a duration in seconds
    // (float / double / int — any numeric tag). Lighting fades all
    // active DMX channels to 0; video animates layer opacity to 0
    // then stops; audio uses stopAll(seconds) which respects each
    // voice's per-voice gain ramp.
    auto extractSeconds = [](const osc::Message &m, double fallback) -> double {
        return osc::firstNumber(m).value_or(fallback);
    };

    sub("/quewi/lighting/fadeOut", [this, extractSeconds](const osc::Message &m) {
        const double dur = extractSeconds(m, 2.0);
        QMetaObject::invokeMethod(this, [this, dur]{
            if (m_lightingEngine) m_lightingEngine->fadeOutAll(dur);
            statusBar()->showMessage(
                tr("Lighting fade-out over %1 s via OSC").arg(dur, 0, 'f', 2),
                2000);
        }, Qt::QueuedConnection);
    });
    sub("/quewi/video/fadeOut", [this, extractSeconds](const osc::Message &m) {
        const double dur = extractSeconds(m, 1.0);
        QMetaObject::invokeMethod(this, [this, dur]{
            if (m_videoEngine) m_videoEngine->fadeOutAll(dur);
            statusBar()->showMessage(
                tr("Video fade-out over %1 s via OSC").arg(dur, 0, 'f', 2),
                2000);
        }, Qt::QueuedConnection);
    });
    // /quewi/fadeAll — soft stop everything in one duration window.
    // Audio, lighting, and video all ramp to silent / black / empty
    // over the same number of seconds. The headline 'one button =
    // graceful stop' control for remotes.
    sub("/quewi/fadeAll", [this, extractSeconds](const osc::Message &m) {
        const double dur = extractSeconds(m, 2.0);
        QMetaObject::invokeMethod(this, [this, dur]{
            if (m_audioEngine)    m_audioEngine->stopAll(dur);
            if (m_lightingEngine) m_lightingEngine->fadeOutAll(dur);
            if (m_videoEngine)    m_videoEngine->fadeOutAll(dur);
            statusBar()->showMessage(
                tr("Fade all over %1 s via OSC").arg(dur, 0, 'f', 2),
                2000);
        }, Qt::QueuedConnection);
    });

    // Active cue list — controller can rotate between named lists.
    // Argument may be a UUID string OR a name; UUID wins on tie.
    sub("/quewi/cueList/select", [this](const osc::Message &m) {
        if (m.args.empty()) return;
        if (m.args.front().tag != osc::Argument::Tag::String) return;
        const QString key = std::get<QString>(m.args.front().value);
        QMetaObject::invokeMethod(this, [this, key]{
            if (!m_workspace) return;
            const QUuid asId(key);
            core::CueList *target = nullptr;
            for (const auto &up : m_workspace->cueLists()) {
                if ((!asId.isNull() && up->id() == asId) || up->name() == key) {
                    target = up.get();
                    break;
                }
            }
            if (target) {
                m_workspace->setActiveCueList(target);
                rebindModel();
                rebuildListTabs();
            }
        }, Qt::QueuedConnection);
    });

    // Cue add. Arg 0 = type key string (see docs/osc-remote-api.md for
    // the full list: audio, memo, osc, fade, group, wait, light,
    // light-fade, video, image, text, midi, msc, start, stop, goto,
    // pause, load, reset, devamp).
    // Optional arg 1 = cue number (float). Optional arg 2 = display name.
    sub("/quewi/cue/add", [this](const osc::Message &m) {
        if (m.args.empty() || m.args.front().tag != osc::Argument::Tag::String) return;
        const QString typeKey = std::get<QString>(m.args.front().value);
        double number = -1.0;
        QString name;
        if (m.args.size() > 1) {
            number = osc::toNumber(m.args[1]).value_or(-1.0);
        }
        if (m.args.size() > 2 && m.args[2].tag == osc::Argument::Tag::String) {
            name = std::get<QString>(m.args[2].value);
        }
        QMetaObject::invokeMethod(this, [this, typeKey, number, name]{
            if (!m_workspace) return;
            auto *list = activeOscList();
            if (!list) return;
            // Type-key factory — covers every cue subclass exposed in
            // the New-Cue menu. Returning nullptr means the controller
            // sent a typo or an unsupported type.
            std::unique_ptr<cues::Cue> cue;
            if      (typeKey == QLatin1String("memo"))       cue = std::make_unique<cues::MemoCue>();
            else if (typeKey == QLatin1String("osc"))        cue = std::make_unique<osc::OscCue>();
            else if (typeKey == QLatin1String("audio"))      cue = std::make_unique<audio::AudioCue>();
            else if (typeKey == QLatin1String("fade"))       cue = std::make_unique<cues::FadeCue>();
            else if (typeKey == QLatin1String("group"))      cue = std::make_unique<cues::GroupCue>();
            else if (typeKey == QLatin1String("wait"))       cue = std::make_unique<cues::WaitCue>();
            else if (typeKey == QLatin1String("light"))      cue = std::make_unique<lighting::LightCue>();
            else if (typeKey == QLatin1String("light-fade")) cue = std::make_unique<lighting::LightFadeCue>();
            else if (typeKey == QLatin1String("video"))      cue = std::make_unique<video::VideoCue>();
            else if (typeKey == QLatin1String("image"))      cue = std::make_unique<video::ImageCue>();
            else if (typeKey == QLatin1String("text"))       cue = std::make_unique<video::TextCue>();
            else if (typeKey == QLatin1String("midi"))       cue = std::make_unique<midi::MidiCue>();
            else if (typeKey == QLatin1String("msc"))        cue = std::make_unique<midi::MscCue>();
            else if (typeKey == QLatin1String("start"))      cue = std::make_unique<cues::StartCue>();
            else if (typeKey == QLatin1String("stop"))       cue = std::make_unique<cues::StopCue>();
            else if (typeKey == QLatin1String("goto"))       cue = std::make_unique<cues::GotoCue>();
            else if (typeKey == QLatin1String("pause"))      cue = std::make_unique<cues::PauseCue>();
            else if (typeKey == QLatin1String("load"))       cue = std::make_unique<cues::LoadCue>();
            else if (typeKey == QLatin1String("reset"))      cue = std::make_unique<cues::ResetCue>();
            else if (typeKey == QLatin1String("devamp"))     cue = std::make_unique<cues::DevampCue>();
            if (!cue) {
                statusBar()->showMessage(
                    tr("OSC cue/add: unknown type \"%1\"").arg(typeKey), 3000);
                return;
            }
            const int insertRow = list->cueCount();
            const double useNumber = (number > 0.0)
                ? number : double(insertRow + 1);
            cue->setField(QStringLiteral("number"), useNumber);
            cue->setField(QStringLiteral("name"),
                name.isEmpty() ? cue->typeName() : name);
            m_workspace->undoStack()->push(
                new core::InsertCueCommand(list, insertRow, std::move(cue)));
        }, Qt::QueuedConnection);
    });

    // Cue move — `/quewi/cue/<num>/move <new_row i>`. The number-in-
    // path route mirrors /quewi/cue/<n>/set/<field>: the OSC server's
    // pattern matcher handles the path-as-wildcard split. new_row is
    // 0-based and target-frame (after move): a cue at row 5 moved to
    // new_row=2 ends up at row 2; the existing rows 2..4 shift down.
    // Posts /quewi/notify/cue/moved and /quewi/notify/cueList/reordered
    // after the undo command applies, so remote caches stay in sync
    // without re-querying.
    sub("/quewi/cue/*/move", [this](const osc::Message &m) {
        const auto parts = m.address.split(QChar('/'), Qt::SkipEmptyParts);
        if (parts.size() < 4) return;
        bool ok = false;
        const double num = parts.value(2).toDouble(&ok);
        if (!ok) return;
        const auto rowOpt = osc::firstNumber(m);
        if (!rowOpt) return;
        const int newRow = static_cast<int>(*rowOpt);
        QMetaObject::invokeMethod(this, [this, num, newRow]{
            if (!m_workspace) return;
            auto *list = activeOscList();
            if (!list) return;
            for (int r = 0; r < list->cueCount(); ++r) {
                if (auto *c = list->cueAt(r); c && qFuzzyCompare(c->number(), num)) {
                    const int clamped = std::clamp(newRow, 0, list->cueCount());
                    if (clamped == r) return;
                    const QString cueId = c->id().toString();
                    m_workspace->undoStack()->push(
                        new core::MoveCueCommand(list, r, clamped));
                    pushOscNotify(
                        QStringLiteral("/quewi/notify/cue/moved"),
                        { osc::Argument::s(cueId),
                          osc::Argument::i(r),
                          osc::Argument::i(clamped) });
                    // Full ordered id list — lets a remote rebuild its
                    // cache without re-fetching every cue.
                    QJsonArray order;
                    for (int i = 0; i < list->cueCount(); ++i) {
                        if (auto *c2 = list->cueAt(i))
                            order.append(c2->id().toString());
                    }
                    pushOscNotify(
                        QStringLiteral("/quewi/notify/cueList/reordered"),
                        { osc::Argument::s(list->id().toString()),
                          osc::Argument::s(QString::fromUtf8(
                              QJsonDocument(order).toJson(
                                  QJsonDocument::Compact))) });
                    return;
                }
            }
        }, Qt::QueuedConnection);
    });

    // Workspace state — combined query used by remotes to show
    // 'unsaved changes' badges and the current show name.
    sub("/quewi/query/workspace",
        [this, replyToSender](const osc::Message &) {
        QJsonObject o;
        if (m_workspace) {
            o.insert(QStringLiteral("name"), m_workspace->name());
            o.insert(QStringLiteral("path"), m_currentPath);
            o.insert(QStringLiteral("dirty"), m_workspace->isDirty());
            // lastSavedTs: filesystem mtime of the current path,
            // 0 if untitled or save-never. Cheap enough at query
            // time that we don't need a cached value.
            qint64 lastSavedTs = 0;
            if (!m_currentPath.isEmpty()) {
                const QFileInfo fi(m_currentPath);
                if (fi.exists()) {
                    lastSavedTs = fi.lastModified().toSecsSinceEpoch();
                }
            }
            o.insert(QStringLiteral("lastSavedTs"), double(lastSavedTs));
        }
        replyToSender(QStringLiteral("/quewi/reply/workspace"),
            { osc::Argument::s(QString::fromUtf8(
                QJsonDocument(o).toJson(QJsonDocument::Compact))) });
    });

    // Richer cue-list reply — JSON array with cue counts and active
    // flag. Pairs with the legacy `/quewi/reply/cueLists s s s s ...`
    // form; remotes pick whichever shape they need.
    sub("/quewi/query/cueListDetails",
        [this, replyToSender](const osc::Message &) {
        QJsonArray arr;
        if (m_workspace) {
            const auto *active = m_workspace->activeCueList();
            for (const auto &up : m_workspace->cueLists()) {
                QJsonObject o;
                o.insert(QStringLiteral("id"), up->id().toString());
                o.insert(QStringLiteral("name"), up->name());
                o.insert(QStringLiteral("cueCount"), up->cueCount());
                o.insert(QStringLiteral("isActive"), up.get() == active);
                arr.append(o);
            }
        }
        replyToSender(QStringLiteral("/quewi/reply/cueListDetails"),
            { osc::Argument::s(QString::fromUtf8(
                QJsonDocument(arr).toJson(QJsonDocument::Compact))) });
    });

    // Cue remove by number. The recipient is the active cue list.
    sub("/quewi/cue/remove", [this](const osc::Message &m) {
        const auto numOpt = osc::firstNumber(m);
        if (!numOpt) return;
        const double num = *numOpt;
        QMetaObject::invokeMethod(this, [this, num]{
            if (!m_workspace) return;
            auto *list = activeOscList();
            if (!list) return;
            for (int r = 0; r < list->cueCount(); ++r) {
                if (auto *c = list->cueAt(r); c && qFuzzyCompare(c->number(), num)) {
                    m_workspace->undoStack()->push(
                        new core::RemoveCueCommand(list, r));
                    return;
                }
            }
        }, Qt::QueuedConnection);
    });

    // Workspace file ops. open/save take a no-args form; open with a
    // string arg loads from that path. These bypass the normal "save
    // dirty?" prompt — controllers are expected to coordinate that.
    sub("/quewi/workspace/new", [this](const osc::Message &) {
        QMetaObject::invokeMethod(this, [this]{ newShow(); },
                                  Qt::QueuedConnection);
    });
    sub("/quewi/workspace/open", [this](const osc::Message &m) {
        QString path;
        if (!m.args.empty() && m.args.front().tag == osc::Argument::Tag::String)
            path = std::get<QString>(m.args.front().value);
        QMetaObject::invokeMethod(this, [this, path]{
            if (path.isEmpty()) openShow();
            else                loadShowFromPath(path);
        }, Qt::QueuedConnection);
    });
    sub("/quewi/workspace/save", [this](const osc::Message &) {
        QMetaObject::invokeMethod(this, [this]{ saveShow(); },
                                  Qt::QueuedConnection);
    });

    // Undo / redo. The undo stack is workspace-owned, so the address
    // implicitly targets whatever workspace is currently loaded.
    sub("/quewi/undo", [this](const osc::Message &) {
        QMetaObject::invokeMethod(this, [this]{
            if (m_workspace) m_workspace->undoStack()->undo();
        }, Qt::QueuedConnection);
    });
    sub("/quewi/redo", [this](const osc::Message &) {
        QMetaObject::invokeMethod(this, [this]{
            if (m_workspace) m_workspace->undoStack()->redo();
        }, Qt::QueuedConnection);
    });

    // Generic field setter: /quewi/cue/<num>/set/<field> <value>
    // Subscribed via wildcard pattern; handler parses the address.
    sub("/quewi/cue/*/set/*", [this](const osc::Message &m) {
        // Address like "/quewi/cue/3.5/set/name"
        const auto parts = m.address.split(QChar('/'), Qt::SkipEmptyParts);
        if (parts.size() < 5) return;
        bool ok = false;
        const double num = parts.value(2).toDouble(&ok);
        if (!ok) return;
        const QString field = parts.value(4);
        if (m.args.empty()) return;
        QVariant v;
        const auto &a = m.args.front();
        switch (a.tag) {
        case osc::Argument::Tag::Int32:   v = std::get<qint32>(a.value); break;
        case osc::Argument::Tag::Int64:   v = static_cast<qlonglong>(std::get<qint64>(a.value)); break;
        case osc::Argument::Tag::Float32: v = std::get<float>(a.value); break;
        case osc::Argument::Tag::Double:  v = std::get<double>(a.value); break;
        case osc::Argument::Tag::String:  v = std::get<QString>(a.value); break;
        case osc::Argument::Tag::True:    v = true; break;
        case osc::Argument::Tag::False:   v = false; break;
        default: return;
        }
        QMetaObject::invokeMethod(this, [this, num, field, v]{
            if (!m_workspace) return;
            auto *list = activeOscList();
            if (!list) return;
            for (int r = 0; r < list->cueCount(); ++r) {
                if (auto *c = list->cueAt(r); c && qFuzzyCompare(c->number(), num)) {
                    c->setField(field, v);
                    return;
                }
            }
        }, Qt::QueuedConnection);
    });
}

core::CueList *MainWindow::activeOscList() const
{
    if (m_model && m_model->cueList()) return m_model->cueList();
    return m_workspace ? m_workspace->activeCueList() : nullptr;
}

void MainWindow::pushOscNotify(const QString &address,
                               std::vector<osc::Argument> args)
{
    if (m_oscSubscribers.empty()) return;
    osc::Message msg;
    msg.address = address;
    msg.args = std::move(args);
    for (const auto &s : m_oscSubscribers) {
        if (!osc::Pattern::matches(s.pattern, address)) continue;
        osc::Destination d;
        d.host = s.host;
        d.port = s.port;
        d.transport = osc::Destination::Udp;
        m_oscEngine->send(d, msg);
    }
}

// ───────────────────────────────────────────────────────────────────
//  Playback push heartbeat — 4 Hz feed of /quewi/notify/cue/playback
//
//  The timer only ticks when there's both a subscriber AND something
//  to report. It self-stops as soon as one of those goes away. Stage
//  managers' remotes use the elapsed/remaining values to render a
//  transport progress bar — without this push the only way for a
//  remote to know playback position is to keep polling, which is
//  expensive and laggy.
// ───────────────────────────────────────────────────────────────────
void MainWindow::maybeStartPlaybackPush()
{
    if (!m_oscPlaybackTimer) {
        m_oscPlaybackTimer = new QTimer(this);
        m_oscPlaybackTimer->setInterval(250);   // 4 Hz
        connect(m_oscPlaybackTimer, &QTimer::timeout,
                this, &MainWindow::pushPlaybackHeartbeat);
    }
    if (m_oscPlaybackTimer->isActive()) return;
    if (m_oscSubscribers.empty()) return;
    if (!m_audioEngine || m_audioEngine->activeVoiceCount() <= 0) return;
    m_oscPlaybackTimer->start();
}

void MainWindow::pushPlaybackHeartbeat()
{
    // Bail out and stop the timer if nobody's listening or nothing
    // is playing. Saves CPU and battery when quewi is idle.
    if (m_oscSubscribers.empty()
        || !m_audioEngine
        || m_audioEngine->activeVoiceCount() <= 0)
    {
        if (m_oscPlaybackTimer) m_oscPlaybackTimer->stop();
        return;
    }

    auto *list = m_workspace ? m_workspace->activeCueList() : nullptr;
    const auto voices = m_audioEngine->activeVoices();
    for (const auto &v : voices) {
        // Resolve voice → owning cue. Cue is required to address
        // the push (`id` field); orphan voices skip silently.
        cues::Cue *owner = audioCueForVoice(list, v.id);
        if (!owner) continue;

        const bool paused = m_audioEngine->isPaused(v.id);
        const QString state = paused ? QStringLiteral("paused")
                                     : QStringLiteral("playing");
        const double elapsed = v.positionSeconds;
        const double remaining = (v.durationSeconds > 0.0)
            ? std::max(0.0, v.durationSeconds - v.positionSeconds)
            : -1.0;
        const double position = v.positionSeconds;

        pushOscNotify(QStringLiteral("/quewi/notify/cue/playback"),
            { osc::Argument::s(owner->id().toString()),
              osc::Argument::s(state),
              osc::Argument::d(elapsed),
              osc::Argument::d(remaining),
              osc::Argument::d(position) });
    }
}

void MainWindow::wireOscNotifications()
{
    // Disconnect anything we wired against the previous workspace —
    // resetWorkspace replaces the unique_ptr, so the old connections
    // are dead anyway, but clearing the list keeps the bookkeeping
    // honest and avoids accidental double-fires.
    for (auto &c : m_oscNotifyConnections) QObject::disconnect(c);
    m_oscNotifyConnections.clear();
    if (!m_workspace) return;

    auto *list = m_workspace->activeCueList();
    if (!list) return;

    // List-level: rows added, removed, changed.
    m_oscNotifyConnections.append(connect(list, &core::CueList::cueInserted, this,
        [this, list](int row) {
            if (auto *c = list->cueAt(row)) {
                // (cueId, row, cueNumber). The cue number went on
                // the end of the arg list (not in place of the row)
                // so older clients that only read the first two
                // args still parse correctly. The number lets
                // remotes immediately follow up with
                // /quewi/query/cue <num> to fetch the new cue's
                // full JSON, instead of having to re-query the
                // whole list to learn the number associated with
                // the row index.
                pushOscNotify(QStringLiteral("/quewi/notify/cue/added"),
                    { osc::Argument::s(c->id().toString()),
                      osc::Argument::i(row),
                      osc::Argument::d(c->number()) });
            }
        }));
    m_oscNotifyConnections.append(connect(list, &core::CueList::cueChanged, this,
        [this, list](int row) {
            auto *c = list->cueAt(row);
            if (!c) return;
            pushOscNotify(QStringLiteral("/quewi/notify/cue/changed"),
                { osc::Argument::s(c->id().toString()),
                  osc::Argument::s(cueToJsonString(c)) });
        }));
    m_oscNotifyConnections.append(connect(list, &core::CueList::aboutToRemoveCue, this,
        [this, list](int row) {
            if (auto *c = list->cueAt(row)) {
                pushOscNotify(QStringLiteral("/quewi/notify/cue/removed"),
                    { osc::Argument::s(c->id().toString()) });
            }
        }));

    // Workspace-level: active list switched.
    m_oscNotifyConnections.append(connect(m_workspace.get(),
        &core::Workspace::activeCueListChanged, this, [this] {
            if (auto *l = m_workspace->activeCueList()) {
                pushOscNotify(QStringLiteral("/quewi/notify/cueList/active"),
                    { osc::Argument::s(l->id().toString()),
                      osc::Argument::s(l->name()) });
            }
        }));

    // Workspace-level: dirty-state transitions. Remotes use this to
    // show 'unsaved changes' badges without polling. Pushed both
    // on becomes-dirty (any edit) and becomes-clean (save). The
    // existing /quewi/notify/workspace/changed remains for full
    // reloads — different semantic.
    m_oscNotifyConnections.append(connect(m_workspace.get(),
        &core::Workspace::dirtyChanged, this, [this] {
            pushOscNotify(QStringLiteral("/quewi/notify/workspace/dirty"),
                { (m_workspace && m_workspace->isDirty())
                    ? osc::Argument::T() : osc::Argument::F() });
        }));

    // GoEngine-level: cue actually fired (transport state push). The
    // controller side uses this to render a "now playing" view without
    // polling.
    if (m_goEngine) {
        m_oscNotifyConnections.append(connect(m_goEngine.get(),
            &GoEngine::cueFired, this, [this](cues::Cue *c) {
                if (!c) return;
                pushOscNotify(QStringLiteral("/quewi/notify/cue/state"),
                    { osc::Argument::s(c->id().toString()),
                      osc::Argument::s(QStringLiteral("fired")),
                      osc::Argument::d(c->number()) });
                // Kick the 4 Hz playback push so a remote subscriber
                // gets the first elapsed/remaining tick immediately,
                // instead of waiting up to 250 ms for the heartbeat
                // to wake on its own.
                maybeStartPlaybackPush();
            }));
    }
    // AudioEngine-level: voice finished playing (natural end or
    // explicit stop). Map the voice id back to the owning cue and
    // push a "finished" state.
    if (m_audioEngine) {
        m_oscNotifyConnections.append(connect(m_audioEngine.get(),
            &audio::AudioEngine::voiceFinished, this,
            [this](audio::VoiceId id) {
                if (!m_workspace) return;
                // A finished voice may belong to any cue list, not just
                // the active one, so search them all.
                for (const auto &up : m_workspace->cueLists()) {
                    if (auto *ac = audioCueForVoice(up.get(), id)) {
                        pushOscNotify(
                            QStringLiteral("/quewi/notify/cue/state"),
                            { osc::Argument::s(ac->id().toString()),
                              osc::Argument::s(QStringLiteral("finished")),
                              osc::Argument::d(ac->number()) });
                        return;
                    }
                }
            }));
    }
    // GoEngine-level: duration-based + instant cues. Audio + Video
    // are NOT routed through this path — their engines have real
    // per-voice finished signals. See AudioEngine block above for
    // audio; the VideoEngine block below covers video.
    if (m_goEngine) {
        m_oscNotifyConnections.append(connect(m_goEngine.get(),
            &GoEngine::cueFinished, this, [this](cues::Cue *c) {
                if (!c) return;
                pushOscNotify(QStringLiteral("/quewi/notify/cue/state"),
                    { osc::Argument::s(c->id().toString()),
                      osc::Argument::s(QStringLiteral("finished")),
                      osc::Argument::d(c->number()) });
            }));
    }
    // VideoEngine-level: a video voice finished (natural end or stop).
    // Clear the owning cue's live-voice handle (so the Inspector scrubber
    // stops targeting a dead layer) and push the OSC "finished" state,
    // mirroring the audio path above.
    if (m_videoEngine) {
        m_oscNotifyConnections.append(connect(m_videoEngine.get(),
            &video::VideoEngine::voiceFinished, this,
            [this](video::VideoVoiceId id) {
                if (!m_workspace || id == 0) return;
                for (const auto &up : m_workspace->cueLists()) {
                    auto *list = up.get();
                    if (!list) continue;
                    for (int r = 0; r < list->cueCount(); ++r) {
                        auto *vc = qobject_cast<video::VisualCue *>(list->cueAt(r));
                        if (vc && vc->currentVoiceId() == id) {
                            vc->setCurrentVoiceId(0);
                            pushOscNotify(
                                QStringLiteral("/quewi/notify/cue/state"),
                                { osc::Argument::s(vc->id().toString()),
                                  osc::Argument::s(QStringLiteral("finished")),
                                  osc::Argument::d(vc->number()) });
                            return;
                        }
                    }
                }
            }));
    }

    // One-shot: tell subscribers the workspace just changed (e.g.,
    // file loaded) so they can re-query.
    pushOscNotify(QStringLiteral("/quewi/notify/workspace/changed"), {});
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
