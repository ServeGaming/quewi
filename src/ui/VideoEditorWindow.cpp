#include "ui/VideoEditorWindow.h"

#include "audio/AudioCue.h"
#include "audio/Cuts.h"
#include "audio/Db.h"
#include "core/UndoCommands.h"
#include "ui/EditorChrome.h"
#include "ui/Theme.h"
#include "ui/VideoInspector.h"
#include "ui/VideoThumbnailer.h"
#include "video/PictureTiming.h"
#include "video/VideoCue.h"

#include <QAction>
#include <QActionGroup>
#include <QAudioOutput>
#include <QCheckBox>
#include <QCloseEvent>
#include <QDateTime>
#include <QDoubleSpinBox>
#include <QFontMetrics>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMediaMetaData>
#include <QMediaPlayer>
#include <QMenu>
#include <QPainter>
#include <QPushButton>
#include <QSplitter>
#include <QStatusBar>
#include <QToolBar>
#include <QToolButton>
#include <QUndoStack>
#include <QUrl>
#include <QVBoxLayout>
#include <QVideoFrame>
#include <QVideoSink>
#include <algorithm>
#include <cmath>

namespace quewi::ui {

namespace {

QString clock(double s)
{
    if (s < 0) s = 0;
    const int m = int(s / 60.0);
    return QStringLiteral("%1:%2").arg(m).arg(s - m * 60.0, 5, 'f', 2, QLatin1Char('0'));
}

QString seconds2(double s) { return QStringLiteral("%1 s").arg(s, 0, 'f', 2); }

// A cuts edit stands on its own in the history: two deletes are two undo
// steps, where field edits of the same name normally merge (a drag).
class CutsEditCommand : public core::EditCueFieldCommand {
public:
    using EditCueFieldCommand::EditCueFieldCommand;
    int id() const override { return -1; }
};

} // namespace

// The frame at the playhead, letterboxed, at the opacity the cue will show
// it with (its opacity × the picture fade at that point). Outside the trims
// or inside a cut it's dimmed and labelled, since the cue never shows that.
class VideoMonitor : public QWidget {
public:
    explicit VideoMonitor(QWidget *parent) : QWidget(parent)
    {
        setMinimumSize(320, 180);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }
    void setFrame(const QImage &img) { m_frame = img; update(); }
    void setLook(double opacity, const QString &note)
    {
        if (qFuzzyCompare(opacity + 1.0, m_opacity + 1.0) && note == m_note) return;
        m_opacity = opacity;
        m_note = note;
        update();
    }
    const QImage &frame() const { return m_frame; }
    double shownOpacity() const { return m_opacity; }

protected:
    void paintEvent(QPaintEvent *) override
    {
        const auto &tk = Theme::tokens();
        QPainter p(this);
        p.fillRect(rect(), tk.bgDeep);
        // The picture area: the frame's aspect, centred.
        QSizeF sz = m_frame.isNull() ? QSizeF(16, 9) : QSizeF(m_frame.size());
        sz.scale(QSizeF(size()) - QSizeF(24, 24), Qt::KeepAspectRatio);
        const QRectF r(QPointF((width() - sz.width()) / 2, (height() - sz.height()) / 2), sz);
        p.fillRect(r, tk.bgInverse);
        if (!m_frame.isNull()) {
            p.setRenderHint(QPainter::SmoothPixmapTransform);
            p.setOpacity(m_opacity);
            p.drawImage(r, m_frame);
            p.setOpacity(1.0);
        }
        if (!m_note.isEmpty()) {
            QFont f = font();
            f.setPixelSize(11);
            f.setBold(true);
            f.setLetterSpacing(QFont::PercentageSpacing, 112);
            p.setFont(f);
            const QRect badge = QFontMetrics(f).boundingRect(m_note).adjusted(-8, -4, 8, 4);
            const QRect at(int(r.left()) + 10, int(r.top()) + 10, badge.width(), badge.height());
            p.fillRect(at, QColor(0, 0, 0, 170));
            p.setPen(tk.ink100);
            p.drawText(at, Qt::AlignCenter, m_note);
        }
        p.setPen(tk.divider);
        p.drawRect(r.adjusted(0, 0, -1, -1));
    }

private:
    QImage  m_frame;
    double  m_opacity = 1.0;
    QString m_note;
};

VideoEditorWindow::VideoEditorWindow(video::VideoCue *cue, QUndoStack *undo, QWidget *parent)
    : QMainWindow(parent), m_cue(cue), m_undo(undo)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(tr("Video Editor"));
    setMinimumSize(1100, 660);
    resize(1280, 820);

    m_player = new QMediaPlayer(this);
    m_sink = new QVideoSink(this);
    m_audio = new QAudioOutput(this);
    m_player->setVideoSink(m_sink);
    m_player->setAudioOutput(m_audio);
    connect(m_sink, &QVideoSink::videoFrameChanged, this, &VideoEditorWindow::onFrame);
    connect(m_player, &QMediaPlayer::durationChanged, this, [this](qint64 ms) {
        m_duration = ms / 1000.0;
        m_timeline->setDuration(m_duration);
        refreshFromCue();
    });
    connect(m_player, &QMediaPlayer::metaDataChanged, this, [this] {
        const auto md = m_player->metaData();
        const double fps = md.value(QMediaMetaData::VideoFrameRate).toDouble();
        if (fps > 1.0 && fps < 1000.0) m_fps = fps;
        m_timeline->setFrameRate(m_fps);
        const QSize res = md.value(QMediaMetaData::Resolution).toSize();
        if (res.isValid()) m_resolution = QStringLiteral("%1×%2").arg(res.width()).arg(res.height());
        updateHeader();
    });
    connect(m_player, &QMediaPlayer::playbackStateChanged, this, [this] { updatePlayButton(); });
    connect(m_player, &QMediaPlayer::errorOccurred, this, [this](QMediaPlayer::Error, const QString &s) {
        statusBar()->showMessage(tr("Can't play this file: %1").arg(s));
    });

