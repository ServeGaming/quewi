#include "ui/AudioEditorWindow.h"

#include <QAction>
#include <QActionGroup>
#include <QAudioDevice>
#include <QAudioFormat>
#include <QBuffer>
#include <QCloseEvent>
#include <QCursor>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMediaDevices>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QScrollBar>
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

    // Initialise model from the cue's file (or empty if none)
    QString filePath = cue ? cue->filePath() : QString();
    m_model->initFromFile(filePath, 48000);

    connect(&m_playTimer, &QTimer::timeout, this, &AudioEditorWindow::onPlaybackTick);
    connect(m_renderer.get(), &audio::AudioEditorRenderer::progress, this, [this](int pct){
        statusBar()->showMessage(tr("Rendering… %1%").arg(pct));
    });

    buildToolbar();
    buildCentral();
    buildBottomPanel();

    statusBar()->showMessage(filePath.isEmpty() ? tr("New session") : QFileInfo(filePath).fileName());
}

AudioEditorWindow::~AudioEditorWindow() = default;

// ── Layout ────────────────────────────────────────────────────────────────────

void AudioEditorWindow::buildToolbar() {
    auto *tb = addToolBar(tr("Transport"));
    tb->setMovable(false);
    tb->setIconSize(QSize(18, 18));

    // Transport
    auto *playAct  = tb->addAction(tr("▶ Play"),  this, &AudioEditorWindow::onPlay);
    auto *stopAct  = tb->addAction(tr("■ Stop"),  this, &AudioEditorWindow::onStop);
    auto *loopBtn  = new QToolButton(tb);
    loopBtn->setText(tr("⟳ Loop"));
    loopBtn->setCheckable(true);
    connect(loopBtn, &QToolButton::toggled, this, &AudioEditorWindow::onLoopToggled);
    tb->addWidget(loopBtn);
    Q_UNUSED(playAct) Q_UNUSED(stopAct)

    tb->addSeparator();

    // Tool mode
    auto *toolGroup = new QActionGroup(this);
    auto *selAct = tb->addAction(tr("✥ Select"));
    auto *razAct = tb->addAction(tr("✂ Razor"));
    selAct->setCheckable(true); selAct->setChecked(true); selAct->setActionGroup(toolGroup);
    razAct->setCheckable(true); razAct->setActionGroup(toolGroup);
    connect(selAct, &QAction::triggered, this, [this]{ m_timeline->setTool(TimelineCanvas::Tool::Select); });
    connect(razAct, &QAction::triggered, this, [this]{ m_timeline->setTool(TimelineCanvas::Tool::Razor); });

    tb->addSeparator();

    // Zoom
    tb->addAction(tr("− Zoom Out"), this, &AudioEditorWindow::zoomOut);
    tb->addAction(tr("+ Zoom In"),  this, &AudioEditorWindow::zoomIn);
    tb->addAction(tr("Fit"),        this, &AudioEditorWindow::zoomFit);

    tb->addSeparator();

    // Tracks
    tb->addAction(tr("+ Track"), this, &AudioEditorWindow::addTrack);

    tb->addSeparator();

    // Bounce
    auto *bounceBtn = new QPushButton(tr("Render to File…"), tb);
    bounceBtn->setObjectName(QStringLiteral("goButton"));
    bounceBtn->setFixedHeight(28);
    connect(bounceBtn, &QPushButton::clicked, this, &AudioEditorWindow::bounceToFile);
    tb->addWidget(bounceBtn);

    // Undo/Redo
    tb->addSeparator();
    auto *undoAct = m_model->undoStack()->createUndoAction(this, tr("Undo"));
    undoAct->setShortcut(QKeySequence::Undo);
    auto *redoAct = m_model->undoStack()->createRedoAction(this, tr("Redo"));
    redoAct->setShortcut(QKeySequence::Redo);
    tb->addAction(undoAct);
    tb->addAction(redoAct);
}

void AudioEditorWindow::buildCentral() {
    auto *central = new QWidget(this);
    auto *vl = new QVBoxLayout(central);
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(0);

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

    vl->addWidget(timelineArea, 1);
    setCentralWidget(central);
}

void AudioEditorWindow::buildBottomPanel() {
    auto *tabs = new QTabWidget(this);
    tabs->setMaximumHeight(260);

    m_effectsRack = new EffectsRackWidget(tabs);
    tabs->addTab(m_effectsRack, tr("Effects"));

    m_spectrogram = new SpectrogramWidget(tabs);
    tabs->addTab(m_spectrogram, tr("Spectrogram"));

    // Attach via a dock-like bottom widget
    auto *central = centralWidget();
    auto *vl = qobject_cast<QVBoxLayout *>(central->layout());
    if (vl) vl->addWidget(tabs);

    // Select first track's effects by default
    if (m_model->trackCount() > 0)
        m_effectsRack->setTrack(m_model->track(0));
}

// ── Playback ──────────────────────────────────────────────────────────────────

void AudioEditorWindow::onPlay() {
    if (m_isPlaying) { stopPlayback(); return; }
    startPlayback();
}

void AudioEditorWindow::onStop() { stopPlayback(); }

