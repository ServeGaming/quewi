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
#include <QScrollBar>
#include <QSplitter>
#include <QStatusBar>
#include <QTabWidget>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

namespace quewi::ui {

namespace {

// 18×18 line icons painted into transparent QPixmaps. Cached on first
// use. White ink so QSS-tinted toolbar buttons pick up the parent
// stylesheet's text colour cleanly.
QIcon makeEditorIcon(const QString &name) {
    static QHash<QString, QIcon> cache;
    if (auto it = cache.constFind(name); it != cache.constEnd()) return it.value();

    const int sz = 18;
    QPixmap pm(sz, sz);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    QColor ink(0xe0, 0xe2, 0xeb);
    QPen pen(ink); pen.setWidthF(1.5); pen.setCapStyle(Qt::RoundCap); pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);

    if (name == QLatin1String("play")) {
        QPainterPath path;
        path.moveTo(5, 4); path.lineTo(14, 9); path.lineTo(5, 14); path.closeSubpath();
        p.fillPath(path, ink);
    } else if (name == QLatin1String("stop")) {
        p.fillRect(QRectF(5, 5, 8, 8), ink);
    } else if (name == QLatin1String("loop")) {
        p.drawArc(3, 4, 12, 10, 30 * 16, 300 * 16);
        QPainterPath arrow;
        arrow.moveTo(13.5, 3); arrow.lineTo(15.5, 5.5); arrow.lineTo(11.5, 6); arrow.closeSubpath();
        p.fillPath(arrow, ink);
    } else if (name == QLatin1String("select")) {
        // Arrow / pointer
        QPainterPath path;
        path.moveTo(4, 3); path.lineTo(4, 14); path.lineTo(7, 11);
        path.lineTo(9.5, 15); path.lineTo(11, 14.2);
        path.lineTo(8.6, 10.4); path.lineTo(13, 10); path.closeSubpath();
        p.fillPath(path, ink);
    } else if (name == QLatin1String("razor")) {
        // Razor blade
        p.drawLine(3, 14, 13, 4);
        p.drawRect(QRectF(11, 3, 4, 4));
    } else if (name == QLatin1String("zoomIn")) {
        p.drawEllipse(2, 2, 9, 9);
        p.drawLine(11, 11, 16, 16);
        p.drawLine(6.5, 4, 6.5, 9); // +
        p.drawLine(4, 6.5, 9, 6.5);
    } else if (name == QLatin1String("zoomOut")) {
        p.drawEllipse(2, 2, 9, 9);
        p.drawLine(11, 11, 16, 16);
        p.drawLine(4, 6.5, 9, 6.5); // -
    } else if (name == QLatin1String("zoomFit")) {
        // Brackets
        p.drawLine(3, 5, 3, 3); p.drawLine(3, 3, 5, 3);
        p.drawLine(13, 3, 15, 3); p.drawLine(15, 3, 15, 5);
        p.drawLine(3, 13, 3, 15); p.drawLine(3, 15, 5, 15);
        p.drawLine(13, 15, 15, 15); p.drawLine(15, 15, 15, 13);
        p.drawLine(6, 9, 12, 9);
    } else if (name == QLatin1String("addTrack")) {
        p.drawLine(3, 5, 15, 5);
        p.drawLine(3, 9, 9, 9);
        p.drawLine(3, 13, 9, 13);
        p.drawLine(13, 11, 13, 15); // +
        p.drawLine(11, 13, 15, 13);
    } else if (name == QLatin1String("undo")) {
        p.drawArc(3, 4, 12, 10, 60 * 16, -180 * 16);
        QPainterPath arrow;
        arrow.moveTo(3, 5); arrow.lineTo(5, 3); arrow.lineTo(7, 6); arrow.closeSubpath();
        p.fillPath(arrow, ink);
    } else if (name == QLatin1String("redo")) {
        p.drawArc(3, 4, 12, 10, 120 * 16, 180 * 16);
        QPainterPath arrow;
        arrow.moveTo(15, 5); arrow.lineTo(13, 3); arrow.lineTo(11, 6); arrow.closeSubpath();
        p.fillPath(arrow, ink);
    } else if (name == QLatin1String("render")) {
        // Down-arrow into tray
        p.drawLine(9, 3, 9, 11);
        QPainterPath arrow;
        arrow.moveTo(5, 8); arrow.lineTo(9, 13); arrow.lineTo(13, 8); arrow.closeSubpath();
        p.fillPath(arrow, ink);
        p.drawLine(3, 15, 15, 15);
    }
    p.end();
    QIcon icon(pm);
    cache.insert(name, icon);
    return cache.value(name);
}

// Vertical 1 px divider for the toolbar — a styled QFrame is heavier
// and renders blurry, so paint it as a thin widget.
QWidget *toolbarDivider(QWidget *parent) {
    auto *w = new QFrame(parent);
    w->setFrameShape(QFrame::VLine);
    w->setFixedWidth(1);
    w->setStyleSheet(QStringLiteral("color: #414752; background: #414752;"));
    return w;
}

QLabel *sectionLabel(const QString &text, QWidget *parent) {
    auto *l = new QLabel(text, parent);
    l->setStyleSheet(QStringLiteral(
        "color:#8a919e; font-size:10px; font-weight:700; letter-spacing:0.15em;"
        "padding:0 6px;"));
    return l;
}

} // namespace

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
                }
            };
            syncRate();
            connect(file.get(), &audio::AudioFile::stateChanged, this,
                    [syncRate](audio::AudioFile::State){ syncRate(); });
        }
    }

    connect(&m_playTimer, &QTimer::timeout, this, &AudioEditorWindow::onPlaybackTick);
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
    tb->setMovable(false);
    tb->setIconSize(QSize(18, 18));
    tb->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    tb->setStyleSheet(QStringLiteral(
        "QToolBar { background:#181c22; border:none; padding:4px 8px; spacing:2px; }"
        "QToolButton { color:#e0e2eb; padding:6px 10px; background:transparent; border:1px solid transparent; }"
        "QToolButton:hover { background:#272a30; border:1px solid #414752; }"
        "QToolButton:checked { background:#272a30; border:1px solid #4a9eff; color:#a4c9ff; }"
        "QToolButton:pressed { background:#1c2026; }"
    ));

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

    // ── TRACKS ────────────────────────────────────────────────────────
    tb->addWidget(sectionLabel(tr("TRACKS"), tb));
    tb->addAction(makeEditorIcon("addTrack"), tr("Add Track"), this, &AudioEditorWindow::addTrack);

    tb->addWidget(toolbarDivider(tb));

    // ── HISTORY ───────────────────────────────────────────────────────
    tb->addWidget(sectionLabel(tr("HISTORY"), tb));
    auto *undoAct = m_model->undoStack()->createUndoAction(this, tr("Undo"));
    undoAct->setIcon(makeEditorIcon("undo"));
    undoAct->setShortcut(QKeySequence::Undo);
    auto *redoAct = m_model->undoStack()->createRedoAction(this, tr("Redo"));
    redoAct->setIcon(makeEditorIcon("redo"));
    redoAct->setShortcut(QKeySequence::Redo);
    tb->addAction(undoAct);
    tb->addAction(redoAct);

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
    tb->addWidget(bounceBtn);
}