    m_tick.setInterval(30);
    connect(&m_tick, &QTimer::timeout, this, &VideoEditorWindow::onTick);

    m_thumbs = new VideoThumbnailer(this);

    buildToolbar();
    buildCentral();
    // Space plays / pauses whatever has focus (EditorChrome); open on the
    // timeline so the arrows step frames instead of nudging the In box.
    installEditorSpaceKey(this, [this] { togglePlay(); });
    m_timeline->setFocusPolicy(Qt::StrongFocus);
    m_timeline->setFocus();
    m_timeline->setFrameRate(m_fps);

    connect(m_thumbs, &VideoThumbnailer::thumbnailReady, m_timeline, &VideoTimeline::setThumbnail);
    connect(m_thumbs, &VideoThumbnailer::failed, this, [this](const QString &why) {
        m_timeline->setPictureNote(why.isEmpty() ? tr("No thumbnails") : why);
    });

    if (m_cue) {
        connect(m_cue, &cues::Cue::changed, this, &VideoEditorWindow::refreshFromCue);
        // The cue deleted (or its list closed): nothing left to edit.
        connect(m_cue, &QObject::destroyed, this, &QWidget::close);
        const QString path = m_cue->filePath();
        if (!path.isEmpty()) {
            m_player->setSource(QUrl::fromLocalFile(path));
            m_player->pause();   // shows the first frame; paused seeks show theirs
            m_timeline->setPictureNote(tr("Reading frames…"));
            m_thumbs->start(path);
        } else {
            m_timeline->setPictureNote(tr("This cue has no file"));
        }
        auto *sound = m_cue->sound();
        sound->prepare();   // decode for the waveform (shared with the cue)
        m_timeline->setAudioFile(sound->audioFile());
        seekTo(m_cue->trimInSeconds());
    }
    refreshFromCue();
    updateSpliceActions();
    statusBar()->showMessage(tr("Space play · I / O set In / Out · drag to select a section, "
                                "Delete cuts it out · B razor, V select · ← → step a frame"));
}

VideoEditorWindow::~VideoEditorWindow()
{
    m_tick.stop();
    if (m_thumbs) m_thumbs->stop();
}

video::VideoCue *VideoEditorWindow::cue() const { return m_cue; }

bool VideoEditorWindow::isPlaying() const
{
    return m_player && m_player->playbackState() == QMediaPlayer::PlayingState;
}

double VideoEditorWindow::endSeconds() const
{
    if (!m_cue) return m_duration;
    return m_cue->pictureTiming().endSeconds(m_duration);
}

double VideoEditorWindow::playsForSeconds() const
{
    if (!m_cue) return 0.0;
    return m_cue->pictureTiming().playSeconds(m_duration);
}

// ── Layout ─────────────────────────────────────────────────────────────────