void AudioEditorWindow::onLoopToggled(bool on) { m_looping = on; }

void AudioEditorWindow::startPlayback() {
    stopPlayback();

    // Render mix
    QProgressDialog prog(tr("Rendering mix…"), tr("Cancel"), 0, 100, this);
    prog.setMinimumDuration(400);
    bool ok = m_renderer->render(m_renderedPcm);
    prog.close();

    if (!ok) {
        statusBar()->showMessage(tr("Render failed: %1").arg(m_renderer->errorString()));
        return;
    }

    // 16-bit interleaved for QAudioSink
    std::vector<qint16> pcm16(m_renderedPcm.size());
    for (size_t i = 0; i < m_renderedPcm.size(); ++i)
        pcm16[i] = qint16(std::clamp(m_renderedPcm[i], -1.f, 1.f) * 32767.f);

    QByteArray bytes(reinterpret_cast<const char*>(pcm16.data()),
                     qsizetype(pcm16.size() * sizeof(qint16)));
    m_playBuffer.close();
    m_playBuffer.setData(bytes);
    m_playBuffer.open(QIODevice::ReadOnly);

    QAudioFormat fmt;
    fmt.setSampleRate(m_model->sampleRate());
    fmt.setChannelCount(2);
    fmt.setSampleFormat(QAudioFormat::Int16);

    const QAudioDevice dev = QMediaDevices::defaultAudioOutput();
    m_sink = std::make_unique<QAudioSink>(dev, fmt, this);
    m_sink->start(&m_playBuffer);

    m_sinkStartFrame  = 0;
    m_isPlaying       = true;
    m_playTimer.start(50); // 20 Hz cursor update
    statusBar()->showMessage(tr("Playing…"));
}

void AudioEditorWindow::stopPlayback() {
    m_playTimer.stop();
    if (m_sink) { m_sink->stop(); m_sink.reset(); }
    m_playBuffer.close();
    m_isPlaying = false;
    m_timeline->setPlayheadFrame(0);
    statusBar()->showMessage(tr("Stopped"));
}

void AudioEditorWindow::onPlaybackTick() {
    if (!m_sink || m_sink->state() == QAudio::StoppedState) {
        if (m_looping) { startPlayback(); return; }
        stopPlayback(); return;
    }
    // Estimate playback position from bytes processed
    qint64 bytesProcessed = m_sink->processedUSecs() * m_model->sampleRate() * 2 * 2 / 1000000;
    qint64 frame = m_sinkStartFrame + bytesProcessed / 4; // 4 bytes/frame (2ch * int16)
    m_timeline->setPlayheadFrame(frame);
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
    m_effectsRack->setTrack(t);
}

// ── Region / track selection ──────────────────────────────────────────────────

void AudioEditorWindow::onRegionSelected(QUuid regionId) {
    if (regionId.isNull()) { m_spectrogram->clear(); return; }

    // Find the region and update spectrogram
    for (int ti = 0; ti < m_model->trackCount(); ++ti) {
        for (const auto &r : m_model->track(ti)->regions()) {
            if (r.id == regionId && r.sourceFile) {
                m_spectrogram->setSource(r.sourceFile, r.srcInSamples, r.srcOutSamples);
                m_effectsRack->setTrack(m_model->track(ti));
                return;
            }
        }
    }
}

void AudioEditorWindow::onTrackSelected(int trackIndex) {
    if (auto *t = m_model->track(trackIndex))
        m_effectsRack->setTrack(t);
}

// ── Bounce ────────────────────────────────────────────────────────────────────

void AudioEditorWindow::bounceToFile() {
    QString path = QFileDialog::getSaveFileName(this, tr("Render to File"),
        m_cue ? QFileInfo(m_cue->filePath()).dir().absolutePath() : QString(),
        tr("WAV files (*.wav)"));
    if (path.isEmpty()) return;

    QProgressDialog prog(tr("Rendering…"), tr("Cancel"), 0, 100, this);
    prog.setMinimumDuration(0);
    prog.show();

    bool ok = m_renderer->renderToWav(path);
    prog.close();

    if (!ok) {
        QMessageBox::critical(this, tr("Render Failed"), m_renderer->errorString());
        return;
    }

    // Update the cue's file path so it plays the bounced file
    if (m_cue) {
        m_cue->setField(QStringLiteral("filePath"), path);
        setWindowTitle(tr("Audio Editor — %1").arg(m_cue->name()));
    }
    statusBar()->showMessage(tr("Rendered to %1").arg(QFileInfo(path).fileName()));
}

// ── Close ─────────────────────────────────────────────────────────────────────

bool AudioEditorWindow::promptSaveIfDirty() {
    if (!m_model->isDirty()) return true;
    auto btn = QMessageBox::question(this, tr("Unsaved Changes"),
        tr("The audio editor has unsaved changes. Bounce to file before closing?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (btn == QMessageBox::Cancel) return false;
    if (btn == QMessageBox::Save)   bounceToFile();
    return true;
}

void AudioEditorWindow::closeEvent(QCloseEvent *e) {
    stopPlayback();
    if (!promptSaveIfDirty()) { e->ignore(); return; }
    e->accept();
}

} // namespace quewi::ui
