#include "ui/AudioEditorWindow.h"

#include "ui/LiveAudioScope.h"
#include "ui/LiveEffectDevice.h"
#include "ui/Theme.h"

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
    } else if (name == QLatin1String("waveform")) {
        // Symmetric amplitude bars around a centre line.
        static const int hgt[] = {3, 6, 9, 5, 8, 4, 7};
        for (int i = 0; i < 7; ++i) {
            const int x = 3 + i * 2;
            p.drawLine(x, 9 - hgt[i] / 2, x, 9 + hgt[i] / 2);
        }
    } else if (name == QLatin1String("spectrogram")) {
        // Stacked frequency bands.
        for (int i = 0; i < 4; ++i) {
            const int y = 4 + i * 3;
            QColor c = ink; c.setAlpha(80 + i * 50);
            p.setPen(QPen(c, 2, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(3, y, 15, y);
        }
    }
    p.end();
    QIcon icon(pm);
    cache.insert(name, icon);
    return cache.value(name);
}

// Vertical 1 px divider for the toolbar — a styled QFrame is heavier
// and renders blurry, so paint it as a thin widget. Colour pulled
// from the central theme so the toolbar divider matches every other
// divider in the app instead of drifting toward the old cool-grey
// palette this file used to ship with.
QWidget *toolbarDivider(QWidget *parent) {
    auto *w = new QFrame(parent);
    w->setFrameShape(QFrame::VLine);
    w->setFixedWidth(1);
    const auto &tk = ui::Theme::tokens();
    w->setStyleSheet(QStringLiteral("color:%1; background:%1;")
                         .arg(tk.outline.name()));
    return w;
}

QLabel *sectionLabel(const QString &text, QWidget *parent) {
    auto *l = new QLabel(text, parent);
    const auto &tk = ui::Theme::tokens();
    l->setStyleSheet(QStringLiteral(
        "color:%1; font-size:10px; font-weight:700; letter-spacing:0.15em;"
        "padding:0 6px;")
        .arg(tk.ink40.name()));
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
    // Stop the live preview before any track is freed — the LiveEffectDevice
    // reads the track's effects on the audio thread (m_sink->stop() is
    // synchronous, so the callback can't be mid-readData after this).
    connect(m_model.get(), &audio::AudioEditorModel::aboutToRemoveTrack,
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
    const auto &tk = Theme::tokens();
    tb->setStyleSheet(QStringLiteral(
        "QToolBar { background:%1; border:none; padding:4px 8px; spacing:2px; }"
        "QToolButton { color:%2; padding:6px 10px; background:transparent;"
                     " border:1px solid transparent; }"
        "QToolButton:hover { background:%3; border:1px solid %4; }"
        "QToolButton:checked { background:%3; border:1px solid %5; color:%5; }"
        "QToolButton:pressed { background:%6; }")
        .arg(tk.bgDeep.name(),
             tk.ink100.name(),
             tk.bgRowHover.name(),
             tk.outline.name(),
             tk.accent.name(),
             tk.bgPanel.name()));

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
    const auto &tkH = Theme::tokens();
    auto *header = new QWidget(central);
    header->setObjectName(QStringLiteral("editorHeader"));
    header->setStyleSheet(QStringLiteral(
        "QWidget#editorHeader { background:%1; border-bottom:1px solid %2; }")
        .arg(tkH.bgDeep.name(), tkH.divider.name()));
    header->setFixedHeight(56);
    auto *hl = new QHBoxLayout(header);
    hl->setContentsMargins(20, 8, 20, 8);
    hl->setSpacing(14);

    m_headerNumber = new QLabel(QStringLiteral("—"), header);
    m_headerNumber->setStyleSheet(QStringLiteral(
        "color:%1; font-family:'Space Grotesk','JetBrains Mono',monospace;"
        "font-size:24px; font-weight:700; letter-spacing:-0.01em;")
        .arg(tkH.accent.name()));
    m_headerNumber->setMinimumWidth(72);

    auto *nameStack = new QVBoxLayout();
    nameStack->setSpacing(0);
    nameStack->setContentsMargins(0, 0, 0, 0);
    auto *caps = new QLabel(tr("AUDIO CUE"), header);
    caps->setStyleSheet(QStringLiteral(
        "color:%1; font-size:10px; font-weight:700; letter-spacing:0.18em;")
        .arg(tkH.ink40.name()));
    m_headerName = new QLabel(QStringLiteral("—"), header);
    m_headerName->setStyleSheet(QStringLiteral(
        "color:%1; font-size:18px; font-weight:600; letter-spacing:-0.005em;")
        .arg(tkH.ink100.name()));
    nameStack->addWidget(caps);
    nameStack->addWidget(m_headerName);

    m_headerMeta = new QLabel(QString(), header);
    m_headerMeta->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_headerMeta->setStyleSheet(QStringLiteral(
        "color:%1; font-family:'Space Grotesk','JetBrains Mono',monospace;"
        "font-size:12px; letter-spacing:0.04em;")
        .arg(tkH.ink60.name()));

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
    connect(m_timeline, &TimelineCanvas::requestAddTrack, this, &AudioEditorWindow::addTrack);

    vl->addWidget(timelineArea, 1);
    setCentralWidget(central);
}

void AudioEditorWindow::buildBottomPanel() {
    // Bottom panel is now the effects rack only — the spectrogram moved
    // into the timeline (the View toggle), so there's no tab strip to
    // hide it behind. The rack is embedded directly on a flush dark
    // surface with a single thin top border.
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

    m_effectsRack = new EffectsRackWidget(bottom);
    bvl->addWidget(m_effectsRack);

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
    m_playTimer.start(33); // ~30 Hz — smooth playhead + live analyzer
    statusBar()->showMessage(tr("Playing…"));
}

void AudioEditorWindow::stopPlayback() {
    m_playTimer.stop();
    if (m_sink) { m_sink->stop(); m_sink.reset(); }
    if (m_liveDevice) { m_liveDevice->close(); m_liveDevice.reset(); }
    m_isPlaying = false;
    if (m_scope) m_scope->setInactive();
    // Hide the playhead while stopped; the blue edit cursor stays put to
    // show where the next GO will start from.
    m_timeline->setPlayheadFrame(-1);
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

    // Natural end of the mix. A pull QIODevice goes Idle (not Stopped) when
    // exhausted, so detect end by the heard position instead.
    const qint64 total = qint64(m_renderedPcm.size() / 2);
    if (total > 0 && frame >= total) {
        if (m_looping) { startPlayback(); return; }
        stopPlayback(); return;
    }

    m_timeline->setPlayheadFrame(frame);
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
    // The editable session is already saved into the cue (closeEvent does
    // that unconditionally), so this is purely about flattening to a file
    // the cue can *play* — the engine plays filePath, not the live model.
    auto btn = QMessageBox::question(this, tr("Bounce for Playback?"),
        tr("Your edits are saved with the cue. Bounce them to a file now so "
           "the cue plays the edited audio?"),
        QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
    if (btn == QMessageBox::Cancel) return false;
    if (btn == QMessageBox::Yes)    bounceToFile();
    m_model->markClean();
    return true;
}

void AudioEditorWindow::closeEvent(QCloseEvent *e) {
    stopPlayback();
    // Auto-save the editable multitrack session into the cue so regions,
    // tracks, gains and fades round-trip on reopen and across show save/load.
    // Independent of bouncing — the cue still plays its filePath.
    if (m_cue && m_model->isDirty())
        m_cue->setEditorModelJson(m_model->toJson());
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