void VideoEditorWindow::buildToolbar()
{
    auto *tb = addToolBar(tr("Video Editor"));
    styleEditorToolBar(tb);
    auto iconOnly = [tb](QAction *a) {
        if (auto *b = qobject_cast<QToolButton *>(tb->widgetForAction(a)))
            b->setToolButtonStyle(Qt::ToolButtonIconOnly);
    };

    tb->addWidget(sectionLabel(tr("TOOL"), tb));
    auto *tools = new QActionGroup(this);
    tools->setExclusive(true);
    m_selectAct = tb->addAction(makeEditorIcon("select"), tr("Select"), this,
                                [this] { setTool(VideoTimeline::Tool::Select); });
    m_selectAct->setCheckable(true);
    m_selectAct->setChecked(true);
    m_selectAct->setToolTip(tr("Select (V): click to seek, drag to select a section"));
    m_razorAct = tb->addAction(makeEditorIcon("razor"), tr("Razor"), this,
                               [this] { setTool(VideoTimeline::Tool::Razor); });
    m_razorAct->setCheckable(true);
    m_razorAct->setToolTip(tr("Razor (B): click to split; then click a segment to select it"));
    tools->addAction(m_selectAct);
    tools->addAction(m_razorAct);

    tb->addWidget(toolbarDivider(tb));
    tb->addWidget(sectionLabel(tr("MARK"), tb));
    auto *setIn = tb->addAction(tr("In"), this, &VideoEditorWindow::setInAtPlayhead);
    setIn->setToolTip(tr("Start the cue here (I)"));
    auto *setOut = tb->addAction(tr("Out"), this, &VideoEditorWindow::setOutAtPlayhead);
    setOut->setToolTip(tr("End the cue here (O)"));
    auto *clear = tb->addAction(tr("Clear"), this, &VideoEditorWindow::clearTrims);
    clear->setToolTip(tr("Play the whole file again"));

    tb->addWidget(toolbarDivider(tb));
    tb->addWidget(sectionLabel(tr("SPLICE"), tb));
    m_deleteAct = tb->addAction(makeEditorIcon("cut"), tr("Delete section"), this,
                                &VideoEditorWindow::deleteSelection);
    m_deleteAct->setToolTip(tr("Cut the selected section out of the cue (Delete). The timeline "
                               "keeps showing the file's own time, with the cut drawn over it."));
    m_keepAct = tb->addAction(makeEditorIcon("keep"), tr("Keep only this"), this,
                              &VideoEditorWindow::keepOnlySelection);
    m_keepAct->setToolTip(tr("Set In and Out around the selected section"));
    m_restoreAllAct = new QAction(tr("Restore all cuts"), this);
    connect(m_restoreAllAct, &QAction::triggered, this, &VideoEditorWindow::restoreAllCuts);
    m_clearSplitsAct = new QAction(tr("Clear split points"), this);
    connect(m_clearSplitsAct, &QAction::triggered, this, [this] { m_timeline->clearSplitPoints(); });

    tb->addWidget(toolbarDivider(tb));
    tb->addWidget(sectionLabel(tr("ZOOM"), tb));
    auto *zOut = tb->addAction(makeEditorIcon("zoomOut"), tr("Zoom out"), this, [this] { m_timeline->zoomOut(); });
    auto *zIn  = tb->addAction(makeEditorIcon("zoomIn"),  tr("Zoom in"),  this, [this] { m_timeline->zoomIn(); });
    auto *zFit = tb->addAction(makeEditorIcon("zoomFit"), tr("Fit"),      this, [this] { m_timeline->zoomFit(); });
    zOut->setToolTip(tr("Zoom out (wheel on the timeline)"));
    zIn->setToolTip(tr("Zoom in around the playhead"));
    zFit->setToolTip(tr("Fit the whole file (double-click the ruler)"));
    for (QAction *a : {zOut, zIn, zFit}) iconOnly(a);

    if (m_undo) {
        tb->addWidget(toolbarDivider(tb));
        tb->addWidget(sectionLabel(tr("HISTORY"), tb));
        // The show's undo stack: these edits are cue edits like any other.
        auto *undoAct = m_undo->createUndoAction(this, tr("Undo"));
        undoAct->setIcon(makeEditorIcon("undo"));
        undoAct->setShortcuts(QKeySequence::Undo);
        undoAct->setShortcutContext(Qt::WindowShortcut);
        auto *redoAct = m_undo->createRedoAction(this, tr("Redo"));
        redoAct->setIcon(makeEditorIcon("redo"));
        redoAct->setShortcuts({QKeySequence::Redo, QKeySequence(QStringLiteral("Ctrl+Shift+Z"))});
        redoAct->setShortcutContext(Qt::WindowShortcut);
        // Short labels: the show's stack names every edit ("Undo Edit
        // pictureFadeInSeconds"), which pushed the buttons off the toolbar.
        // The full name goes in the tooltip.
        for (QAction *a : {undoAct, redoAct}) {
            const QString label = a == undoAct ? tr("Undo") : tr("Redo");
            a->setText(label);
            connect(a, &QAction::changed, this, [a, label] {
                if (a->text() == label) return;
                a->setToolTip(a->text());
                a->setText(label);
            });
        }
        tb->addAction(undoAct);
        tb->addAction(redoAct);
        addAction(undoAct);
        addAction(redoAct);
        iconOnly(undoAct);
        iconOnly(redoAct);
    }
}

