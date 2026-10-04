#include "ui/VideoEditorWindow.h"

#include "audio/AudioCue.h"
#include "audio/Db.h"
#include "core/UndoCommands.h"
#include "ui/EditorChrome.h"
#include "ui/Theme.h"
#include "ui/VideoThumbnailer.h"
#include "ui/VideoTimeline.h"
#include "video/PictureTiming.h"
#include "video/VideoCue.h"

#include <QAction>
#include <QAudioOutput>
#include <QCheckBox>
#include <QCloseEvent>
#include <QDoubleSpinBox>
#include <QFontMetrics>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMediaMetaData>
#include <QMediaPlayer>
#include <QPainter>
#include <QPushButton>
#include <QStatusBar>
#include <QToolBar>
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

QDoubleSpinBox *secondsSpin(QWidget *parent)
{
    auto *s = new QDoubleSpinBox(parent);
    s->setDecimals(2);
    s->setSingleStep(0.1);
    s->setRange(0.0, 24 * 3600.0);
    s->setSuffix(QStringLiteral(" s"));
    s->setKeyboardTracking(false);
    s->setAccelerated(true);
    return s;
}

} // namespace

// The frame at the playhead, letterboxed, at the opacity the cue will show
// it with (its opacity × the picture fade at that point). Outside the trims
// it's dimmed and labelled, since the cue never shows that part.
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
        sz.scale(QSizeF(size()) - QSizeF(16, 16), Qt::KeepAspectRatio);
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
            f.setPixelSize(12);
            f.setBold(true);
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
    resize(1120, 760);

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
    statusBar()->showMessage(tr("Space play · I / O set In / Out · Home / End jump · "
                                "← → step a frame (Shift: a second)"));
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

// ── Layout ─────────────────────────────────────────────────────────────────

void VideoEditorWindow::buildToolbar()
{
    auto *tb = addToolBar(tr("Video Editor"));
    styleEditorToolBar(tb);

    tb->addWidget(sectionLabel(tr("TRANSPORT"), tb));
    auto *toIn = tb->addAction(makeEditorIcon("toIn"), tr("To In"), this, [this] {
        if (m_cue) seekTo(m_cue->trimInSeconds());
    });
    toIn->setToolTip(tr("Go to the In point (Home)"));
    m_playAct = tb->addAction(makeEditorIcon("play"), tr("Play"), this, &VideoEditorWindow::togglePlay);
    m_playAct->setToolTip(tr("Play / pause (Space)"));
    auto *toOut = tb->addAction(makeEditorIcon("toOut"), tr("To Out"), this, [this] {
        seekTo(std::max(0.0, endSeconds() - 1.0 / m_fps));
    });
    toOut->setToolTip(tr("Go to the Out point (End)"));
    m_loopAct = tb->addAction(makeEditorIcon("loop"), tr("Loop"));
    m_loopAct->setCheckable(true);
    m_loopAct->setToolTip(tr("Loop the preview between In and Out"));
    m_hearAct = tb->addAction(makeEditorIcon("waveform"), tr("Sound"));
    m_hearAct->setCheckable(true);
    m_hearAct->setChecked(true);
    m_hearAct->setToolTip(tr("Hear the soundtrack while previewing (at the cue's level and fades)"));
    connect(m_hearAct, &QAction::toggled, this, [this] { applyPreviewLevel(); });

    tb->addWidget(toolbarDivider(tb));
    tb->addWidget(sectionLabel(tr("MARK"), tb));
    auto *setIn = tb->addAction(tr("Set In"), this, &VideoEditorWindow::setInAtPlayhead);
    setIn->setToolTip(tr("Start the cue here (I)"));
    auto *setOut = tb->addAction(tr("Set Out"), this, &VideoEditorWindow::setOutAtPlayhead);
    setOut->setToolTip(tr("End the cue here (O)"));
    auto *clear = tb->addAction(tr("Clear"), this, &VideoEditorWindow::clearTrims);
    clear->setToolTip(tr("Play the whole file again"));

    tb->addWidget(toolbarDivider(tb));
    tb->addWidget(sectionLabel(tr("ZOOM"), tb));
    tb->addAction(makeEditorIcon("zoomOut"), tr("Out"), this, [this] { m_timeline->zoomOut(); });
    tb->addAction(makeEditorIcon("zoomIn"),  tr("In"),  this, [this] { m_timeline->zoomIn(); });
    tb->addAction(makeEditorIcon("zoomFit"), tr("Fit"), this, [this] { m_timeline->zoomFit(); });

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
    }
}

