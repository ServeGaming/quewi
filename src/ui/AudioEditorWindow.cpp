#include "ui/AudioEditorWindow.h"
#include "ui/DeskTakeRecorder.h"

#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QListWidget>
#include "ui/EditorChrome.h"

#include "ui/LightTriggersPanel.h"
#include "ui/LiveAudioScope.h"
#include "ui/LiveEffectDevice.h"
#include "ui/Theme.h"

#include <QAction>
#include <QActionGroup>
#include <QAudioDevice>
#include <QAbstractSpinBox>
#include <QApplication>
#include <QAudioFormat>
#include <QBuffer>
#include <QCloseEvent>
#include <QCursor>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QLabel>
#include <QMediaDevices>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QProgressDialog>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QShortcut>
#include <QTextEdit>
#include <QLineEdit>
#include <QSplitter>
#include <QStatusBar>
#include <QTabWidget>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

namespace quewi::ui {

AudioEditorWindow::AudioEditorWindow(audio::AudioCue *cue, QWidget *parent)
    : QMainWindow(parent), m_cue(cue)
{
    setWindowTitle(cue ? tr("Audio Editor — %1").arg(cue->name()) : tr("Audio Editor"));
    resize(1280, 720);
    setAttribute(Qt::WA_DeleteOnClose);

    m_model    = std::make_unique<audio::AudioEditorModel>(this);
    m_renderer = std::make_unique<audio::AudioEditorRenderer>(m_model.get(), this);
    // Stop the live preview before any track is freed — the LiveEffectDevice
    // reads the track's effects on the audio thread (m_sink->stop() is
    // synchronous, so the callback can't be mid-readData after this).
    connect(m_model.get(), &audio::AudioEditorModel::aboutToRemoveTrack,
            this, [this] { if (m_isPlaying) stopPlayback(); });
    // Same guard for per-effect add/remove/reorder: the live preview iterates
    // the track's effect vector on the audio thread, so adding/removing/moving
    // an effect mid-playback would race it (cross-thread UAF / realloc). The
    // signal fires BEFORE the mutation and stopPlayback()'s m_sink->stop() is
    // synchronous, so the audio callback can't be in flight when it returns.
    connect(m_model.get(), &audio::AudioEditorModel::effectsAboutToChange,
            this, [this] { if (m_isPlaying) stopPlayback(); });

    // Restore the saved multitrack session if the cue carries one; otherwise
    // derive a fresh single-track session from the cue's file. This is what
    // makes region splits, gains, fades and multi-track layout survive
    // closing and reopening the editor (and a show save/load).
    QString filePath = cue ? cue->filePath() : QString();
    if (cue && !cue->editorModelJson().isEmpty())
        m_model->fromJson(cue->editorModelJson());
    else
        m_model->initFromFile(filePath, 48000);

    // Adopt the source file's sample rate once it finishes loading, so the
    // ruler/timeline measure time correctly and playback isn't sped-up.
    if (m_model->trackCount() > 0 && !m_model->track(0)->regions().empty()) {
        auto file = m_model->track(0)->regions().front().sourceFile;
        if (file) {
            auto syncRate = [this, file]() {
                if (file->state() == audio::AudioFile::State::Loaded
                    && file->sampleRate() > 0)
                {
                    m_model->setSampleRate(file->sampleRate());
                    statusBar()->showMessage(QFileInfo(file->path()).fileName());
                    updateHeader();
                    syncTriggersToCanvas();   // markers are in seconds
                    // The decode is async — when it finishes (typically well
                    // after the 150 ms zoom-fit timer below has already
                    // no-op'd on a zero-length model), fit the view to the
                    // now-known duration and repaint so the waveform/peaks
                    // actually appear. Without this the timeline sat at the
                    // default zoom showing only the first sliver of audio,
                    // which read as a blank/flat line on tracks with a quiet
                    // intro. One-shot so later edits don't fight the user's
                    // manual zoom.
                    if (m_timeline) {
                        if (!m_initialFitDone) { m_initialFitDone = true; zoomFit(); }
                        m_timeline->update();
                    }
                }
            };
            syncRate();
            connect(file.get(), &audio::AudioFile::stateChanged, this,
                    [syncRate](audio::AudioFile::State){ syncRate(); });
        }
    }

    connect(&m_playTimer, &QTimer::timeout, this, &AudioEditorWindow::onPlaybackTick);

    m_syncTimer.setSingleShot(true);
    m_syncTimer.setInterval(300);
    connect(&m_syncTimer, &QTimer::timeout, this, [this] { syncSessionToCue(); });
    connect(m_model.get(), &audio::AudioEditorModel::effectsEdited, this, [this] {
        m_syncTimer.start();
        if (playsOwnRender())
            statusBar()->showMessage(tr("This cue plays a render — Update Render to "
                                        "hear effect changes in the show."), 5000);
    });
    connect(m_renderer.get(), &audio::AudioEditorRenderer::progress, this, [this](int pct){
        statusBar()->showMessage(tr("Rendering… %1%").arg(pct));
    });

    buildToolbar();
    buildCentral();
    buildBottomPanel();

    statusBar()->showMessage(filePath.isEmpty() ? tr("New session") : QFileInfo(filePath).fileName());

    // Fit timeline to content once the view has a real size and the audio
    // file has loaded enough metadata for totalDurationSamples() to be
    // non-zero. Defer to the next event-loop tick.
    QTimer::singleShot(150, this, &AudioEditorWindow::zoomFit);
}

AudioEditorWindow::~AudioEditorWindow() = default;

// ── Layout ────────────────────────────────────────────────────────────────────

void AudioEditorWindow::buildToolbar() {
    auto *tb = addToolBar(tr("Editor"));
    styleEditorToolBar(tb);

    // ── TRANSPORT ─────────────────────────────────────────────────────
    tb->addWidget(sectionLabel(tr("TRANSPORT"), tb));
    tb->addAction(makeEditorIcon("play"), tr("Play"), this, &AudioEditorWindow::onPlay);
    tb->addAction(makeEditorIcon("stop"), tr("Stop"), this, &AudioEditorWindow::onStop);
    auto *loopBtn = new QToolButton(tb);
    loopBtn->setIcon(makeEditorIcon("loop"));
    loopBtn->setText(tr("Loop"));
    loopBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    loopBtn->setCheckable(true);
    connect(loopBtn, &QToolButton::toggled, this, &AudioEditorWindow::onLoopToggled);
    tb->addWidget(loopBtn);

    // Lighting triggers during the preview. Off by default: previewing a song
    // shouldn't drive the rig unless the operator is programming against it.
    m_sendTriggersBtn = new QToolButton(tb);
    m_sendTriggersBtn->setText(tr("Send while previewing"));
    m_sendTriggersBtn->setCheckable(true);
    m_sendTriggersBtn->setToolTip(tr("Send this song's lighting triggers as the editor's "
                                     "preview plays past them, so you can program the desk "
                                     "against the music"));
    connect(m_sendTriggersBtn, &QToolButton::toggled, this, [this](bool on) {
        if (!on) { endPreviewTriggers(); return; }
        if (m_isPlaying && m_sink)
            beginPreviewTriggers(m_sinkStartFrame
                                 + m_sink->processedUSecs() * m_model->sampleRate() / 1000000);
    });
    tb->addWidget(m_sendTriggersBtn);

    // Record from desk — the same toggle as the Lighting tab's.
    m_recordBtn = new QToolButton(tb);
    m_recordBtn->setObjectName(QStringLiteral("editorRecordFromDesk"));
    m_recordBtn->setText(tr("● Record from desk"));
    m_recordBtn->setCheckable(true);
    m_recordBtn->setToolTip(tr("Play the song and fire cues on the lighting desk: each becomes a lighting "
                               "trigger at that moment (you choose to keep them when the song stops). "
                               "Nothing is sent to the desk."));
    connect(m_recordBtn, &QToolButton::toggled, this, [this](bool on) {
        if (on) startRecordingFromDesk();
        else stopRecordingFromDesk(true);
    });
    tb->addWidget(m_recordBtn);

    tb->addWidget(toolbarDivider(tb));

    // ── TOOL ──────────────────────────────────────────────────────────
    tb->addWidget(sectionLabel(tr("TOOL"), tb));
    auto *toolGroup = new QActionGroup(this);
    auto *selAct = tb->addAction(makeEditorIcon("select"), tr("Select"));
    auto *razAct = tb->addAction(makeEditorIcon("razor"),  tr("Razor"));
    selAct->setCheckable(true); selAct->setChecked(true); selAct->setActionGroup(toolGroup);
    razAct->setCheckable(true); razAct->setActionGroup(toolGroup);
    connect(selAct, &QAction::triggered, this, [this]{ m_timeline->setTool(TimelineCanvas::Tool::Select); });
    connect(razAct, &QAction::triggered, this, [this]{ m_timeline->setTool(TimelineCanvas::Tool::Razor); });

    tb->addWidget(toolbarDivider(tb));

    // ── ZOOM ──────────────────────────────────────────────────────────
    tb->addWidget(sectionLabel(tr("ZOOM"), tb));
    tb->addAction(makeEditorIcon("zoomOut"), tr("Out"), this, &AudioEditorWindow::zoomOut);
    tb->addAction(makeEditorIcon("zoomIn"),  tr("In"),  this, &AudioEditorWindow::zoomIn);
    tb->addAction(makeEditorIcon("zoomFit"), tr("Fit"), this, &AudioEditorWindow::zoomFit);

    tb->addWidget(toolbarDivider(tb));

    // ── VIEW ──────────────────────────────────────────────────────────
    // Audacity-style toggle between the peak waveform and a whole-file
    // spectrogram. The spectrogram image is built lazily per source file.
    tb->addWidget(sectionLabel(tr("VIEW"), tb));
    auto *viewGroup = new QActionGroup(this);
    auto *waveAct = tb->addAction(makeEditorIcon("waveform"),    tr("Waveform"));
    auto *specAct = tb->addAction(makeEditorIcon("spectrogram"), tr("Spectrogram"));
    waveAct->setCheckable(true); waveAct->setChecked(true); waveAct->setActionGroup(viewGroup);
    specAct->setCheckable(true); specAct->setActionGroup(viewGroup);
    connect(waveAct, &QAction::triggered, this, [this]{
        m_timeline->setViewMode(TimelineCanvas::ViewMode::Waveform); });
    connect(specAct, &QAction::triggered, this, [this]{
        m_timeline->setViewMode(TimelineCanvas::ViewMode::Spectrogram); });

    tb->addWidget(toolbarDivider(tb));

    // ── TRACKS ────────────────────────────────────────────────────────
    tb->addWidget(sectionLabel(tr("TRACKS"), tb));
    tb->addAction(makeEditorIcon("addTrack"), tr("Add Track"), this, &AudioEditorWindow::addTrack);

    tb->addWidget(toolbarDivider(tb));

    // ── HISTORY ───────────────────────────────────────────────────────
    // Undo/redo drive the editor's OWN undo stack (separate from the show's).
    // Shortcuts are registered window-wide (WindowShortcut + addAction on the
    // window) so Ctrl+Z / Ctrl+Y / Ctrl+Shift+Z fire no matter which child
    // widget — timeline, scrollbars, toolbar — currently holds focus.
    tb->addWidget(sectionLabel(tr("HISTORY"), tb));
    auto *undoAct = m_model->undoStack()->createUndoAction(this, tr("Undo"));
    undoAct->setIcon(makeEditorIcon("undo"));
    undoAct->setShortcuts(QList<QKeySequence>{ QKeySequence::Undo });
    undoAct->setShortcutContext(Qt::WindowShortcut);
    auto *redoAct = m_model->undoStack()->createRedoAction(this, tr("Redo"));
    redoAct->setIcon(makeEditorIcon("redo"));
    redoAct->setShortcuts(QList<QKeySequence>{
        QKeySequence::Redo, QKeySequence(QStringLiteral("Ctrl+Shift+Z")) });
    redoAct->setShortcutContext(Qt::WindowShortcut);
    tb->addAction(undoAct);
    tb->addAction(redoAct);
    // Belt-and-suspenders: also own the actions at the window level so the
    // shortcuts stay live even if the toolbar is ever hidden.
    addAction(undoAct);
    addAction(redoAct);

    // Spacer pushes Render to the far right.
    auto *spacer = new QWidget(tb);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    tb->addWidget(spacer);

    // Render — accent-filled action button (shares GO button style).
    auto *bounceBtn = new QPushButton(makeEditorIcon("render"), tr("  Render to File"), tb);
    bounceBtn->setObjectName(QStringLiteral("goButton"));
    bounceBtn->setProperty("state", "ready");
    bounceBtn->setMinimumHeight(30);
    bounceBtn->setStyleSheet(QStringLiteral(
        "QPushButton#goButton { font-size:12px; min-height:30px; padding:4px 18px; }"
    ));
    connect(bounceBtn, &QPushButton::clicked, this, &AudioEditorWindow::bounceToFile);

    // Always asks for a new file, even when "Render" would update in place.
    auto *renderAsBtn = new QPushButton(tr("Render As…"), tb);
    renderAsBtn->setFlat(true);
    renderAsBtn->setCursor(Qt::PointingHandCursor);
    renderAsBtn->setToolTip(tr("Render to a new file of your choosing"));
    connect(renderAsBtn, &QPushButton::clicked, this, [this] { renderAs(); });
    tb->addWidget(renderAsBtn);
    tb->addWidget(bounceBtn);
    m_renderBtn = bounceBtn;
    updateRenderButton();
}

bool AudioEditorWindow::playsOwnRender() const
{
    const QString bounced = m_model->bouncedPath();
    return m_cue && audio::AudioCue::sameFile(bounced, m_cue->filePath())
        && QFileInfo::exists(bounced);
}

void AudioEditorWindow::updateRenderButton()
{
    if (!m_renderBtn) return;
    if (playsOwnRender()) {
        m_renderBtn->setText(tr("  Update Render"));
        m_renderBtn->setToolTip(tr("Re-render into %1, the file this cue plays — no save dialog")
                                    .arg(QFileInfo(m_model->bouncedPath()).fileName()));
    } else {
        m_renderBtn->setText(tr("  Render to File"));
        m_renderBtn->setToolTip(tr("Render the edit (with its effects) to a WAV the cue plays"));
    }
}

void AudioEditorWindow::syncSessionToCue()
{
    if (m_cue) m_cue->setEditorModelJson(m_model->toJson());
}

void AudioEditorWindow::buildCentral() {
    auto *central = new QWidget(this);
    auto *vl = new QVBoxLayout(central);
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(0);

    // ── Header strip ─────────────────────────────────────────────────
    const auto header = makeEditorHeader(tr("AUDIO CUE"), central);
    m_headerNumber = header.number;
    m_headerName   = header.name;
    m_headerMeta   = header.meta;
    vl->addWidget(header.widget, 0);
    updateHeader();

    // Timeline + scrollbars in a grid
    auto *timelineArea = new QWidget(central);
    auto *grid = new QGridLayout(timelineArea);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(0);

    m_timeline = new TimelineCanvas(m_model.get(), timelineArea);
    m_hbar = new QScrollBar(Qt::Horizontal, timelineArea);
    m_vbar = new QScrollBar(Qt::Vertical,   timelineArea);
    m_timeline->setScrollBars(m_hbar, m_vbar);

    grid->addWidget(m_timeline, 0, 0);
    grid->addWidget(m_vbar,     0, 1);
    grid->addWidget(m_hbar,     1, 0);

    connect(m_timeline, &TimelineCanvas::regionSelected, this, &AudioEditorWindow::onRegionSelected);
    connect(m_timeline, &TimelineCanvas::trackSelected,  this, &AudioEditorWindow::onTrackSelected);
    connect(m_timeline, &TimelineCanvas::requestAddTrack, this, &AudioEditorWindow::addTrack);

    vl->addWidget(timelineArea, 1);
    setCentralWidget(central);
}

void AudioEditorWindow::buildBottomPanel() {
    // Bottom panel: two tabs on a flush dark surface with a single thin top
    // border — the effects rack, and the song's lighting triggers. (The
    // spectrogram lives in the timeline, via the View toggle.)
    const auto &tkB = Theme::tokens();
    auto *bottom = new QWidget(this);
    bottom->setObjectName(QStringLiteral("editorBottomPanel"));
    bottom->setStyleSheet(QStringLiteral(
        "QWidget#editorBottomPanel {"
        "    background: %1;"
        "    border-top: 1px solid %2;"
        "}")
        .arg(tkB.bgDeep.name(), tkB.divider.name()));
    bottom->setMinimumHeight(240);
    auto *bvl = new QVBoxLayout(bottom);
    bvl->setContentsMargins(0, 0, 0, 0);
    bvl->setSpacing(0);

    m_bottomTabs = new QTabWidget(bottom);
    m_bottomTabs->setDocumentMode(true);
    m_effectsRack = new EffectsRackWidget(m_bottomTabs);
    m_bottomTabs->addTab(m_effectsRack, tr("Effects"));
    m_triggersPanel = new LightTriggersPanel(m_bottomTabs);
    m_triggersPanel->setCue(m_cue);
    m_bottomTabs->addTab(m_triggersPanel, tr("Lighting"));
    bvl->addWidget(m_bottomTabs);

    // Lighting: panel <-> marker lane. Every edit goes through the panel's
    // commit; the lane follows the cue (so undo and remotes show up too).
    connect(m_timeline, &TimelineCanvas::triggerAdded, this, [this](double s, double e) {
        showLightingTab();
        m_triggersPanel->addTrigger(s, e);
    });
    connect(m_timeline, &TimelineCanvas::triggerMoved, this, [this](QUuid id, double s, double e) {
        m_triggersPanel->moveTrigger(id, s, e);
    });
    connect(m_timeline, &TimelineCanvas::triggerClicked, this,
            [this](QUuid id, Qt::KeyboardModifiers mods) {
        showLightingTab();
        m_triggersPanel->clickTrigger(id, mods);
    });
    connect(m_timeline, &TimelineCanvas::triggersSpanSelected, this, [this](double a, double b) {
        showLightingTab();
        m_triggersPanel->selectSpan(a, b);
    });
    connect(m_timeline, &TimelineCanvas::triggersMovedBy, this, [this](QList<QUuid> ids, double d) {
        m_triggersPanel->moveTriggersBy(ids, d);
    });
    connect(m_timeline, &TimelineCanvas::triggerContextAction, this, [this](QUuid id, QString action) {
        showLightingTab();
        m_triggersPanel->applyTriggerAction(id, action);
    });
    connect(m_timeline, &TimelineCanvas::editCursorMoved, this, [this](qint64 frame) {
        m_triggersPanel->setCursorSeconds(double(frame) * secondsPerFrame());
    });
    connect(m_triggersPanel, &LightTriggersPanel::selectionSetChanged,
            m_timeline, &TimelineCanvas::setSelectedTriggers);
    connect(m_triggersPanel, &LightTriggersPanel::testRequested,
            this, &AudioEditorWindow::testTriggerRequested);
    connect(m_triggersPanel, &LightTriggersPanel::deskSettingsRequested,
            this, &AudioEditorWindow::lightingDeskSettingsRequested);
    m_triggersPanel->setCursorSeconds(double(m_timeline->editCursorFrame()) * secondsPerFrame());

    // Beat grid: snap follows the panel's checkbox; Tap reads the preview's
    // playhead; Detect analyses the dry mix (the one the preview plays).
    m_timeline->setSnapToBeats(m_triggersPanel->snapToBeats());
    connect(m_triggersPanel, &LightTriggersPanel::snapToBeatsChanged,
            m_timeline, &TimelineCanvas::setSnapToBeats);
    // Record from desk: the recorder notes each desk cue at the preview's
    // playhead (nothing while paused or stopped).
    m_recorder = new DeskTakeRecorder(this);
    m_recorder->setPlayhead([this] {
        const qint64 f = currentPlayFrame();
        return (f < 0 || m_paused) ? -1.0 : double(f) * secondsPerFrame();
    });
    connect(m_recorder, &DeskTakeRecorder::recorded, this, [this](const audio::RecordedCue &c) {
        const QString last = tr("LX %1 at %2").arg(c.cue, audio::DeskRecording::timeText(c.at));
        m_triggersPanel->setRecordingState(true, int(m_recorder->takes().size()), last);
        statusBar()->showMessage(tr("● Recorded %1").arg(last), 3000);
    });
    connect(m_triggersPanel, &LightTriggersPanel::recordToggled, this, [this](bool on) {
        if (on) startRecordingFromDesk();
        else stopRecordingFromDesk(true);
    });
    m_triggersPanel->setPlayheadProvider([this] {
        const qint64 f = currentPlayFrame();
        return f < 0 ? -1.0 : double(f) * secondsPerFrame();
    });
    m_triggersPanel->setDetectSource([this](std::vector<float> &stereo, int &rate) {
        rate = m_model->sampleRate();
        if (m_isPlaying && !m_renderedPcm.empty()) {   // already rendered, and current
            stereo = m_renderedPcm;
            return true;
        }
        QApplication::setOverrideCursor(Qt::WaitCursor);
        const bool ok = m_renderer->render(stereo, /*applyEffects=*/false);
        QApplication::restoreOverrideCursor();
        statusBar()->clearMessage();
        return ok;
    });
    // T taps the tempo anywhere in the editor except while typing. A widget
    // that takes text claims the key first (ShortcutOverride), so this only
    // fires when T isn't text.
    auto *tapKey = new QShortcut(QKeySequence(Qt::Key_T), this);
    tapKey->setContext(Qt::WindowShortcut);
    auto doTap = [this] {
        QWidget *fw = QApplication::focusWidget();
        if (qobject_cast<QLineEdit *>(fw) || qobject_cast<QAbstractSpinBox *>(fw)
            || qobject_cast<QTextEdit *>(fw) || qobject_cast<QPlainTextEdit *>(fw))
            return;
        m_triggersPanel->tap();
    };
    connect(tapKey, &QShortcut::activated, this, doTap);
    connect(tapKey, &QShortcut::activatedAmbiguously, this, doTap);

    // Space plays / pauses the editor mix, whatever has focus (EditorChrome).
    installEditorSpaceKey(this, [this] { togglePlayPause(); });
    auto typing = [] {
        QWidget *fw = QApplication::focusWidget();
        return qobject_cast<QLineEdit *>(fw) || qobject_cast<QAbstractSpinBox *>(fw)
            || qobject_cast<QTextEdit *>(fw) || qobject_cast<QPlainTextEdit *>(fw);
    };
    // M drops a lighting marker where the song is playing.
    auto *markKey = new QShortcut(QKeySequence(Qt::Key_M), this);
    markKey->setContext(Qt::WindowShortcut);
    auto doMark = [this, typing] {
        if (typing()) return;
        m_triggersPanel->addPointHere();
        showLightingTab();
    };
    connect(markKey, &QShortcut::activated, this, doMark);
    connect(markKey, &QShortcut::activatedAmbiguously, this, doMark);

    // Ctrl+G groups the selected lighting triggers; Delete removes them
    // (the list has its own Delete; this one covers the lane).
    auto *groupKey = new QShortcut(QKeySequence(QStringLiteral("Ctrl+G")), this);
    groupKey->setContext(Qt::WindowShortcut);
    connect(groupKey, &QShortcut::activated, this, [this, typing] {
        if (typing() || m_triggersPanel->selectedTrigger().isNull()) return;
        showLightingTab();
        m_triggersPanel->applyTriggerAction(m_triggersPanel->selectedTrigger(), QStringLiteral("group"));
    });
    auto *delTrig = new QShortcut(QKeySequence::Delete, m_timeline);
    delTrig->setContext(Qt::WidgetShortcut);
    connect(delTrig, &QShortcut::activated, this, [this] {
        if (!m_triggersPanel->selectedTriggers().isEmpty()) m_triggersPanel->deleteSelection();
    });

    if (m_cue)
        connect(m_cue, &cues::Cue::changed, this, &AudioEditorWindow::syncTriggersToCanvas);
    syncTriggersToCanvas();

    // The live analyzer is owned here and handed to the rack so any editor
    // it opens (EQ / Compressor) can subscribe to it.
    m_scope = new LiveAudioScope(this);
    m_effectsRack->setScope(m_scope);

    // Attach via a dock-like bottom widget
    auto *central = centralWidget();
    auto *vl = qobject_cast<QVBoxLayout *>(central->layout());
    if (vl) vl->addWidget(bottom);

    // Select first track's effects by default
    if (m_model->trackCount() > 0) {
        m_activeTrack = m_model->track(0);
        m_effectsRack->setTrack(m_activeTrack);
    }
    // Removing the active track nulls m_activeTrack (and the rack's track);
    // fall back to the first track so Play and the rack have one again.
    connect(m_model.get(), &audio::AudioEditorModel::tracksChanged, this, [this] {
        syncTriggersToCanvas();             // the song length may have changed
        if (m_activeTrack || m_model->trackCount() == 0) return;
        m_activeTrack = m_model->track(0);
        m_effectsRack->setTrack(m_activeTrack);
    });
}

void AudioEditorWindow::setTriggerSupport(core::Workspace *ws, QUndoStack *undo,
                                          std::function<QStringList()> midiPorts)
{
    m_triggersPanel->setWorkspace(ws);
    m_triggersPanel->setUndoStack(undo);
    m_triggersPanel->setMidiPortsProvider(std::move(midiPorts));
}

void AudioEditorWindow::refreshLightingDesk()
{
    if (m_triggersPanel) m_triggersPanel->refreshDesk();
}

void AudioEditorWindow::showLightingTab()
{
    if (m_bottomTabs) m_bottomTabs->setCurrentWidget(m_triggersPanel);
}

void AudioEditorWindow::flashTrigger(const QUuid &id)
{
    m_triggersPanel->flashTrigger(id);
    m_timeline->flashTrigger(id);
}

double AudioEditorWindow::secondsPerFrame() const
{
    return 1.0 / double(std::max(1, m_model->sampleRate()));
}

qint64 AudioEditorWindow::currentPlayFrame() const
{
    if (!m_isPlaying || !m_sink) return -1;
    return m_sinkStartFrame + m_sink->processedUSecs() * m_model->sampleRate() / 1000000;
}

void AudioEditorWindow::syncTriggersToCanvas()
{
    if (!m_timeline) return;
    m_timeline->setTriggers(m_cue ? m_cue->lightTriggers() : audio::LightTriggers{},
                            double(m_model->sampleRate()));
    m_timeline->setBeatGrid(m_cue ? m_cue->beatGrid() : audio::BeatGrid{});
    if (m_triggersPanel)
        m_triggersPanel->setSongLength(double(m_model->totalDurationSamples()) * secondsPerFrame());
}

// ── Preview triggers ("Send while previewing") ───────────────────────────────
// The preview plays the editor's mix from timeline frame 0, so its position in
// seconds is frame / sampleRate — the same clock as the markers in the lane.

void AudioEditorWindow::beginPreviewTriggers(qint64 frame)
{
    endPreviewTriggers();
    if (!m_cue || m_cue->lightTriggers().empty()) return;
    m_previewTracker = audio::TriggerTracker{};
    m_previewTracking = true;
    m_previewClock.start();
    sendPreviewEvents(m_previewTracker.begin(m_cue->lightTriggers(), double(frame) * secondsPerFrame()));
}

void AudioEditorWindow::endPreviewTriggers()
{
    if (!m_previewTracking) return;
    m_previewTracking = false;
    if (m_cue) sendPreviewEvents(m_previewTracker.stop(m_cue->lightTriggers()));
}

void AudioEditorWindow::sendPreviewEvents(const std::vector<audio::TriggerEvent> &events)
{
    if (!m_cue) return;
    const auto &triggers = m_cue->lightTriggers();
    for (const auto &ev : events) {
        if (ev.index < 0 || ev.index >= int(triggers.size())) continue;
        const auto &t = triggers[size_t(ev.index)];
        const auto &action = ev.exit ? t.exit : t.enter;
        // While recording from the desk nothing is sent: the desk's own cues
        // are what's being recorded, and an echo of ours would be caught too.
        if (!action.isNone() && !isRecordingFromDesk()) emit testTriggerRequested(action);
        flashTrigger(t.id);
    }
}

void AudioEditorWindow::updateHeader() {
    if (!m_headerNumber) return; // header not built yet
    if (!m_cue) {
        m_headerNumber->setText(QStringLiteral("—"));
        m_headerName->setText(QStringLiteral("—"));
        m_headerMeta->setText(QString());
        return;
    }
    m_headerNumber->setText(QString::number(m_cue->number(), 'f', 2));
    const auto name = m_cue->name().isEmpty() ? m_cue->typeName() : m_cue->name();
    m_headerName->setText(name);

    // Format readout from the first loaded source file, if any.
    QString meta;
    if (m_model->trackCount() > 0 && !m_model->track(0)->regions().empty()) {
        auto file = m_model->track(0)->regions().front().sourceFile;
        if (file && file->state() == audio::AudioFile::State::Loaded) {
            const double dur = file->durationSeconds();
            const int mins = int(dur) / 60;
            const int secs = int(dur) % 60;
            const int ms   = int(dur * 1000.0) % 1000;
            meta = QStringLiteral("%1 Hz   %2 ch   %3:%4.%5")
                       .arg(file->sampleRate())
                       .arg(file->channelCount())
                       .arg(mins)
                       .arg(secs, 2, 10, QChar('0'))
                       .arg(ms,  3, 10, QChar('0'));
        }
    }
    m_headerMeta->setText(meta);
}

// ── Playback ──────────────────────────────────────────────────────────────────

void AudioEditorWindow::onPlay() { togglePlayPause(); }

void AudioEditorWindow::togglePlayPause() {
    if (!m_isPlaying) { startPlayback(); return; }
    if (!m_sink) return;
    if (m_paused) {
        m_sink->resume();
        m_paused = false;
        m_playTimer.start(33);
        statusBar()->showMessage(tr("Playing…"));
    } else {
        // Pause where it is: the playhead stays put and Space carries on
        // from there (Stop goes back to the edit cursor).
        m_sink->suspend();
        m_paused = true;
        m_playTimer.stop();
        statusBar()->showMessage(tr("Paused — Space to carry on"));
    }
}

void AudioEditorWindow::onStop() { stopPlayback(); }

void AudioEditorWindow::onLoopToggled(bool on) { m_looping = on; }

void AudioEditorWindow::startPlayback() {
    m_restarting = true;              // a loop or restart isn't the end of a recording
    stopPlayback();
    m_restarting = false;

    // DRY render (no effects). The active track's effect chain is applied in
    // real time by LiveEffectDevice during playback, so EQ / compressor edits
    // and effect toggles are heard immediately without re-rendering.
    QProgressDialog prog(tr("Rendering mix…"), tr("Cancel"), 0, 100, this);
    prog.setMinimumDuration(400);
    bool ok = m_renderer->render(m_renderedPcm, /*applyEffects=*/false);
    prog.close();

    if (!ok) {
        statusBar()->showMessage(tr("Render failed: %1").arg(m_renderer->errorString()));
        return;
    }

    // Start from the edit cursor (Audacity-style: click sets where playback
    // begins). The render is the whole mix from timeline frame 0, so the
    // cursor frame maps directly to a sample offset. Clamp into range.
    const qint64 totalFrames = qint64(m_renderedPcm.size() / 2);
    const qint64 startFrame = std::clamp<qint64>(m_timeline->editCursorFrame(),
                                                 0, std::max<qint64>(0, totalFrames - 1));

    QAudioFormat fmt;
    fmt.setSampleRate(m_model->sampleRate());
    fmt.setChannelCount(2);
    fmt.setSampleFormat(QAudioFormat::Int16);

    const QAudioDevice dev = QMediaDevices::defaultAudioOutput();
    m_sink = std::make_unique<QAudioSink>(dev, fmt, this);

    if (!m_activeTrack && m_model->trackCount() > 0)
        m_activeTrack = m_model->track(0);
    m_liveDevice = std::make_unique<LiveEffectDevice>(this);
    m_liveDevice->start(&m_renderedPcm, m_activeTrack, m_model->sampleRate(), startFrame);
    m_sink->start(m_liveDevice.get());

    m_sinkStartFrame  = startFrame;
    m_isPlaying       = true;
    if (m_sendTriggersBtn && m_sendTriggersBtn->isChecked()) beginPreviewTriggers(startFrame);
    m_playTimer.start(33); // ~30 Hz — smooth playhead + live analyzer
    statusBar()->showMessage(tr("Playing…"));
}

void AudioEditorWindow::stopPlayback() {
    m_playTimer.stop();
    m_paused = false;
    endPreviewTriggers();   // ranges we were inside send their exit
    if (m_sink) { m_sink->stop(); m_sink.reset(); }
    if (m_liveDevice) { m_liveDevice->close(); m_liveDevice.reset(); }
    m_isPlaying = false;
    if (m_scope) m_scope->setInactive();
    // Hide the playhead while stopped; the blue edit cursor stays put to
    // show where the next GO will start from.
    m_timeline->setPlayheadFrame(-1);
    statusBar()->showMessage(tr("Stopped"));
    // The song stopped: so does recording from the desk — then the question.
    if (!m_restarting && isRecordingFromDesk()) {
        m_recorder->stop();
        syncRecordUi();
        QTimer::singleShot(0, this, &AudioEditorWindow::reviewRecording);
    }
}

void AudioEditorWindow::onPlaybackTick() {
    if (!m_sink || m_sink->state() == QAudio::StoppedState) {
        if (m_looping) { startPlayback(); return; }
        stopPlayback(); return;
    }
    // Estimate playback position from bytes processed
    qint64 bytesProcessed = m_sink->processedUSecs() * m_model->sampleRate() * 2 * 2 / 1000000;
    qint64 frame = m_sinkStartFrame + bytesProcessed / 4; // 4 bytes/frame (2ch * int16)

    // Natural end of the mix. A pull QIODevice goes Idle (not Stopped) when
    // exhausted, so detect end by the heard position instead.
    const qint64 total = qint64(m_renderedPcm.size() / 2);
    if (total > 0 && frame >= total) {
        if (m_looping) { startPlayback(); return; }
        stopPlayback(); return;
    }

    m_timeline->setPlayheadFrame(frame);
    if (m_previewTracking && m_cue) {
        const double wall = double(m_previewClock.restart()) / 1000.0;
        sendPreviewEvents(m_previewTracker.advance(m_cue->lightTriggers(),
                                                   double(frame) * secondsPerFrame(),
                                                   0.0, -1.0, wall));
    }
    // Feed the live analyzer the window at the current playback position so
    // any open EQ / Compressor editor shows the program in real time.
    if (m_scope) m_scope->analyze(m_renderedPcm, frame, m_model->sampleRate());
}

// ── Zoom ──────────────────────────────────────────────────────────────────────

void AudioEditorWindow::zoomIn()  { m_timeline->setFramesPerPixel(m_timeline->framesPerPixel() * 0.5); }
void AudioEditorWindow::zoomOut() { m_timeline->setFramesPerPixel(m_timeline->framesPerPixel() * 2.0); }
void AudioEditorWindow::zoomFit() {
    qint64 dur = m_model->totalDurationSamples();
    if (dur <= 0) return;
    int viewW = m_timeline->width() - TimelineCanvas::kHeaderWidth;
    if (viewW <= 0) return;
    m_timeline->setFramesPerPixel(double(dur) / double(viewW));
}

// ── Track management ──────────────────────────────────────────────────────────

void AudioEditorWindow::addTrack() {
    auto *t = m_model->addTrack(tr("Track %1").arg(m_model->trackCount() + 1));
    m_activeTrack = t;
    m_effectsRack->setTrack(t);
}

// ── Region / track selection ──────────────────────────────────────────────────

void AudioEditorWindow::onRegionSelected(QUuid regionId) {
    if (regionId.isNull()) return;

    // Bind the effects rack to the track that owns the selected region.
    for (int ti = 0; ti < m_model->trackCount(); ++ti) {
        for (const auto &r : m_model->track(ti)->regions()) {
            if (r.id == regionId && r.sourceFile) {
                m_activeTrack = m_model->track(ti);
                m_effectsRack->setTrack(m_activeTrack);
                return;
            }
        }
    }
}

void AudioEditorWindow::onTrackSelected(int trackIndex) {
    if (auto *t = m_model->track(trackIndex)) {
        m_activeTrack = t;
        m_effectsRack->setTrack(t);
    }
}

// ── Bounce ────────────────────────────────────────────────────────────────────

bool AudioEditorWindow::bounceToFile() {
    // Already playing our own render: update that file in place, no dialog.
    if (playsOwnRender()) return renderTo(m_model->bouncedPath());
    return renderAs();
}

bool AudioEditorWindow::renderAs() {
    const QString path = QFileDialog::getSaveFileName(this, tr("Render to File"),
        m_cue ? QFileInfo(m_cue->filePath()).dir().absolutePath() : QString(),
        tr("WAV files (*.wav)"));
    if (path.isEmpty()) return false;
    return renderTo(path);
}

bool AudioEditorWindow::renderTo(const QString &path) {
    stopPlayback();
    QProgressDialog prog(tr("Rendering…"), tr("Cancel"), 0, 100, this);
    prog.setMinimumDuration(0);
    prog.show();

    // Render beside the target and swap it in, so a failed render (or a
    // file another program holds open) never leaves a half-written WAV
    // where the cue's audio was.
    const QString tmp = path + QStringLiteral(".rendering");
    QFile::remove(tmp);
    bool ok = m_renderer->renderToWav(tmp);
    prog.close();
    QString error = ok ? QString() : m_renderer->errorString();
    if (ok && QFileInfo::exists(path) && !QFile::remove(path)) {
        ok = false;
        error = tr("Couldn't replace %1 — is it open in another program?")
                    .arg(QDir::toNativeSeparators(path));
    }
    if (ok && !QFile::rename(tmp, path)) {
        ok = false;
        error = tr("Couldn't write %1.").arg(QDir::toNativeSeparators(path));
    }
    if (!ok) {
        QFile::remove(tmp);
        QMessageBox::critical(this, tr("Render Failed"), error);
        return false;
    }

    // The cue now plays the render, which has the rack baked in; remember
    // that in the session so playback doesn't apply the rack a second time
    // and so the next render updates this same file.
    m_model->setBouncedPath(path);
    m_model->markClean();
    if (m_cue) {
        const bool samePath = audio::AudioCue::sameFile(m_cue->filePath(), path);
        m_cue->setField(QStringLiteral("filePath"), path);
        if (samePath) m_cue->reloadAudio();   // same name, new audio
        setWindowTitle(tr("Audio Editor — %1").arg(m_cue->name()));
    }
    syncSessionToCue();
    updateRenderButton();
    statusBar()->showMessage(tr("Rendered to %1").arg(QFileInfo(path).fileName()));
    return true;
}

// ── Close ─────────────────────────────────────────────────────────────────────

bool AudioEditorWindow::promptSaveIfDirty() {
    // The editable session is already saved into the cue (closeEvent does
    // that), so this is purely about the file the cue *plays*. Effects-only
    // changes need no render while the cue plays its original file — the
    // rack is applied live. But a cue playing a render has the OLD rack
    // baked in, so there they need one too.
    const bool ownRender = playsOwnRender();
    if (!m_model->isDirty() && !(ownRender && m_model->effectsDirty())) return true;

    QMessageBox::StandardButton btn;
    if (ownRender) {
        btn = QMessageBox::question(this, tr("Update Render?"),
            tr("This cue plays %1. Update it with your changes now?\n\n"
               "If you don't, your edits are still kept with the cue, but it "
               "keeps playing the previous render.")
                .arg(QFileInfo(m_model->bouncedPath()).fileName()),
            QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
    } else {
        btn = QMessageBox::question(this, tr("Bounce for Playback?"),
            tr("Your edits are saved with the cue. Bounce them to a file now so "
               "the cue plays the edited audio?"),
            QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
    }
    if (btn == QMessageBox::Cancel) return false;
    if (btn == QMessageBox::Yes && !bounceToFile()) return false;   // failed / cancelled: stay open
    m_model->markClean();
    return true;
}

void AudioEditorWindow::closeEvent(QCloseEvent *e) {
    // Recording: stop now and ask before the window goes.
    if (isRecordingFromDesk()) {
        m_recorder->stop();
        syncRecordUi();
        reviewRecording();
    }
    stopPlayback();
    // Auto-save the editable multitrack session into the cue so regions,
    // tracks, gains, fades AND the effects rack round-trip on reopen and
    // across show save/load. (Effects-only edits used to be dropped here.)
    m_syncTimer.stop();
    if (m_cue && (m_model->isDirty() || m_model->effectsDirty()))
        syncSessionToCue();
    if (!promptSaveIfDirty()) { e->ignore(); return; }
    e->accept();
}

bool AudioEditorWindow::event(QEvent *e)
{
    // Esc here means "stop the editor's preview". Panic is an application-
    // wide shortcut on Esc, so it grabbed the key first: stopping a preview
    // during a show killed every playing cue. Claiming the override delivers
    // Esc to keyPressEvent below instead. (The main window's Panic button and
    // Esc there are unaffected.)
    if (e->type() == QEvent::ShortcutOverride) {
        auto *ke = static_cast<QKeyEvent *>(e);
        if (ke->key() == Qt::Key_Escape && ke->modifiers() == Qt::NoModifier) {
            e->accept();
            return true;
        }
    }
    return QMainWindow::event(e);
}

void AudioEditorWindow::keyPressEvent(QKeyEvent *e) {
    // Consume Space locally so the main window's GO never fires while the
    // editor is focused. Space here toggles play/stop on the editor mix.
    if (e->key() == Qt::Key_Space && e->modifiers() == Qt::NoModifier) {
        togglePlayPause();
        e->accept();
        return;
    }
    if (e->key() == Qt::Key_Escape) {
        stopPlayback();
        e->accept();
        return;
    }
    QMainWindow::keyPressEvent(e);
}

// ── Record from desk ─────────────────────────────────────────────────────────

void AudioEditorWindow::setDeskFeedback(osc::EosFeedback *fb)
{
    if (m_recorder) m_recorder->setFeedback(fb);
}

bool AudioEditorWindow::isRecordingFromDesk() const
{
    return m_recorder && m_recorder->isRecording();
}

void AudioEditorWindow::syncRecordUi()
{
    const bool on = isRecordingFromDesk();
    if (m_recordBtn && m_recordBtn->isChecked() != on) {
        QSignalBlocker block(m_recordBtn);
        m_recordBtn->setChecked(on);
    }
    if (m_recordBtn) m_recordBtn->setText(on ? tr("■ Stop recording") : tr("● Record from desk"));
    if (m_triggersPanel)
        m_triggersPanel->setRecordingState(on, m_recorder ? int(m_recorder->takes().size()) : 0);
}

bool AudioEditorWindow::startRecordingFromDesk()
{
    if (!m_recorder || !m_cue) return false;
    const QString why = m_recorder->whyNot();
    if (!why.isEmpty()) {
        syncRecordUi();
        m_triggersPanel->setRecordMessage(why);
        statusBar()->showMessage(tr("Can't record from the desk: %1").arg(why), 8000);
        return false;
    }
    showLightingTab();
    if (!m_isPlaying) startPlayback();
    else if (m_paused) togglePlayPause();
    if (!m_isPlaying) {                       // the preview didn't start
        syncRecordUi();
        return false;
    }
    m_recorder->start();
    syncRecordUi();
    statusBar()->showMessage(tr("● Recording from the desk — fire cues as the song plays; "
                                "stopping the song stops recording"));
    return true;
}

void AudioEditorWindow::stopRecordingFromDesk(bool review)
{
    if (!isRecordingFromDesk()) { syncRecordUi(); return; }
    m_recorder->stop();
    syncRecordUi();
    if (review) reviewRecording();
}

void AudioEditorWindow::reviewRecording()
{
    if (!m_recorder || !m_cue) return;
    const auto takes = m_recorder->takes();
    m_recorder->clear();
    if (takes.empty()) {
        statusBar()->showMessage(tr("Nothing was recorded from the desk"), 4000);
        return;
    }
    const bool gridSet = m_cue->beatGrid().isSet();
    bool snap = false;
    bool keep = false;
    if (m_reviewer) {
        keep = m_reviewer(takes, gridSet, &snap);
    } else {
        QDialog d(this);
        d.setWindowTitle(tr("Recorded from the desk"));
        auto *v = new QVBoxLayout(&d);
        auto *intro = new QLabel(takes.size() == 1
                                     ? tr("1 cue was fired on the desk while the song played:")
                                     : tr("%1 cues were fired on the desk while the song played:").arg(takes.size()), &d);
        intro->setWordWrap(true);
        v->addWidget(intro);
        auto *list = new QListWidget(&d);
        for (const auto &t : takes) {
            QString line = QStringLiteral("%1     Go to cue %2").arg(audio::DeskRecording::timeText(t.at), t.cue);
            if (!t.list.isEmpty()) line += tr(" (list %1)").arg(t.list);
            if (!t.label.isEmpty()) line += QStringLiteral("     ") + t.label;
            list->addItem(line);
        }
        v->addWidget(list, 1);
        auto *snapBox = new QCheckBox(tr("Snap them to the beat grid"), &d);
        snapBox->setEnabled(gridSet);
        if (!gridSet) snapBox->setToolTip(tr("This song has no beat grid (TEMPO is Off)."));
        v->addWidget(snapBox);
        auto *note = new QLabel(tr("Kept, they become lighting triggers on this song, grouped as \"Recorded\" "
                                   "(one undo step). Played back, they send \"Go to cue\" to the desk."), &d);
        note->setWordWrap(true);
        v->addWidget(note);
        auto *bb = new QDialogButtonBox(&d);
        auto *keepBtn = bb->addButton(tr("Keep as lighting triggers"), QDialogButtonBox::AcceptRole);
        bb->addButton(tr("Discard"), QDialogButtonBox::RejectRole);
        keepBtn->setDefault(true);
        connect(bb, &QDialogButtonBox::accepted, &d, &QDialog::accept);
        connect(bb, &QDialogButtonBox::rejected, &d, &QDialog::reject);
        v->addWidget(bb);
        d.resize(520, 420);
        keep = d.exec() == QDialog::Accepted;
        snap = snapBox->isChecked();
    }
    if (!keep) {
        statusBar()->showMessage(tr("Discarded what was recorded from the desk"), 4000);
        return;
    }
    const auto grid = m_cue->beatGrid();
    const auto triggers = audio::DeskRecording::toTriggers(takes, {}, snap ? &grid : nullptr);
    const int n = m_triggersPanel->keepRecorded(triggers, tr("Recorded"));
    showLightingTab();
    statusBar()->showMessage(n == 1 ? tr("Kept 1 recorded trigger (Ctrl+Z undoes)")
                                    : tr("Kept %1 recorded triggers (Ctrl+Z undoes)").arg(n), 6000);
}

} // namespace quewi::ui