QWidget *VideoEditorWindow::buildViewer(QWidget *parent)
{
    const auto &tk = Theme::tokens();
    auto *viewer = new QWidget(parent);
    auto *vl = new QVBoxLayout(viewer);
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(0);

    m_monitor = new VideoMonitor(viewer);
    vl->addWidget(m_monitor, 1);

    // The transport strip under the picture: timecode, the buttons, how
    // long the cue plays for.
    auto *bar = new QWidget(viewer);
    bar->setObjectName(QStringLiteral("videoTransport"));
    bar->setFixedHeight(42);
    bar->setStyleSheet(QStringLiteral(
        "QWidget#videoTransport { background:%1; border-top:1px solid %2; }"
        "QWidget#videoTransport QToolButton { background:transparent; border:1px solid transparent;"
        "  border-radius:3px; padding:3px; }"
        "QWidget#videoTransport QToolButton:hover { background:%3; border-color:%4; }"
        "QWidget#videoTransport QToolButton:checked { background:%3; border-color:%5; }")
        .arg(tk.bgPanel.name(), tk.divider.name(), tk.bgRowHover.name(), tk.outline.name(),
             tk.accent.name()));
    auto *hl = new QHBoxLayout(bar);
    hl->setContentsMargins(16, 0, 16, 0);
    hl->setSpacing(2);

    m_timeLabel = new QLabel(bar);
    m_timeLabel->setStyleSheet(QStringLiteral(
        "color:%1; font-family:'Space Grotesk','JetBrains Mono',monospace; font-size:17px;"
        " font-weight:600; letter-spacing:0.02em;").arg(tk.ink100.name()));
    m_timeLabel->setMinimumWidth(QFontMetrics(m_timeLabel->font()).horizontalAdvance(QStringLiteral("00:00.00")) + 24);
    m_lengthLabel = new QLabel(bar);
    m_lengthLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_lengthLabel->setStyleSheet(QStringLiteral(
        "color:%1; font-family:'Space Grotesk','JetBrains Mono',monospace; font-size:12px;"
        " letter-spacing:0.04em;").arg(tk.ink60.name()));
    m_lengthLabel->setMinimumWidth(m_timeLabel->minimumWidth());

    auto button = [bar](QAction *a) {
        auto *b = new QToolButton(bar);
        b->setDefaultAction(a);
        b->setToolButtonStyle(Qt::ToolButtonIconOnly);
        b->setIconSize(QSize(18, 18));
        b->setFixedSize(30, 28);
        b->setFocusPolicy(Qt::NoFocus);
        b->setAutoRaise(true);
        return b;
    };
    auto *toIn = new QAction(makeEditorIcon("toIn"), tr("To In"), this);
    toIn->setToolTip(tr("Go to the In point (Home)"));
    connect(toIn, &QAction::triggered, this, [this] { if (m_cue) seekTo(m_cue->trimInSeconds()); });
    auto *back = new QAction(makeEditorIcon("frameBack"), tr("Frame back"), this);
    back->setToolTip(tr("A frame back (←, Shift: a second)"));
    connect(back, &QAction::triggered, this, [this] { stepFrames(-1); });
    m_playAct = new QAction(makeEditorIcon("play"), tr("Play"), this);
    m_playAct->setToolTip(tr("Play / pause (Space)"));
    connect(m_playAct, &QAction::triggered, this, &VideoEditorWindow::togglePlay);
    auto *fwd = new QAction(makeEditorIcon("frameForward"), tr("Frame on"), this);
    fwd->setToolTip(tr("A frame on (→, Shift: a second)"));
    connect(fwd, &QAction::triggered, this, [this] { stepFrames(1); });
    auto *toOut = new QAction(makeEditorIcon("toOut"), tr("To Out"), this);
    toOut->setToolTip(tr("Go to the Out point (End)"));
    connect(toOut, &QAction::triggered, this, [this] { seekTo(std::max(0.0, endSeconds() - 1.0 / m_fps)); });
    m_loopAct = new QAction(makeEditorIcon("loop"), tr("Loop"), this);
    m_loopAct->setCheckable(true);
    m_loopAct->setToolTip(tr("Loop the preview between In and Out"));
    m_hearAct = new QAction(makeEditorIcon("waveform"), tr("Sound"), this);
    m_hearAct->setCheckable(true);
    m_hearAct->setChecked(true);
    m_hearAct->setToolTip(tr("Hear the soundtrack while previewing (at the cue's level and fades)"));
    connect(m_hearAct, &QAction::toggled, this, [this] { applyPreviewLevel(); });

    hl->addWidget(m_timeLabel);
    hl->addStretch(1);
    for (QAction *a : {toIn, back, m_playAct, fwd, toOut}) hl->addWidget(button(a));
    hl->addSpacing(10);
    hl->addWidget(button(m_loopAct));
    hl->addWidget(button(m_hearAct));
    hl->addStretch(1);
    hl->addWidget(m_lengthLabel);
    vl->addWidget(bar);
    return viewer;
}

void VideoEditorWindow::buildCentral()
{
    auto *central = new QWidget(this);
    auto *vl = new QVBoxLayout(central);
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(0);

    const auto header = makeEditorHeader(tr("VIDEO CUE"), central);
    m_headerNumber = header.number;
    m_headerName   = header.name;
    m_headerMeta   = header.meta;
    vl->addWidget(header.widget);

    // Viewer and inspector over the timeline; the split between them moves.
    auto *split = new QSplitter(Qt::Vertical, central);
    split->setChildrenCollapsible(false);
    split->setHandleWidth(6);

    auto *top = new QWidget(split);
    auto *hl = new QHBoxLayout(top);
    hl->setContentsMargins(0, 0, 0, 0);
    hl->setSpacing(0);
    hl->addWidget(buildViewer(top), 1);
    m_inspector = new VideoInspector(top);
    hl->addWidget(m_inspector);
    split->addWidget(top);

    m_timeline = new VideoTimeline(split);
    split->addWidget(m_timeline);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 2);
    split->setSizes({470, 230});
    vl->addWidget(split, 1);

    connect(m_timeline, &VideoTimeline::seekRequested, this, &VideoEditorWindow::seekTo);
    connect(m_timeline, &VideoTimeline::trimInChanged, this, [this](double s) {
        edit(QStringLiteral("trimInSeconds"), s);
        if (!isPlaying()) seekTo(s);    // show the new first frame
    });
    connect(m_timeline, &VideoTimeline::trimOutChanged, this, [this](double s) {
        edit(QStringLiteral("trimOutSeconds"), s);
        if (!isPlaying()) seekTo(std::max(0.0, endSeconds() - 1.0 / m_fps));
    });
    connect(m_timeline, &VideoTimeline::pictureFadeInChanged, this,
            [this](double s) { edit(QStringLiteral("pictureFadeInSeconds"), s); });
    connect(m_timeline, &VideoTimeline::pictureFadeOutChanged, this,
            [this](double s) { edit(QStringLiteral("pictureFadeOutSeconds"), s); });
    connect(m_timeline, &VideoTimeline::soundFadeInChanged, this,
            [this](double s) { edit(QStringLiteral("sound.fadeInSeconds"), s); });
    connect(m_timeline, &VideoTimeline::soundFadeOutChanged, this,
            [this](double s) { edit(QStringLiteral("sound.fadeOutSeconds"), s); });
    connect(m_timeline, &VideoTimeline::soundMuteToggled, this,
            [this](bool muted) { edit(QStringLiteral("soundEnabled"), !muted); });
    connect(m_timeline, &VideoTimeline::selectionChanged, this, &VideoEditorWindow::updateSpliceActions);
    connect(m_timeline, &VideoTimeline::splitPointsChanged, this, &VideoEditorWindow::updateSpliceActions);
    connect(m_timeline, &VideoTimeline::toolChanged, this, [this](VideoTimeline::Tool t) {
        (t == VideoTimeline::Tool::Razor ? m_razorAct : m_selectAct)->setChecked(true);
    });
    connect(m_timeline, &VideoTimeline::contextMenuRequested, this, &VideoEditorWindow::showTimelineMenu);

    wireInspector();
    setCentralWidget(central);
}