void VideoEditorWindow::buildCentral()
{
    const auto &tk = Theme::tokens();
    auto *central = new QWidget(this);
    auto *vl = new QVBoxLayout(central);
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(0);

    const auto header = makeEditorHeader(tr("VIDEO CUE"), central);
    m_headerNumber = header.number;
    m_headerName   = header.name;
    m_headerMeta   = header.meta;
    vl->addWidget(header.widget);

    m_monitor = new VideoMonitor(central);
    vl->addWidget(m_monitor, 1);

    // Time readout under the monitor.
    auto *timeRow = new QWidget(central);
    auto *tl = new QHBoxLayout(timeRow);
    tl->setContentsMargins(20, 6, 20, 6);
    m_timeLabel = new QLabel(timeRow);
    m_timeLabel->setStyleSheet(QStringLiteral(
        "color:%1; font-family:'Space Grotesk','JetBrains Mono',monospace; font-size:18px;"
        " font-weight:600;").arg(tk.ink100.name()));
    m_lengthLabel = new QLabel(timeRow);
    m_lengthLabel->setStyleSheet(QStringLiteral(
        "color:%1; font-family:'Space Grotesk','JetBrains Mono',monospace; font-size:12px;")
        .arg(tk.ink60.name()));
    tl->addWidget(m_timeLabel);
    tl->addStretch(1);
    tl->addWidget(m_lengthLabel);
    vl->addWidget(timeRow);

    m_timeline = new VideoTimeline(central);
    vl->addWidget(m_timeline);

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

    // ── Fields ─────────────────────────────────────────────────────────
    auto *panel = new QWidget(central);
    panel->setObjectName(QStringLiteral("videoEditorPanel"));
    panel->setStyleSheet(QStringLiteral("QWidget#videoEditorPanel { background:%1;"
                                        " border-top:1px solid %2; }")
                             .arg(tk.bgPanel.name(), tk.divider.name()));
    auto *pl = new QHBoxLayout(panel);
    pl->setContentsMargins(16, 10, 16, 12);
    pl->setSpacing(16);

    auto *trimBox = new QGroupBox(tr("Trim"), panel);
    auto *tf = new QFormLayout(trimBox);
    m_inSpin = secondsSpin(trimBox);
    m_outSpin = secondsSpin(trimBox);
    m_outSpin->setSpecialValueText(tr("End"));   // 0 = the end of the file
    m_loopCheck = new QCheckBox(tr("Loop between In and Out"), trimBox);
    tf->addRow(tr("In"), m_inSpin);
    tf->addRow(tr("Out"), m_outSpin);
    tf->addRow(QString(), m_loopCheck);
    pl->addWidget(trimBox);

    auto *picBox = new QGroupBox(tr("Picture"), panel);
    auto *pf = new QFormLayout(picBox);
    m_picFadeInSpin = secondsSpin(picBox);
    m_picFadeOutSpin = secondsSpin(picBox);
    m_opacitySpin = new QDoubleSpinBox(picBox);
    m_opacitySpin->setRange(0.0, 1.0);
    m_opacitySpin->setSingleStep(0.05);
    m_opacitySpin->setDecimals(2);
    m_opacitySpin->setKeyboardTracking(false);
    pf->addRow(tr("Fade in"), m_picFadeInSpin);
    pf->addRow(tr("Fade out"), m_picFadeOutSpin);
    pf->addRow(tr("Opacity"), m_opacitySpin);
    pl->addWidget(picBox);

    auto *sndBox = new QGroupBox(tr("Sound"), panel);
    auto *sf = new QFormLayout(sndBox);
    m_soundCheck = new QCheckBox(tr("Play the video's sound"), sndBox);
    m_levelSpin = new QDoubleSpinBox(sndBox);
    m_levelSpin->setRange(-60.0, 12.0);
    m_levelSpin->setDecimals(1);
    m_levelSpin->setSingleStep(0.5);
    m_levelSpin->setSuffix(QStringLiteral(" dB"));
    m_levelSpin->setKeyboardTracking(false);
    m_sndFadeInSpin = secondsSpin(sndBox);
    m_sndFadeOutSpin = secondsSpin(sndBox);
    sf->addRow(QString(), m_soundCheck);
    sf->addRow(tr("Level"), m_levelSpin);
    sf->addRow(tr("Fade in"), m_sndFadeInSpin);
    sf->addRow(tr("Fade out"), m_sndFadeOutSpin);
    pl->addWidget(sndBox);

    auto *btns = new QVBoxLayout();
    m_editSoundBtn = new QPushButton(tr("Edit sound…"), panel);
    m_editSoundBtn->setToolTip(tr("Open the soundtrack in the audio editor: effects, EQ, "
                                  "compressor, presets"));
    m_lightingBtn = new QPushButton(tr("Lighting…"), panel);
    m_lightingBtn->setToolTip(tr("Lighting triggers on this video's soundtrack"));
    btns->addWidget(m_editSoundBtn);
    btns->addWidget(m_lightingBtn);
    btns->addStretch(1);
    pl->addLayout(btns);
    pl->addStretch(1);
    vl->addWidget(panel);

    auto spinEdit = [this](QDoubleSpinBox *spin, const QString &field) {
        connect(spin, &QDoubleSpinBox::valueChanged, this, [this, field](double v) {
            if (!m_loading) edit(field, v);
        });
    };
    connect(m_inSpin, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        if (m_loading || !m_cue) return;
        // Keep In before Out.
        v = std::min(v, std::max(0.0, endSeconds() - 0.05));
        edit(QStringLiteral("trimInSeconds"), v);
        if (!isPlaying()) seekTo(v);
    });
    connect(m_outSpin, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        if (m_loading || !m_cue) return;
        if (v > 0.0) v = std::max(v, m_cue->trimInSeconds() + 0.05);
        if (m_duration > 0.0 && v >= m_duration) v = 0.0;
        edit(QStringLiteral("trimOutSeconds"), v);
    });
    spinEdit(m_picFadeInSpin,  QStringLiteral("pictureFadeInSeconds"));
    spinEdit(m_picFadeOutSpin, QStringLiteral("pictureFadeOutSeconds"));
    spinEdit(m_opacitySpin,    QStringLiteral("opacity"));
    spinEdit(m_levelSpin,      QStringLiteral("sound.gainDb"));
    spinEdit(m_sndFadeInSpin,  QStringLiteral("sound.fadeInSeconds"));
    spinEdit(m_sndFadeOutSpin, QStringLiteral("sound.fadeOutSeconds"));
    connect(m_loopCheck, &QCheckBox::toggled, this, [this](bool on) {
        if (!m_loading) edit(QStringLiteral("loop"), on);
    });
    connect(m_soundCheck, &QCheckBox::toggled, this, [this](bool on) {
        if (!m_loading) edit(QStringLiteral("soundEnabled"), on);
    });
    connect(m_editSoundBtn, &QPushButton::clicked, this, [this] {
        if (m_cue) emit editSoundRequested(m_cue->sound());
    });
    connect(m_lightingBtn, &QPushButton::clicked, this, [this] {
        if (m_cue) emit lightingRequested(m_cue->sound());
    });

    setCentralWidget(central);
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

