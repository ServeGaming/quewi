#include "MainWindow.h"

#include "audio/AudioCue.h"
#include "audio/AudioEngine.h"
#include "core/CueList.h"
#include "core/CueListModel.h"
#include "core/UndoCommands.h"
#include "core/Workspace.h"
#include "cues/FadeCue.h"
#include "cues/MemoCue.h"
#include "lighting/LightCue.h"
#include "lighting/LightingEngine.h"
#include "osc/OscCue.h"
#include "osc/OscEngine.h"
#include "show/ShowFile.h"
#include "ui/CueListView.h"
#include "ui/Inspector.h"
#include "ui/OscMonitor.h"
#include "ui/PreferencesDialog.h"
#include "ui/TransportBar.h"

#include <QAction>
#include <QCloseEvent>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QKeySequence>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QSplitter>
#include <QStatusBar>
#include <QUndoStack>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

namespace quewi {

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    resize(1280, 800);
    setAcceptDrops(true);
    m_oscEngine = std::make_unique<osc::OscEngine>(this);
    connect(m_oscEngine.get(), &osc::OscEngine::sendError, this, [this](const QString &reason) {
        statusBar()->showMessage(tr("OSC: %1").arg(reason), 4000);
    });
    m_audioEngine = std::make_unique<audio::AudioEngine>(this);
    m_lightingEngine = std::make_unique<lighting::LightingEngine>(this);
    buildLayout();
    buildMenus();
    resetWorkspace();
    statusBar()->showMessage(tr("Ready"));
}

MainWindow::~MainWindow() = default;

void MainWindow::buildLayout()
{
    auto *central = new QWidget(this);
    auto *outer = new QVBoxLayout(central);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    m_mainSplitter = new QSplitter(Qt::Horizontal, central);

    m_cueListView = new ui::CueListView(m_mainSplitter);
    m_inspector   = new ui::Inspector(m_mainSplitter);

    m_mainSplitter->addWidget(m_cueListView);
    m_mainSplitter->addWidget(m_inspector);
    m_mainSplitter->setStretchFactor(0, 3);
    m_mainSplitter->setStretchFactor(1, 2);
    m_mainSplitter->setSizes({800, 480});

    m_transport = new ui::TransportBar(central);

    outer->addWidget(m_mainSplitter, 1);
    outer->addWidget(m_transport, 0);

    setCentralWidget(central);

    connect(m_cueListView, &ui::CueListView::currentCueChanged,
            m_inspector,   &ui::Inspector::setCue);
    connect(m_cueListView, &ui::CueListView::currentCueChanged,
            this, [this](cues::Cue *) { onSelectionChanged(); });
    connect(m_cueListView, &ui::CueListView::goRequested,
            this, &MainWindow::onGoRequested);
    connect(m_transport,   &ui::TransportBar::goPressed,
            this, &MainWindow::onGoRequested);
    connect(m_transport,   &ui::TransportBar::panicPressed, this, [this] {
        // Hard stop with a 50 ms fade so the speakers don't click.
        m_audioEngine->stopAll(0.05);
        m_lightingEngine->blackout();
        statusBar()->showMessage(tr("PANIC: all output stopped"), 2000);
    });
    connect(m_transport,   &ui::TransportBar::pausePressed, this, [this] {
        // No real pause yet — fade out everything as a Phase-3 stand-in.
        // True pause (sample-accurate resume) lands with the GoEngine.
        m_audioEngine->stopAll(0.25);
        statusBar()->showMessage(tr("Pause: faded out (proper pause arrives in Phase 6)"), 2500);
    });
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
    toolsMenu->addAction(tr("&OSC Monitor…"),
                         QKeySequence(QStringLiteral("Ctrl+1")),
                         this, &MainWindow::showOscMonitor);

    auto *cueMenu = menuBar()->addMenu(tr("&Cue"));
    cueMenu->addAction(tr("New &Memo"),       QKeySequence(Qt::Key_M), this, &MainWindow::insertMemoCue);
    cueMenu->addAction(tr("New &OSC"),        QKeySequence(Qt::Key_O), this, &MainWindow::insertOscCue);
    cueMenu->addAction(tr("New &Audio"),      QKeySequence(Qt::Key_A), this, &MainWindow::insertAudioCue);
    cueMenu->addAction(tr("New &Fade"),       QKeySequence(Qt::Key_F), this, &MainWindow::insertFadeCue);
    cueMenu->addAction(tr("New &Light"),      QKeySequence(Qt::Key_L), this, &MainWindow::insertLightCue);
    cueMenu->addAction(tr("New Light Fa&de"), QKeySequence(QStringLiteral("Shift+L")), this, &MainWindow::insertLightFadeCue);
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

void MainWindow::deleteSelectedCue()
{
    auto *list = m_workspace->activeCueList();
    if (!list) return;
    const auto idx = m_cueListView->currentIndex();
    if (!idx.isValid()) return;
    m_workspace->undoStack()->push(
        new core::RemoveCueCommand(list, idx.row()));
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

    // Fire by type. As more cue types come online they'll plug in here
    // (Phase 6 will replace this dispatch with a proper GoEngine).
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
        if (!file || file->state() != audio::AudioFile::State::Loaded) {
            statusBar()->showMessage(tr("GO: audio not ready (%1)")
                .arg(file ? tr("loading") : tr("no file")), 2000);
        } else {
            audio::VoiceParams p;
            p.gainDb = audioCue->gainDb();
            p.fadeInSeconds = audioCue->fadeInSeconds();
            p.fadeOutSeconds = audioCue->fadeOutSeconds();
            p.loop = audioCue->loop();
            const auto vid = m_audioEngine->fire(file, p);
            audioCue->setCurrentVoiceId(vid);
            statusBar()->showMessage(tr("GO: ▶ %1 (%2 s)")
                .arg(cue->name().isEmpty() ? cue->typeName() : cue->name(),
                     QString::number(file->durationSeconds(), 'f', 2)),
                2000);
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

    // Advance selection. Pre-load the (now) next audio cue eagerly so
    // its file is ready when GO fires next.
    const auto idx = m_cueListView->currentIndex();
    const int next = idx.isValid() ? idx.row() + 1 : 0;
    if (next < m_model->rowCount())
        m_cueListView->setCurrentIndex(m_model->index(next, 0));
    if (auto *upcoming = m_cueListView->nextCue()) {
        if (auto *ac = qobject_cast<audio::AudioCue *>(upcoming)) ac->prepare();
    }
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (maybeSaveChanges()) event->accept();
    else                    event->ignore();
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

int MainWindow::insertCuesFromUrls(const QList<QUrl> &urls)
{
    auto *list = m_workspace ? m_workspace->activeCueList() : nullptr;
    if (!list) return 0;

    const auto sel = m_cueListView->currentIndex();
    int insertRow = sel.isValid() ? sel.row() + 1 : list->cueCount();
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

    if (videoExts.contains(ext) || imageExts.contains(ext)) {
        // Phase 5 lands real video/image cues. Drop a Memo with a note
        // for now so the operator's intent isn't lost.
        auto cue = std::make_unique<cues::MemoCue>();
        cue->setField(QStringLiteral("name"), stem);
        cue->setField(QStringLiteral("notes"),
            tr("(%1 cue type lands in Phase 5; original path: %2)")
                .arg(videoExts.contains(ext) ? tr("Video") : tr("Image"), path));
        return cue;
    }

    return nullptr;
}

} // namespace quewi