void VideoEditorWindow::wireInspector()
{
    auto *ins = m_inspector;
    auto spinEdit = [this](QDoubleSpinBox *spin, const QString &field) {
        connect(spin, &QDoubleSpinBox::valueChanged, this, [this, field](double v) {
            if (!m_loading) edit(field, v);
        });
    };
    connect(ins->inSpin, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        if (m_loading || !m_cue) return;
        // Keep In before Out.
        v = std::min(v, std::max(0.0, endSeconds() - 0.05));
        edit(QStringLiteral("trimInSeconds"), v);
        if (!isPlaying()) seekTo(v);
    });
    connect(ins->outSpin, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        if (m_loading || !m_cue) return;
        if (v > 0.0) v = std::max(v, m_cue->trimInSeconds() + 0.05);
        if (m_duration > 0.0 && v >= m_duration) v = 0.0;
        edit(QStringLiteral("trimOutSeconds"), v);
    });
    spinEdit(ins->picFadeInSpin,  QStringLiteral("pictureFadeInSeconds"));
    spinEdit(ins->picFadeOutSpin, QStringLiteral("pictureFadeOutSeconds"));
    spinEdit(ins->opacitySpin,    QStringLiteral("opacity"));
    spinEdit(ins->levelSpin,      QStringLiteral("sound.gainDb"));
    spinEdit(ins->sndFadeInSpin,  QStringLiteral("sound.fadeInSeconds"));
    spinEdit(ins->sndFadeOutSpin, QStringLiteral("sound.fadeOutSeconds"));
    connect(ins->loopCheck, &QCheckBox::toggled, this, [this](bool on) {
        if (!m_loading) edit(QStringLiteral("loop"), on);
    });
    connect(ins->soundCheck, &QCheckBox::toggled, this, [this](bool on) {
        if (!m_loading) edit(QStringLiteral("soundEnabled"), on);
    });
    connect(ins->editSoundBtn, &QPushButton::clicked, this, [this] {
        if (m_cue) emit editSoundRequested(m_cue->sound());
    });
    connect(ins->lightingBtn, &QPushButton::clicked, this, [this] {
        if (m_cue) emit lightingRequested(m_cue->sound());
    });
}

// ── Cue ↔ view ─────────────────────────────────────────────────────────────

void VideoEditorWindow::edit(const QString &field, const QVariant &value)
{
    if (!m_cue) return;
    const QVariant old = m_cue->field(field);
    if (old == value) return;
    if (m_undo)
        m_undo->push(new core::EditCueFieldCommand(m_cue, field, old, value));
    else
        m_cue->setField(field, value);
}

void VideoEditorWindow::editCuts(const audio::Cuts &cuts, const QString &what)
{
    if (!m_cue || cuts == m_cue->cuts()) return;
    const QVariant old = m_cue->field(QStringLiteral("cuts"));
    const QVariant value = audio::cutsToJson(cuts);
    if (m_undo) {
        auto *cmd = new CutsEditCommand(m_cue, QStringLiteral("cuts"), old, value);
        cmd->setText(what);
        m_undo->push(cmd);
    } else {
        m_cue->setField(QStringLiteral("cuts"), value);
    }
}

void VideoEditorWindow::refreshFromCue()
{
    if (!m_cue) return;
    m_loading = true;
    auto *ins = m_inspector;
    auto *sound = m_cue->sound();
    const double in = m_cue->trimInSeconds(), out = m_cue->trimOutSeconds();
    m_timeline->setTrims(in, out);
    m_timeline->setPictureFades(m_cue->pictureFadeInSeconds(), m_cue->pictureFadeOutSeconds());
    m_timeline->setSoundFades(sound->fadeInSeconds(), sound->fadeOutSeconds());
    m_timeline->setSoundEnabled(m_cue->soundEnabled());
    m_timeline->setCuts(m_cue->cuts());
    QList<double> marks;
    for (const auto &t : sound->lightTriggers()) marks << t.start;
    m_timeline->setTriggerMarks(marks);

    auto set = [](QDoubleSpinBox *s, double v) {
        if (!qFuzzyCompare(s->value() + 1.0, v + 1.0)) s->setValue(v);
    };
    if (m_duration > 0.0) {
        ins->inSpin->setMaximum(m_duration);
        ins->outSpin->setMaximum(m_duration);
    }
    set(ins->inSpin, in);
    set(ins->outSpin, out);
    set(ins->picFadeInSpin, m_cue->pictureFadeInSeconds());
    set(ins->picFadeOutSpin, m_cue->pictureFadeOutSeconds());
    set(ins->opacitySpin, m_cue->opacity());
    set(ins->levelSpin, sound->gainDb());
    set(ins->sndFadeInSpin, sound->fadeInSeconds());
    set(ins->sndFadeOutSpin, sound->fadeOutSeconds());
    ins->loopCheck->setChecked(m_cue->loop());
    ins->soundCheck->setChecked(m_cue->soundEnabled());
    for (QWidget *w : {static_cast<QWidget *>(ins->levelSpin), static_cast<QWidget *>(ins->sndFadeInSpin),
                       static_cast<QWidget *>(ins->sndFadeOutSpin), static_cast<QWidget *>(ins->editSoundBtn)})
        w->setEnabled(m_cue->soundEnabled());

    // The cuts, and what they take off.
    const auto &cuts = m_cue->cuts();
    const double gone = audio::cutSecondsBetween(cuts, in, endSeconds() > 0 ? endSeconds() : 1e9);
    ins->clipSection->form()->setRowVisible(ins->cutsLabel, !cuts.empty());
    ins->cutsLabel->setText(cuts.size() == 1 ? tr("1 section · −%1").arg(seconds2(gone))
                                             : tr("%1 sections · −%2").arg(cuts.size()).arg(seconds2(gone)));
    ins->audioSection->setNote(m_cue->soundEnabled()
                                   ? QStringLiteral("%1 dB").arg(sound->gainDb(), 0, 'f', 1)
                                   : tr("off"));
    ins->videoSection->setNote(QStringLiteral("%1 %").arg(int(std::lround(m_cue->opacity() * 100))));
    const int triggers = int(sound->lightTriggers().size());
    ins->lightingSection->setNote(triggers ? QString::number(triggers) : QString());
    ins->triggersLabel->setText(triggers == 0 ? tr("None")
                                : triggers == 1 ? tr("1 trigger") : tr("%1 triggers").arg(triggers));
    m_loading = false;

    updateHeader();
    updateTimeReadout();
    updateSpliceActions();
    applyPreviewLevel();
}