void VideoEditorWindow::refreshFromCue()
{
    if (!m_cue) return;
    m_loading = true;
    auto *sound = m_cue->sound();
    const double in = m_cue->trimInSeconds(), out = m_cue->trimOutSeconds();
    m_timeline->setTrims(in, out);
    m_timeline->setPictureFades(m_cue->pictureFadeInSeconds(), m_cue->pictureFadeOutSeconds());
    m_timeline->setSoundFades(sound->fadeInSeconds(), sound->fadeOutSeconds());
    m_timeline->setSoundEnabled(m_cue->soundEnabled());
    QList<double> marks;
    for (const auto &t : sound->lightTriggers()) marks << t.start;
    m_timeline->setTriggerMarks(marks);

    auto set = [](QDoubleSpinBox *s, double v) {
        if (!qFuzzyCompare(s->value() + 1.0, v + 1.0)) s->setValue(v);
    };
    if (m_duration > 0.0) {
        m_inSpin->setMaximum(m_duration);
        m_outSpin->setMaximum(m_duration);
    }
    set(m_inSpin, in);
    set(m_outSpin, out);
    set(m_picFadeInSpin, m_cue->pictureFadeInSeconds());
    set(m_picFadeOutSpin, m_cue->pictureFadeOutSeconds());
    set(m_opacitySpin, m_cue->opacity());
    set(m_levelSpin, sound->gainDb());
    set(m_sndFadeInSpin, sound->fadeInSeconds());
    set(m_sndFadeOutSpin, sound->fadeOutSeconds());
    m_loopCheck->setChecked(m_cue->loop());
    m_soundCheck->setChecked(m_cue->soundEnabled());
    for (QWidget *w : {static_cast<QWidget *>(m_levelSpin), static_cast<QWidget *>(m_sndFadeInSpin),
                       static_cast<QWidget *>(m_sndFadeOutSpin), static_cast<QWidget *>(m_editSoundBtn)})
        w->setEnabled(m_cue->soundEnabled());
    m_loading = false;

    updateHeader();
    updateTimeReadout();
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
    m_timeLabel->setText(QStringLiteral("%1 / %2").arg(clock(m_playhead), clock(m_duration)));
    const double in = m_cue->trimInSeconds(), end = endSeconds();
    m_lengthLabel->setText(tr("In %1   Out %2   Plays %3")
                               .arg(clock(in), clock(end), clock(std::max(0.0, end - in))));

    // The monitor shows the frame as the cue would: its opacity × the fade.
    const auto timing = m_cue->pictureTiming();
    QString note;
    double look = m_cue->opacity() * timing.envelope(m_playhead, m_duration, true);
    if (m_playhead < in - 1e-3) { note = tr("Before In"); look = 0.35; }
    else if (end > 0.0 && m_playhead > end + 1e-3) { note = tr("After Out"); look = 0.35; }
    m_monitor->setLook(look, note);
}