void AudioEditorWindow::buildCentral() {
    auto *central = new QWidget(this);
    auto *vl = new QVBoxLayout(central);
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(0);

    // ── Header strip ─────────────────────────────────────────────────
    // Cue identity left, format readout right. Sits between toolbar and
    // timeline so the operator always knows what they're editing.
    auto *header = new QWidget(central);
    header->setObjectName(QStringLiteral("editorHeader"));
    header->setStyleSheet(QStringLiteral(
        "QWidget#editorHeader { background:#101419; border-bottom:1px solid #262a38; }"
    ));
    header->setFixedHeight(56);
    auto *hl = new QHBoxLayout(header);
    hl->setContentsMargins(20, 8, 20, 8);
    hl->setSpacing(14);

    m_headerNumber = new QLabel(QStringLiteral("—"), header);
    m_headerNumber->setStyleSheet(QStringLiteral(
        "color:#a4c9ff; font-family:'Space Grotesk','JetBrains Mono',monospace;"
        "font-size:24px; font-weight:700; letter-spacing:-0.01em;"));
    m_headerNumber->setMinimumWidth(72);

    auto *nameStack = new QVBoxLayout();
    nameStack->setSpacing(0);
    nameStack->setContentsMargins(0, 0, 0, 0);
    auto *caps = new QLabel(tr("AUDIO CUE"), header);
    caps->setStyleSheet(QStringLiteral(
        "color:#8a919e; font-size:10px; font-weight:700; letter-spacing:0.18em;"));
    m_headerName = new QLabel(QStringLiteral("—"), header);
    m_headerName->setStyleSheet(QStringLiteral(
        "color:#e0e2eb; font-size:18px; font-weight:600; letter-spacing:-0.005em;"));
    nameStack->addWidget(caps);
    nameStack->addWidget(m_headerName);

    m_headerMeta = new QLabel(QString(), header);
    m_headerMeta->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_headerMeta->setStyleSheet(QStringLiteral(
        "color:#c0c7d4; font-family:'Space Grotesk','JetBrains Mono',monospace;"
        "font-size:12px; letter-spacing:0.04em;"));

    hl->addWidget(m_headerNumber, 0, Qt::AlignVCenter);
    hl->addLayout(nameStack, 1);
    hl->addWidget(m_headerMeta, 0, Qt::AlignVCenter);
    vl->addWidget(header, 0);
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

    vl->addWidget(timelineArea, 1);
    setCentralWidget(central);
}