void VideoEditorWindow::updateHeader()
{
    if (!m_headerNumber) return;
    if (!m_cue) {
        m_headerNumber->setText(QStringLiteral("—"));
        m_headerName->setText(tr("(cue deleted)"));
        m_headerMeta->clear();
        return;
    }
    m_headerNumber->setText(QString::number(m_cue->number()));
    const QString name = !m_cue->name().isEmpty() ? m_cue->name()
                                                  : QFileInfo(m_cue->filePath()).fileName();
    m_headerName->setText(name.isEmpty() ? tr("(untitled)") : name);
    setWindowTitle(tr("Video Editor — %1").arg(m_headerName->text()));
    QStringList meta;
    if (!m_resolution.isEmpty()) meta << m_resolution;
    meta << tr("%1 fps").arg(m_fps, 0, 'f', m_fps == std::floor(m_fps) ? 0 : 2);
    if (m_duration > 0.0) meta << clock(m_duration);
    m_headerMeta->setText(meta.join(QStringLiteral("  ·  ")));
}

void VideoEditorWindow::updateTimeReadout()
{
    if (!m_timeLabel || !m_cue) return;
    m_timeLabel->setText(clock(m_playhead));
    const double plays = playsForSeconds();
    m_lengthLabel->setText(tr("Plays %1").arg(clock(plays)));
    m_inspector->playsForLabel->setText(clock(plays));
    m_inspector->clipSection->setNote(clock(plays));

    // The monitor shows the frame as the cue would: its opacity × the fade.
    const auto timing = m_cue->pictureTiming();
    const double in = m_cue->trimInSeconds(), end = endSeconds();
    QString note;
    double look = m_cue->opacity() * timing.envelope(m_playhead, m_duration, true);
    if (m_playhead < in - 1e-3) { note = tr("BEFORE IN"); look = 0.35; }
    else if (end > 0.0 && m_playhead > end + 1e-3) { note = tr("AFTER OUT"); look = 0.35; }
    else if (const auto *c = audio::cutAt(timing.cuts, m_playhead)) {
        note = tr("CUT  −%1").arg(seconds2(c->length()));
        look = 0.35;
    }
    m_monitor->setLook(look, note);
}

void VideoEditorWindow::updatePlayButton()
{
    if (!m_playAct) return;
    const bool playing = isPlaying();
    m_playAct->setIcon(makeEditorIcon(playing ? "pause" : "play"));
    m_playAct->setText(playing ? tr("Pause") : tr("Play"));
}

void VideoEditorWindow::updateSpliceActions()
{
    const bool sel = m_timeline->hasSelection();
    m_deleteAct->setEnabled(sel);
    m_keepAct->setEnabled(sel);
    m_restoreAllAct->setEnabled(m_cue && !m_cue->cuts().empty());
    m_clearSplitsAct->setEnabled(!m_timeline->splitPoints().isEmpty());
}

void VideoEditorWindow::applyPreviewLevel()
{
    if (!m_audio || !m_cue) return;
    auto *sound = m_cue->sound();
    const bool hear = m_hearAct && m_hearAct->isChecked() && m_cue->soundEnabled();
    m_audio->setMuted(!hear);
    if (!hear) return;
    // The sound's own fades, over the same In / Out as the picture.
    video::PictureTiming t = m_cue->pictureTiming();
    t.fadeInSeconds = sound->fadeInSeconds();
    t.fadeOutSeconds = sound->fadeOutSeconds();
    const double gain = audio::dbToLinear(sound->gainDb()) * t.envelope(m_playhead, m_duration, true);
    m_audio->setVolume(float(std::clamp(gain, 0.0, 1.0)));
}

// ── Transport ──────────────────────────────────────────────────────────────

