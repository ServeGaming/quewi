#include "MainWindow.h"

#include "GoEngine.h"

#include "audio/AudioCue.h"
#include "audio/AudioEngine.h"
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
#include "osc/OscCue.h"
#include "osc/OscEngine.h"
#include "show/ShowFile.h"
#include "video/VideoCue.h"
#include "video/VideoEngine.h"
#include "ui/ActiveCuesPanel.h"
#include "ui/AudioEditorWindow.h"
#include "ui/CommandPalette.h"
#include "ui/FindReplaceDialog.h"
#include "ui/ShortcutManager.h"
#include "ui/ShortcutsDialog.h"
#include "ui/Theme.h"
#include "ui/CueListView.h"
#include "ui/PreflightDialog.h"
#include "ui/Inspector.h"
#include "ui/OscMonitor.h"
#include "ui/PreferencesDialog.h"
#include "ui/TransportBar.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QAudioDevice>
#include <QFileDialog>
#include <QFileInfo>
#include <QMediaDevices>
#include <QSettings>
#include <QKeySequence>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QDir>
#include <QInputDialog>
#include <QSplitter>
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
    m_midiEngine     = std::make_unique<midi::MidiEngine>(this);

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
    m_actGo->setShortcutContext(Qt::ApplicationShortcut);
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

    // Offer recovery after the main window is on screen.
    QTimer::singleShot(0, this, &MainWindow::recoverFromJournalIfPresent);
}

MainWindow::~MainWindow() = default;

void MainWindow::buildLayout()
{
    auto *central = new QWidget(this);
    auto *outer = new QVBoxLayout(central);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    m_listTabs = new QTabBar(central);
    m_listTabs->setObjectName(QStringLiteral("cueListTabs"));
    m_listTabs->setExpanding(false);
    m_listTabs->setDocumentMode(true);
    m_listTabs->setTabsClosable(false);
    m_listTabs->setMovable(false);
    connect(m_listTabs, &QTabBar::currentChanged, this, &MainWindow::onTabSelected);
    connect(m_listTabs, &QTabBar::tabBarDoubleClicked, this, [this](int){ renameCueListTab(); });

    m_mainSplitter = new QSplitter(Qt::Horizontal, central);

    m_cueListView = new ui::CueListView(m_mainSplitter);
    m_cueListView->setMinimumWidth(280);
    m_inspector   = new ui::Inspector(m_mainSplitter);

    m_mainSplitter->addWidget(m_cueListView);
    m_mainSplitter->addWidget(m_inspector);
    m_mainSplitter->setStretchFactor(0, 3);
    m_mainSplitter->setStretchFactor(1, 2);
    m_mainSplitter->setSizes({800, 480});
    m_mainSplitter->setChildrenCollapsible(false);

    m_activePanel = new ui::ActiveCuesPanel(central);
    m_activePanel->setAudioEngine(m_audioEngine.get());
    m_activePanel->hide(); // shown when something starts playing

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
    file->addSeparator();
    m_actSave = file->addAction(tr("&Save"), QKeySequence::Save, this, [this]{ saveShow(); });
    file->addAction(tr("Save &As…"), QKeySequence::SaveAs, this, [this]{ saveShowAs(); });
    file->addSeparator();
    file->addAction(tr("&Preferences…"), this, &MainWindow::showPreferences);
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
    auto applyTheme = [](const QString &name) {
        const auto qss = ui::Theme::load(name);
        if (!qss.isEmpty()) qApp->setStyleSheet(qss);
        QSettings s(QStringLiteral("ServeGaming"), QStringLiteral("quewi"));
        s.setValue(QStringLiteral("ui/theme"), name);
    };
    themeMenu->addAction(tr("&Dark"),  this, [applyTheme]{ applyTheme(QStringLiteral("quewi-dark")); });
    themeMenu->addAction(tr("&Light"), this, [applyTheme]{ applyTheme(QStringLiteral("quewi-light")); });

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
    cueMenu->addAction(tr("New Gr&oup"), QKeySequence(QStringLiteral("Ctrl+G")),        this, &MainWindow::insertGroupCue);
    cueMenu->addAction(tr("New &MIDI"),  QKeySequence(QStringLiteral("Shift+M")),       this, &MainWindow::insertMidiCue);
    cueMenu->addAction(tr("New M&SC"),   QKeySequence(QStringLiteral("Ctrl+Shift+M")),  this, &MainWindow::insertMscCue);
    cueMenu->addSeparator();
    cueMenu->addAction(tr("&Delete"), QKeySequence::Delete, this, &MainWindow::deleteSelectedCue);
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
    ui::PreferencesDialog dlg(m_audioEngine.get(), this);
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
    const auto idx = m_cueListView->currentIndex();
    if (!idx.isValid()) return;
    m_workspace->undoStack()->push(
        new core::RemoveCueCommand(list, idx.row()));
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

void MainWindow::toggleShowMode()
{
    m_showMode = m_actShowMode ? m_actShowMode->isChecked() : !m_showMode;
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
    setWindowTitle(QStringLiteral("%1%2 — quewi").arg(fileName, dirty));
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