void VideoEditorWindow::updatePlayButton()
{
    if (!m_playAct) return;
    const bool playing = isPlaying();
    m_playAct->setIcon(makeEditorIcon(playing ? "pause" : "play"));
    m_playAct->setText(playing ? tr("Pause") : tr("Play"));
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
    if (m_player) m_player->setPosition(qint64(m_playhead * 1000.0));
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
    // From the playhead — or from In when it's outside what the cue plays.
    const double in = m_cue->trimInSeconds(), end = endSeconds();
    if (m_playhead < in - 1e-3 || (end > 0.0 && m_playhead >= end - 0.02)) seekTo(in);
    m_player->play();
    m_tick.start();
}

void VideoEditorWindow::onTick()
{
    if (!m_player || !m_cue) return;
    if (!isPlaying()) { m_tick.stop(); return; }
    m_playhead = m_player->position() / 1000.0;
    const double end = endSeconds();
    if (end > 0.0 && m_playhead >= end) {
        if (m_loopAct->isChecked() || m_cue->loop()) {
            seekTo(m_cue->trimInSeconds());
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

void VideoEditorWindow::keyPressEvent(QKeyEvent *e)
{
    const bool shift = e->modifiers() & Qt::ShiftModifier;
    switch (e->key()) {
    case Qt::Key_Space: togglePlay(); return;
    case Qt::Key_I: setInAtPlayhead(); return;
    case Qt::Key_O: setOutAtPlayhead(); return;
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