void VideoEditorWindow::seekTo(double seconds)
{
    // On a frame: In / Out set from the playhead land exactly on one.
    seconds = std::round(seconds * m_fps) / m_fps;
    if (m_duration > 0.0) seconds = std::clamp(seconds, 0.0, m_duration);
    m_playhead = std::max(0.0, seconds);
    if (m_player) {
        m_player->setPosition(qint64(m_playhead * 1000.0));
        // Until the player reports somewhere near here, its position is
        // the old one — don't let a tick drag the playhead back.
        m_pendingSeek = m_playhead;
        m_pendingSince = QDateTime::currentMSecsSinceEpoch();
    }
    m_timeline->setPlayhead(m_playhead);
    updateTimeReadout();
    applyPreviewLevel();
}

void VideoEditorWindow::togglePlay()
{
    if (!m_player || !m_cue) return;
    if (isPlaying()) {
        m_player->pause();
        m_tick.stop();
        return;
    }
    // From the playhead — or from In when it's outside what the cue plays;
    // never from inside a cut.
    const double in = m_cue->trimInSeconds(), end = endSeconds();
    if (m_playhead < in - 1e-3 || (end > 0.0 && m_playhead >= end - 0.02)) seekTo(in);
    const double past = m_cue->pictureTiming().skipCuts(m_playhead);
    if (past > m_playhead) seekTo(past);
    m_player->play();
    m_tick.start();
}

void VideoEditorWindow::onTick()
{
    if (!m_player || !m_cue) return;
    if (!isPlaying()) { m_tick.stop(); return; }
    const double pos = m_player->position() / 1000.0;
    if (m_pendingSeek >= 0.0) {
        // Landed: at the target or just past it — never the old position
        // still playing towards it (a jump over a cut must not show the cut).
        const bool landed = (pos >= m_pendingSeek - 0.05 && pos <= m_pendingSeek + 0.6)
                            || QDateTime::currentMSecsSinceEpoch() - m_pendingSince > 1500;
        if (!landed) return;
        m_pendingSeek = -1.0;
    }
    m_playhead = pos;
    const auto timing = m_cue->pictureTiming();
    // Into a cut: jump to its end, as the show will.
    const double past = timing.skipCuts(m_playhead);
    if (past > m_playhead) { seekTo(past); return; }
    const double end = endSeconds();
    if (end > 0.0 && m_playhead >= end) {
        if (m_loopAct->isChecked() || m_cue->loop()) {
            seekTo(timing.skipCuts(m_cue->trimInSeconds()));
            return;
        }
        m_player->pause();
        m_tick.stop();
        m_playhead = end;
    }
    m_timeline->setPlayhead(m_playhead);
    updateTimeReadout();
    applyPreviewLevel();
}

void VideoEditorWindow::onFrame(const QVideoFrame &frame)
{
    if (!frame.isValid()) return;
    m_monitor->setFrame(frame.toImage());
}

void VideoEditorWindow::setInAtPlayhead()
{
    if (!m_cue) return;
    const double end = endSeconds();
    // Past the Out point: In here, and the Out goes back to the end.
    if (end > 0.0 && m_playhead >= end - 0.05) edit(QStringLiteral("trimOutSeconds"), 0.0);
    edit(QStringLiteral("trimInSeconds"), m_playhead);
    statusBar()->showMessage(tr("In at %1").arg(clock(m_playhead)), 3000);
}

void VideoEditorWindow::setOutAtPlayhead()
{
    if (!m_cue) return;
    // Before the In point: Out here, and the In goes back to the top.
    if (m_playhead <= m_cue->trimInSeconds() + 0.05) edit(QStringLiteral("trimInSeconds"), 0.0);
    const double out = (m_duration > 0.0 && m_playhead >= m_duration - 0.005) ? 0.0 : m_playhead;
    edit(QStringLiteral("trimOutSeconds"), out);
    statusBar()->showMessage(tr("Out at %1").arg(clock(m_playhead)), 3000);
}

void VideoEditorWindow::clearTrims()
{
    edit(QStringLiteral("trimInSeconds"), 0.0);
    edit(QStringLiteral("trimOutSeconds"), 0.0);
}

void VideoEditorWindow::stepFrames(int frames)
{
    if (isPlaying()) { m_player->pause(); m_tick.stop(); }
    seekTo(m_playhead + frames / m_fps);
}

// ── Splicing ───────────────────────────────────────────────────────────────

void VideoEditorWindow::setTool(VideoTimeline::Tool tool)
{
    m_timeline->setTool(tool);
    (tool == VideoTimeline::Tool::Razor ? m_razorAct : m_selectAct)->setChecked(true);
    statusBar()->showMessage(tool == VideoTimeline::Tool::Razor
                                 ? tr("Razor: click the timeline to split it; click a segment to select it")
                                 : tr("Select: drag across the timeline to select a section"),
                             4000);
}

void VideoEditorWindow::selectRange(double start, double end) { m_timeline->setSelection(start, end); }
void VideoEditorWindow::clearSelection() { m_timeline->clearSelection(); }

void VideoEditorWindow::markSelectionStart()
{
    const double end = m_timeline->hasSelection() && m_timeline->selectionEnd() > m_playhead
                           ? m_timeline->selectionEnd() : endSeconds();
    m_timeline->setSelection(m_playhead, end);
}

void VideoEditorWindow::markSelectionEnd()
{
    const double start = m_timeline->hasSelection() && m_timeline->selectionStart() < m_playhead
                             ? m_timeline->selectionStart() : (m_cue ? m_cue->trimInSeconds() : 0.0);
    m_timeline->setSelection(start, m_playhead);
}