void AudioEditorWindow::buildBottomPanel() {
    auto *bottom = new QWidget(this);
    bottom->setObjectName(QStringLiteral("editorBottomPanel"));
    bottom->setStyleSheet(QStringLiteral(
        "QWidget#editorBottomPanel { background:#181c22; border-top:1px solid #262a38; }"
    ));
    auto *bvl = new QVBoxLayout(bottom);
    bvl->setContentsMargins(0, 0, 0, 0);
    bvl->setSpacing(0);

    auto *tabs = new QTabWidget(bottom);
    tabs->setMinimumHeight(220);
    tabs->setMaximumHeight(320);
    tabs->setDocumentMode(true);

    m_effectsRack = new EffectsRackWidget(tabs);
    tabs->addTab(m_effectsRack, tr("EFFECTS"));

    m_spectrogram = new SpectrogramWidget(tabs);
    tabs->addTab(m_spectrogram, tr("SPECTROGRAM"));

    // Only run FFT while the spectrogram tab is active — otherwise selection
    // clicks are instant.
    connect(tabs, &QTabWidget::currentChanged, this, [this, tabs](int idx){
        m_spectrogram->setActive(tabs->widget(idx) == m_spectrogram);
    });
    m_spectrogram->setActive(false);

    bvl->addWidget(tabs);

    // Attach via a dock-like bottom widget
    auto *central = centralWidget();
    auto *vl = qobject_cast<QVBoxLayout *>(central->layout());
    if (vl) vl->addWidget(bottom);

    // Select first track's effects by default
    if (m_model->trackCount() > 0)
        m_effectsRack->setTrack(m_model->track(0));
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

void AudioEditorWindow::keyPressEvent(QKeyEvent *e) {
    // Consume Space locally so the main window's GO never fires while the
    // editor is focused. Space here toggles play/stop on the editor mix.
    if (e->key() == Qt::Key_Space && e->modifiers() == Qt::NoModifier) {
        onPlay();
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

} // namespace quewi::ui