void VideoEditorWindow::splitAtPlayhead()
{
    m_timeline->addSplitPoint(m_playhead);
    statusBar()->showMessage(tr("Split at %1 — click a segment to select it, Delete cuts it out")
                                 .arg(clock(m_playhead)), 4000);
}

void VideoEditorWindow::deleteSelection()
{
    if (!m_cue) return;
    if (!m_timeline->hasSelection()) {
        statusBar()->showMessage(tr("Select a section first: drag across the timeline, "
                                    "or split it with the razor and click a segment"), 4000);
        return;
    }
    const double a = m_timeline->selectionStart(), b = m_timeline->selectionEnd();
    editCuts(audio::addCut(m_cue->cuts(), a, b), tr("Delete section"));
    // Split points in or at the cut have done their job: its edges are the
    // boundaries now.
    for (double s : QList<double>(m_timeline->splitPoints()))
        if (s >= a - 1e-6 && s <= b + 1e-6) m_timeline->removeSplitPoint(s);
    m_timeline->clearSelection();
    statusBar()->showMessage(tr("Cut %1 – %2 (−%3). Right-click it to restore")
                                 .arg(clock(a), clock(b), seconds2(b - a)), 5000);
}

void VideoEditorWindow::keepOnlySelection()
{
    if (!m_cue || !m_timeline->hasSelection()) return;
    const double a = m_timeline->selectionStart(), b = m_timeline->selectionEnd();
    const double out = (m_duration > 0.0 && b >= m_duration - 0.005) ? 0.0 : b;
    if (m_undo) m_undo->beginMacro(tr("Keep only this"));
    edit(QStringLiteral("trimInSeconds"), a);
    edit(QStringLiteral("trimOutSeconds"), out);
    if (m_undo) m_undo->endMacro();
    m_timeline->clearSelection();
    if (!isPlaying()) seekTo(a);
}

void VideoEditorWindow::restoreCut(double start, double end)
{
    if (!m_cue) return;
    editCuts(audio::removeCut(m_cue->cuts(), start, end), tr("Restore section"));
}

void VideoEditorWindow::restoreAllCuts()
{
    if (!m_cue) return;
    editCuts({}, tr("Restore all cuts"));
}

void VideoEditorWindow::showTimelineMenu(double seconds, const QPoint &globalPos)
{
    if (!m_cue) return;
    QMenu menu(this);
    if (const auto *c = audio::cutAt(m_cue->cuts(), seconds)) {
        const double a = c->start, b = c->end;
        menu.addAction(tr("Restore section (%1 – %2, +%3)").arg(clock(a), clock(b), seconds2(b - a)),
                       this, [this, a, b] { restoreCut(a, b); });
    }
    if (m_timeline->hasSelection()) {
        menu.addAction(m_deleteAct);
        menu.addAction(m_keepAct);
    }
    if (!menu.isEmpty()) menu.addSeparator();
    menu.addAction(tr("Split here"), this, [this, seconds] { m_timeline->addSplitPoint(seconds); });
    menu.addAction(tr("Set In here"), this, [this, seconds] { seekTo(seconds); setInAtPlayhead(); });
    menu.addAction(tr("Set Out here"), this, [this, seconds] { seekTo(seconds); setOutAtPlayhead(); });
    menu.addSeparator();
    menu.addAction(m_restoreAllAct);
    menu.addAction(m_clearSplitsAct);
    menu.addAction(tr("Clear In / Out"), this, &VideoEditorWindow::clearTrims);
    menu.addSeparator();
    menu.addAction(tr("Fit the whole file"), this, [this] { m_timeline->zoomFit(); });
    menu.exec(globalPos);
}

void VideoEditorWindow::keyPressEvent(QKeyEvent *e)
{
    const bool shift = e->modifiers() & Qt::ShiftModifier;
    const bool ctrl = e->modifiers() & Qt::ControlModifier;
    switch (e->key()) {
    case Qt::Key_Space: togglePlay(); return;
    case Qt::Key_I: if (shift) markSelectionStart(); else setInAtPlayhead(); return;
    case Qt::Key_O: if (shift) markSelectionEnd(); else setOutAtPlayhead(); return;
    case Qt::Key_V: setTool(VideoTimeline::Tool::Select); return;
    case Qt::Key_B: setTool(VideoTimeline::Tool::Razor); return;
    case Qt::Key_K: if (ctrl) { splitAtPlayhead(); return; } break;
    case Qt::Key_Delete: case Qt::Key_Backspace: deleteSelection(); return;
    case Qt::Key_Escape: clearSelection(); return;
    case Qt::Key_Home: if (m_cue) seekTo(m_cue->trimInSeconds()); return;
    case Qt::Key_End: seekTo(std::max(0.0, endSeconds() - 1.0 / m_fps)); return;
    case Qt::Key_Left:  stepFrames(shift ? -int(std::lround(m_fps)) : -1); return;
    case Qt::Key_Right: stepFrames(shift ? int(std::lround(m_fps)) : 1); return;
    default: break;
    }
    QMainWindow::keyPressEvent(e);
}

void VideoEditorWindow::closeEvent(QCloseEvent *e)
{
    m_tick.stop();
    if (m_player) m_player->stop();
    if (m_thumbs) m_thumbs->stop();
    QMainWindow::closeEvent(e);
}

} // namespace quewi::ui
