#include "ui/ShowModeView.h"

#include "ui/Theme.h"

#include <QBoxLayout>
#include <QDateTime>
#include <QEvent>
#include <QFontMetrics>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QStyle>
#include <QTextLayout>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace quewi::ui {

namespace {

constexpr int kPollMs = 100;
constexpr int kComingUpRows = 4;
constexpr int kRunningRows = 3;
constexpr int kHitRows = 3;          // after the headline hit

// The number/time face — tabular digits so a ticking countdown doesn't
// jitter. The app's text face (IBM Plex Sans / Segoe UI) handles the words.
QString monoFamilies()
{
    return QStringLiteral("'JetBrains Mono','Cascadia Mono','Consolas',monospace");
}

QString rgba(const QColor &c, int alpha)
{
    return QStringLiteral("rgba(%1,%2,%3,%4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(alpha);
}

int px(double base, double scale) { return std::max(8, int(std::lround(base * scale))); }

// The cue's one-line "Audio · pre-wait 2.0 s · AUTO-CONTINUE" summary.
QString cueMeta(const ShowCueLine &c)
{
    QStringList parts;
    if (!c.type.isEmpty()) parts << c.type;
    if (c.durationSeconds > 0.0) parts << showClockText(c.durationSeconds);
    if (c.preWaitSeconds > 0.0)
        parts << ShowModeView::tr("pre-wait %1 s").arg(QString::number(c.preWaitSeconds, 'f', 1));
    if (c.autoContinue) parts << ShowModeView::tr("AUTO-CONTINUE");
    if (c.autoFollow) parts << ShowModeView::tr("AUTO-FOLLOW");
    return parts.join(QStringLiteral("  ·  "));
}

QString oneLine(const QString &s)
{
    return s.simplified();
}

} // namespace

// ── Text helpers ────────────────────────────────────────────────────────

QString showClockText(double seconds)
{
    if (seconds < 0.0) seconds = 0.0;
    const auto total = qint64(std::floor(seconds + 1e-6));
    const auto h = total / 3600, m = (total % 3600) / 60, s = total % 60;
    if (h > 0)
        return QStringLiteral("%1:%2:%3").arg(h).arg(m, 2, 10, QLatin1Char('0')).arg(s, 2, 10, QLatin1Char('0'));
    return QStringLiteral("%1:%2").arg(m).arg(s, 2, 10, QLatin1Char('0'));
}

QString showCountdownText(double seconds)
{
    if (seconds < 0.0) seconds = 0.0;
    if (seconds >= 600.0) return showClockText(seconds);
    // Tenths, truncated (not rounded) so "0:00.0" is the moment it fires.
    const auto tenths = qint64(std::floor(seconds * 10.0 + 1e-6));
    const auto m = tenths / 600, s = (tenths % 600) / 10, t = tenths % 10;
    return QStringLiteral("%1:%2.%3").arg(m).arg(s, 2, 10, QLatin1Char('0')).arg(t);
}

QString showRemainingText(double seconds)
{
    if (seconds < 0.0) return {};
    // Ceil so a cue with 0.4 s left still says -0:01, and "-0:00" is the end.
    return QStringLiteral("-%1").arg(showClockText(std::ceil(seconds - 1e-6)));
}

QString showDeskLinkText(ShowDeskStatus::Link link)
{
    switch (link) {
    case ShowDeskStatus::Link::Off:        return ShowModeView::tr("No desk feedback");
    case ShowDeskStatus::Link::Connecting: return ShowModeView::tr("Connecting");
    case ShowDeskStatus::Link::Live:       return ShowModeView::tr("Live");
    case ShowDeskStatus::Link::Failed:     return ShowModeView::tr("Failed");
    }
    return {};
}

QColor showDeskLinkColour(ShowDeskStatus::Link link)
{
    const auto &tk = Theme::tokens();
    switch (link) {
    case ShowDeskStatus::Link::Off:        return tk.ink40;
    case ShowDeskStatus::Link::Connecting: return tk.warn;
    case ShowDeskStatus::Link::Live:       return tk.running;
    case ShowDeskStatus::Link::Failed:     return tk.err;
    }
    return tk.ink40;
}

// ── ElideLabel ──────────────────────────────────────────────────────────

ElideLabel::ElideLabel(QWidget *parent) : QLabel(parent)
{
    // Ignored horizontally: the layout decides the width, the text fits it.
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    setTextFormat(Qt::PlainText);
}

void ElideLabel::setMaxLines(int lines)
{
    m_maxLines = std::max(1, lines);
    updateGeometry();
    update();
}

QSize ElideLabel::sizeHint() const
{
    const QFontMetrics fm(font());
    const auto m = contentsMargins();
    return QSize(fm.horizontalAdvance(text()) + m.left() + m.right() + 2,
                 fm.lineSpacing() * m_maxLines + m.top() + m.bottom());
}

QSize ElideLabel::minimumSizeHint() const
{
    const QFontMetrics fm(font());
    const auto m = contentsMargins();
    return QSize(0, fm.lineSpacing() * m_maxLines + m.top() + m.bottom());
}

void ElideLabel::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    const QRect r = contentsRect();
    const QFontMetrics fm(font());
    p.setFont(font());
    p.setPen(palette().color(foregroundRole()));
    const auto flags = alignment();
    if (m_maxLines <= 1 || !wordWrap()) {
        const QString t = fm.elidedText(text(), Qt::ElideRight, r.width());
        p.drawText(r, int(flags) | Qt::TextSingleLine, t);
        return;
    }
    // Wrap with QTextLayout, elide the last line that fits.
    QTextLayout layout(text(), font());
    QTextOption opt;
    opt.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    layout.setTextOption(opt);
    layout.beginLayout();
    std::vector<QTextLine> lines;
    while (true) {
        QTextLine line = layout.createLine();
        if (!line.isValid()) break;
        line.setLineWidth(r.width());
        lines.push_back(line);
        if (int(lines.size()) >= m_maxLines) break;
    }
    layout.endLayout();
    if (lines.empty()) return;
    const int lh = fm.lineSpacing();
    int y = r.top();
    if (flags & Qt::AlignVCenter) y = r.top() + (r.height() - lh * int(lines.size())) / 2;
    for (size_t i = 0; i < lines.size(); ++i) {
        const QTextLine &line = lines[i];
        const bool last = i + 1 == lines.size();
        const int from = line.textStart();
        QString chunk = last ? text().mid(from) : text().mid(from, line.textLength());
        if (last) chunk = fm.elidedText(chunk.simplified(), Qt::ElideRight, r.width());
        int x = r.left();
        if (flags & Qt::AlignRight) x = r.right() - fm.horizontalAdvance(chunk);
        else if (flags & Qt::AlignHCenter) x = r.left() + (r.width() - fm.horizontalAdvance(chunk)) / 2;
        p.drawText(x, y + fm.ascent(), chunk);
        y += lh;
    }
}

// ── Region colours ──────────────────────────────────────────────────────

QColor showMix(const QColor &a, const QColor &b, double t)
{
    t = std::clamp(t, 0.0, 1.0);
    return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t,
                            a.greenF() + (b.greenF() - a.greenF()) * t,
                            a.blueF() + (b.blueF() - a.blueF()) * t);
}

ShowRegionColours showRegionColours()
{
    const auto &tk = Theme::tokens();
    ShowRegionColours c;
    c.standby = tk.accent;
    c.coming  = tk.ink60;
    c.running = tk.running;
    // The hits need their own hue: info and loaded are the same blue at two
    // strengths, so one is derived from info — turned toward violet and
    // muted to the palette's pastel weight. Lavender, not purple.
    c.hits    = QColor::fromHslF(std::fmod(tk.info.hslHueF() + 55.0 / 360.0, 1.0), 0.48, 0.72);
    c.desk    = tk.loaded;
    return c;
}

// ── ThinProgressBar ─────────────────────────────────────────────────────

namespace {
constexpr int    kFrameMs   = 16;      // ~60 fps while gliding
constexpr double kEaseTauMs = 45.0;    // the drawn value's lag behind where it's heading
constexpr double kSnapBack  = 0.05;    // a step back bigger than this is a restart
constexpr double kMaxLeadMs = 1500.0;  // never extrapolate further than this past a change
}

ThinProgressBar::ThinProgressBar(QWidget *parent) : QWidget(parent)
{
    m_colour = Theme::tokens().accent;
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setFixedHeight(m_thickness);
    m_anim = new QTimer(this);
    m_anim->setInterval(kFrameMs);
    m_anim->setTimerType(Qt::PreciseTimer);
    connect(m_anim, &QTimer::timeout, this, &ThinProgressBar::frame);
}

bool ThinProgressBar::isGliding() const
{
    return m_anim->isActive();
}

void ThinProgressBar::snapTo(double v)
{
    m_target = v;
    m_shown = v;
    m_rate = -1.0;
    m_leadMs = 0.0;
    m_sinceChange.start();
    m_anim->stop();
    update();
}

void ThinProgressBar::setProgress(double p, const QString &item)
{
    const double v = p < 0.0 ? -1.0 : std::clamp(p, 0.0, 1.0);
    const bool newItem = item != m_item;
    m_item = item;
    // Cases that must not glide: a different thing, nothing known on either
    // side, a restart, or a bar nobody can see (tests included).
    if (newItem || v < 0.0 || m_target < 0.0 || m_shown < 0.0 || v < m_target - kSnapBack || !isVisible()) {
        if (!qFuzzyCompare(v + 2.0, m_shown + 2.0) || !qFuzzyCompare(v + 2.0, m_target + 2.0) || newItem)
            snapTo(v);
        return;
    }
    if (qFuzzyCompare(v + 2.0, m_target + 2.0)) return;   // the same value again: keep gliding
    const qint64 dt = m_sinceChange.restart();
    // Two changes a few ms apart (a snapshot right after a show) say nothing
    // about the real rate; keep whatever was known.
    if (v > m_target && dt >= 30) {
        // Rate from this change, smoothed with the last; lead a bit past the
        // gap we've seen so a late update doesn't stall the bar.
        const double r = (v - m_target) / double(dt);
        // One change is a guess; from the second rising change on, lead
        // ahead by up to one more gap at the latest rate — so the bar can
        // never run more than one observed step past what it was told.
        const bool trusted = m_rate >= 0.0;
        m_rate = r;
        m_leadMs = trusted ? std::min(kMaxLeadMs, double(dt)) : 0.0;
    } else if (v < m_target) {
        m_rate = -1.0;   // a small step back (overshoot corrected): just ease to it
        m_leadMs = 0.0;
    }
    m_target = v;
    if (!m_anim->isActive()) {
        m_sinceFrame.start();
        m_anim->start();
    }
}

void ThinProgressBar::frame()
{
    const double dtMs = std::max<double>(1.0, double(m_sinceFrame.restart()));
    double desired = m_target;
    const double lead = m_rate > 0.0 && m_leadMs > 0.0 ? std::min(double(m_sinceChange.elapsed()), m_leadMs) : 0.0;
    if (lead > 0.0) desired = std::min(1.0, m_target + m_rate * lead);
    const double k = 1.0 - std::exp(-dtMs / kEaseTauMs);
    m_shown += (desired - m_shown) * k;
    const bool stillLeading = m_rate > 0.0 && m_leadMs > 0.0 && lead < m_leadMs && desired < 1.0;
    if (std::abs(desired - m_shown) < 0.0005 && !stillLeading) {
        m_shown = desired;
        m_anim->stop();
    }
    update();
}

void ThinProgressBar::showEvent(QShowEvent *)
{
    m_shown = m_target;
    m_rate = -1.0;
    m_sinceChange.start();
}

void ThinProgressBar::hideEvent(QHideEvent *)
{
    m_anim->stop();
    m_shown = m_target;
}

void ThinProgressBar::setColour(const QColor &c)
{
    if (c == m_colour) return;
    m_colour = c;
    update();
}

void ThinProgressBar::setThickness(int t)
{
    m_thickness = std::max(2, t);
    setFixedHeight(m_thickness);
}

void ThinProgressBar::paintEvent(QPaintEvent *)
{
    const auto &tk = Theme::tokens();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    const QRectF r = rect();
    const double rad = std::min(2.0, r.height() / 2.0);
    p.setBrush(tk.bgInteractive);
    p.drawRoundedRect(r, rad, rad);
    if (m_shown >= 0.0) {
        QRectF f = r;
        f.setWidth(std::max(r.height(), r.width() * m_shown));
        p.setBrush(m_colour);
        p.drawRoundedRect(f, rad, rad);
    }
}

// ── ShowModeView ────────────────────────────────────────────────────────

ShowModeView::ShowModeView(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("showModeView"));
    setFocusPolicy(Qt::NoFocus);
    setAttribute(Qt::WA_StyledBackground);
    buildUi();
    m_tick = new QTimer(this);
    m_tick->setInterval(kPollMs);
    m_tick->setTimerType(Qt::PreciseTimer);
    connect(m_tick, &QTimer::timeout, this, &ShowModeView::tick);
    applyScale();
    render();
}

void ShowModeView::setSnapshotProvider(std::function<ShowSnapshot()> provider)
{
    m_provider = std::move(provider);
    if (m_provider && isVisible()) setSnapshot(m_provider());
}

void ShowModeView::setSnapshot(const ShowSnapshot &s)
{
    m_snap = s;
    render();
}

void ShowModeView::setPauseAllowed(bool allowed)
{
    m_pauseAllowed = allowed;
    renderTransport();
}

void ShowModeView::setFadeAllAllowed(bool allowed)
{
    m_fadeAllowed = allowed;
    renderTransport();
}

void ShowModeView::stopwatchToggle()
{
    if (m_swRunning) {
        m_swBase += m_swLeg.elapsed() / 1000.0;
        m_swRunning = false;
    } else {
        m_swLeg.start();
        m_swRunning = true;
    }
    updateClocks();
}

void ShowModeView::stopwatchReset()
{
    m_swBase = 0.0;
    m_swRunning = false;
    updateClocks();
}

double ShowModeView::stopwatchSeconds() const
{
    return m_swBase + (m_swRunning ? m_swLeg.elapsed() / 1000.0 : 0.0);
}

void ShowModeView::showEvent(QShowEvent *)
{
    if (m_provider) setSnapshot(m_provider());
    updateClocks();
    m_tick->start();
}

void ShowModeView::hideEvent(QHideEvent *)
{
    m_tick->stop();
}

void ShowModeView::resizeEvent(QResizeEvent *e)
{
    QWidget::resizeEvent(e);
    applyScale();
}

// The stopwatch chip: left click starts/pauses, right click resets.
bool ShowModeView::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_stopwatch && event->type() == QEvent::MouseButtonPress) {
        auto *me = static_cast<QMouseEvent *>(event);
        if (me->button() == Qt::RightButton) {
            stopwatchReset();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void ShowModeView::tick()
{
    if (m_provider) setSnapshot(m_provider());
    updateClocks();
}

// ── Building ────────────────────────────────────────────────────────────

namespace {

QWidget *card(QWidget *parent, const char *name, const char *region)
{
    auto *w = new QWidget(parent);
    w->setObjectName(QLatin1String(name));
    w->setProperty("role", "card");
    w->setProperty("region", QLatin1String(region));
    w->setAttribute(Qt::WA_StyledBackground);
    return w;
}

// A small caption inside a card ("ACTIVE", "PENDING"), tinted to its region.
QLabel *caps(QWidget *parent, const QString &text, const char *region)
{
    auto *l = new QLabel(text, parent);
    l->setProperty("role", "caps");
    l->setProperty("region", QLatin1String(region));
    return l;
}

ElideLabel *elide(QWidget *parent, const char *role, int lines = 1)
{
    auto *l = new ElideLabel(parent);
    l->setProperty("role", role);
    if (lines > 1) {
        l->setWordWrap(true);
        l->setMaxLines(lines);
    }
    return l;
}

QPushButton *button(QWidget *parent, const char *name, const QString &text)
{
    auto *b = new QPushButton(text, parent);
    b->setObjectName(QLatin1String(name));
    b->setFocusPolicy(Qt::NoFocus);
    b->setAutoDefault(false);
    b->setDefault(false);
    b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    return b;
}

} // namespace

ShowModeView::Row ShowModeView::makeRow(QWidget *parent, bool withBar, const char *region)
{
    Row r;
    r.frame = new QWidget(parent);
    r.frame->setProperty("role", "row");
    r.frame->setAttribute(Qt::WA_StyledBackground);
    auto *g = new QGridLayout(r.frame);
    g->setContentsMargins(0, 0, 0, 0);
    g->setHorizontalSpacing(10);
    g->setVerticalSpacing(2);
    r.edge = new QWidget(r.frame);
    r.edge->setFixedWidth(4);
    r.edge->setAttribute(Qt::WA_StyledBackground);
    r.lead = elide(r.frame, "rowLead");
    r.lead->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    r.lead->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    r.title = elide(r.frame, "rowTitle");
    r.detail = elide(r.frame, "rowDetail");
    r.trail = elide(r.frame, "rowTrail");
    r.trail->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    r.trail->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    for (QWidget *w : {static_cast<QWidget *>(r.lead), static_cast<QWidget *>(r.title),
                       static_cast<QWidget *>(r.detail), static_cast<QWidget *>(r.trail)})
        w->setProperty("region", QLatin1String(region));
    g->addWidget(r.edge, 0, 0, withBar ? 3 : 2, 1);
    g->addWidget(r.lead, 0, 1, 2, 1, Qt::AlignVCenter);
    g->addWidget(r.title, 0, 2);
    g->addWidget(r.detail, 1, 2);
    g->addWidget(r.trail, 0, 3, 2, 1, Qt::AlignVCenter);
    if (withBar) {
        r.bar = new ThinProgressBar(r.frame);
        g->addWidget(r.bar, 2, 1, 1, 3);
    }
    g->setColumnStretch(2, 1);
    return r;
}

void ShowModeView::setRowVisible(Row &r, bool on)
{
    r.frame->setVisible(on);
}

ShowModeView::Band ShowModeView::makeBand(QWidget *card, const char *region, const QString &heading)
{
    Band b;
    b.frame = new QWidget(card);
    b.frame->setProperty("role", "band");
    b.frame->setProperty("region", QLatin1String(region));
    b.frame->setAttribute(Qt::WA_StyledBackground);
    b.lay = new QHBoxLayout(b.frame);
    b.lay->setContentsMargins(16, 6, 12, 6);
    b.lay->setSpacing(10);
    b.heading = new QLabel(heading, b.frame);
    b.heading->setProperty("role", "heading");
    b.heading->setProperty("region", QLatin1String(region));
    b.lay->addWidget(b.heading);
    b.lay->addStretch(1);
    m_bandLayouts.push_back(b.lay);
    return b;
}

void ShowModeView::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(16, 10, 16, 16);
    root->setSpacing(10);

    // ── Header: show · list | position | stopwatch | clock | exit ──
    auto *header = new QHBoxLayout;
    header->setSpacing(14);
    m_showLabel = elide(this, "headerShow");
    m_showLabel->setObjectName(QStringLiteral("smShow"));
    m_showLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_positionLabel = elide(this, "headerPos");
    m_positionLabel->setObjectName(QStringLiteral("smPosition"));
    m_positionLabel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_stopwatch = new QPushButton(this);
    m_stopwatch->setObjectName(QStringLiteral("smStopwatch"));
    m_stopwatch->setFocusPolicy(Qt::NoFocus);
    m_stopwatch->setToolTip(tr("Show stopwatch — click to start/pause, right-click to reset"));
    m_stopwatch->installEventFilter(this);
    connect(m_stopwatch, &QPushButton::clicked, this, &ShowModeView::stopwatchToggle);
    m_clock = new QLabel(this);
    m_clock->setObjectName(QStringLiteral("smClock"));
    m_clock->setProperty("role", "clock");
    m_exit = new QPushButton(tr("Exit Show Mode"), this);
    m_exit->setObjectName(QStringLiteral("smExit"));
    m_exit->setFocusPolicy(Qt::NoFocus);
    connect(m_exit, &QPushButton::clicked, this, &ShowModeView::exitRequested);
    header->addWidget(m_showLabel, 1);
    header->addWidget(m_positionLabel);
    header->addSpacing(8);
    header->addWidget(m_stopwatch);
    header->addWidget(m_clock);
    header->addSpacing(8);
    header->addWidget(m_exit);
    root->addLayout(header);

    // ── Body: the stage column + the transport column ──
    auto *body = new QHBoxLayout;
    body->setSpacing(12);
    root->addLayout(body, 1);

    auto *stage = new QVBoxLayout;
    stage->setSpacing(10);
    body->addLayout(stage, 1);

    // Standby hero: the amber band, then the cue's own colour down the edge.
    m_standbyCard = card(this, "smStandbyCard", "standby");
    {
        auto *outer = new QVBoxLayout(m_standbyCard);
        outer->setContentsMargins(0, 0, 0, 0);
        outer->setSpacing(0);
        auto band = makeBand(m_standbyCard, "standby", tr("STANDBY"));
        m_standbyCaps = band.heading;
        outer->addWidget(band.frame);
        auto *h = new QHBoxLayout;
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(0);
        outer->addLayout(h, 1);
        m_standbyEdge = new QWidget(m_standbyCard);
        m_standbyEdge->setObjectName(QStringLiteral("smStandbyEdge"));
        m_standbyEdge->setAttribute(Qt::WA_StyledBackground);
        m_standbyEdge->setFixedWidth(8);
        h->addWidget(m_standbyEdge);
        auto *v = new QVBoxLayout;
        v->setContentsMargins(18, 12, 18, 14);
        v->setSpacing(4);
        h->addLayout(v, 1);

        auto *line = new QHBoxLayout;
        line->setSpacing(22);
        m_standbyNumber = new QLabel(m_standbyCard);
        m_standbyNumber->setObjectName(QStringLiteral("smStandbyNumber"));
        m_standbyNumber->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        m_standbyNumber->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        line->addWidget(m_standbyNumber, 0, Qt::AlignVCenter);
        auto *side = new QVBoxLayout;
        side->setSpacing(2);
        m_standbyName = elide(m_standbyCard, "standbyName", 2);
        m_standbyName->setObjectName(QStringLiteral("smStandbyName"));
        m_standbyMeta = elide(m_standbyCard, "standbyMeta");
        m_standbyMeta->setObjectName(QStringLiteral("smStandbyMeta"));
        m_standbyHits = elide(m_standbyCard, "standbyHits");
        m_standbyHits->setObjectName(QStringLiteral("smStandbyHits"));
        side->addStretch(1);
        side->addWidget(m_standbyName);
        side->addWidget(m_standbyMeta);
        side->addWidget(m_standbyHits);
        side->addStretch(1);
        line->addLayout(side, 1);
        v->addLayout(line);

        m_standbyNotes = elide(m_standbyCard, "standbyNotes", 3);
        m_standbyNotes->setObjectName(QStringLiteral("smStandbyNotes"));
        m_standbyNotes->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        v->addSpacing(6);
        v->addWidget(m_standbyNotes);

        m_standbyEmpty = new QLabel(tr("End of the cue list — nothing on standby."), m_standbyCard);
        m_standbyEmpty->setObjectName(QStringLiteral("smStandbyEmpty"));
        m_standbyEmpty->setProperty("role", "standbyEmpty");
        m_standbyEmpty->setWordWrap(true);
        v->addWidget(m_standbyEmpty);
        v->addStretch(1);
    }
    stage->addWidget(m_standbyCard, 5);

    // Coming up.
    m_comingCard = card(this, "smComingCard", "coming");
    {
        auto *outer = new QVBoxLayout(m_comingCard);
        outer->setContentsMargins(0, 0, 0, 0);
        outer->setSpacing(0);
        auto band = makeBand(m_comingCard, "coming", tr("COMING UP"));
        m_comingCaps = band.heading;
        outer->addWidget(band.frame);
        auto *v = new QVBoxLayout;
        v->setContentsMargins(18, 8, 18, 10);
        v->setSpacing(6);
        outer->addLayout(v, 1);
        for (int i = 0; i < kComingUpRows; ++i) {
            m_comingRows.push_back(makeRow(m_comingCard, false, "coming"));
            v->addWidget(m_comingRows.back().frame);
        }
        m_comingEmpty = new QLabel(tr("Nothing after this."), m_comingCard);
        m_comingEmpty->setProperty("role", "quiet");
        v->addWidget(m_comingEmpty);
        v->addStretch(1);
    }
    stage->addWidget(m_comingCard, 3);

    // Lower row: Now playing | Next lighting hit | Lighting desk.
    m_lowerRow = new QHBoxLayout;
    m_lowerRow->setSpacing(10);
    stage->addLayout(m_lowerRow, 4);

    m_runningCard = card(this, "smRunningCard", "running");
    {
        auto *outer = new QVBoxLayout(m_runningCard);
        outer->setContentsMargins(0, 0, 0, 0);
        outer->setSpacing(0);
        auto band = makeBand(m_runningCard, "running", tr("NOW PLAYING"));
        m_runningCaps = band.heading;
        m_runningState = new QLabel(m_runningCard);
        m_runningState->setObjectName(QStringLiteral("smRunningState"));
        m_runningState->setProperty("role", "chip");
        m_runningState->setProperty("region", "running");
        m_runningState->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        band.lay->addWidget(m_runningState);
        outer->addWidget(band.frame);
        auto *v = new QVBoxLayout;
        v->setContentsMargins(16, 10, 16, 10);
        v->setSpacing(8);
        outer->addLayout(v, 1);
        for (int i = 0; i < kRunningRows; ++i) {
            m_runningRows.push_back(makeRow(m_runningCard, true, "running"));
            v->addWidget(m_runningRows.back().frame);
        }
        v->addStretch(1);
        m_lastFired = elide(m_runningCard, "quiet");
        m_lastFired->setObjectName(QStringLiteral("smLastFired"));
        v->addWidget(m_lastFired);
    }
    m_runningCard->setObjectName(QStringLiteral("smRunning"));
    m_lowerRow->addWidget(m_runningCard, 5);

    m_hitsCard = card(this, "smHitsCard", "hits");
    {
        auto *outer = new QVBoxLayout(m_hitsCard);
        outer->setContentsMargins(0, 0, 0, 0);
        outer->setSpacing(0);
        auto band = makeBand(m_hitsCard, "hits", tr("NEXT LIGHTING HIT"));
        m_hitsCaps = band.heading;
        m_armed = new QPushButton(m_hitsCard);
        m_armed->setObjectName(QStringLiteral("smArmed"));
        m_armed->setCheckable(true);
        m_armed->setFocusPolicy(Qt::NoFocus);
        m_armed->setToolTip(tr("Lighting triggers armed — click to disarm / arm"));
        connect(m_armed, &QPushButton::toggled, this, [this](bool on) {
            if (on != m_snap.triggersArmed) emit armedToggled(on);
        });
        band.lay->addWidget(m_armed);
        outer->addWidget(band.frame);
        auto *v = new QVBoxLayout;
        v->setContentsMargins(16, 8, 16, 10);
        v->setSpacing(4);
        outer->addLayout(v, 1);
        m_hitCountdown = new QLabel(m_hitsCard);
        m_hitCountdown->setObjectName(QStringLiteral("smHitCountdown"));
        m_hitCountdown->setProperty("role", "hitCountdown");
        v->addWidget(m_hitCountdown);
        m_hitName = elide(m_hitsCard, "hitName");
        m_hitName->setObjectName(QStringLiteral("smHitName"));
        m_hitDoes = elide(m_hitsCard, "hitDoes");
        m_hitDoes->setObjectName(QStringLiteral("smHitDoes"));
        v->addWidget(m_hitName);
        v->addWidget(m_hitDoes);
        v->addSpacing(6);
        for (int i = 0; i < kHitRows; ++i) {
            m_hitRows.push_back(makeRow(m_hitsCard, false, "hits"));
            m_hitRows.back().edge->hide();
            v->addWidget(m_hitRows.back().frame);
        }
        m_hitsState = new QLabel(m_hitsCard);
        m_hitsState->setObjectName(QStringLiteral("smHitsState"));
        m_hitsState->setProperty("role", "quiet");
        m_hitsState->setWordWrap(true);
        v->addWidget(m_hitsState);
        v->addStretch(1);
    }
    m_lowerRow->addWidget(m_hitsCard, 4);

    m_deskCard = card(this, "smDeskCard", "desk");
    {
        auto *outer = new QVBoxLayout(m_deskCard);
        outer->setContentsMargins(0, 0, 0, 0);
        outer->setSpacing(0);
        auto band = makeBand(m_deskCard, "desk", tr("LIGHTING DESK"));
        m_deskCaps = band.heading;
        m_deskDot = new QLabel(m_deskCard);
        m_deskDot->setObjectName(QStringLiteral("smDeskDot"));
        m_deskDot->setFixedSize(10, 10);
        m_deskLink = elide(m_deskCard, "deskLink");
        m_deskLink->setObjectName(QStringLiteral("smDeskLink"));
        m_deskLink->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        band.lay->addWidget(m_deskDot);
        band.lay->addWidget(m_deskLink);
        outer->addWidget(band.frame);
        auto *v = new QVBoxLayout;
        v->setContentsMargins(16, 8, 16, 10);
        v->setSpacing(4);
        outer->addLayout(v, 1);
        m_deskDetail = elide(m_deskCard, "quiet", 2);
        m_deskDetail->setObjectName(QStringLiteral("smDeskDetail"));
        v->addWidget(m_deskDetail);
        v->addSpacing(4);
        m_deskActiveCaps = caps(m_deskCard, tr("ACTIVE"), "desk");
        v->addWidget(m_deskActiveCaps);
        auto *act = new QHBoxLayout;
        act->setSpacing(12);
        m_deskActive = new QLabel(m_deskCard);
        m_deskActive->setObjectName(QStringLiteral("smDeskActive"));
        m_deskActive->setProperty("role", "deskActive");
        m_deskActiveTime = new QLabel(m_deskCard);
        m_deskActiveTime->setObjectName(QStringLiteral("smDeskActiveTime"));
        m_deskActiveTime->setProperty("role", "deskTime");
        m_deskActiveTime->setAlignment(Qt::AlignRight | Qt::AlignBottom);
        act->addWidget(m_deskActive);
        act->addStretch(1);
        act->addWidget(m_deskActiveTime);
        v->addLayout(act);
        m_deskActiveLabel = elide(m_deskCard, "deskLabel");
        m_deskActiveLabel->setObjectName(QStringLiteral("smDeskActiveLabel"));
        v->addWidget(m_deskActiveLabel);
        m_deskBar = new ThinProgressBar(m_deskCard);
        m_deskBar->setObjectName(QStringLiteral("smDeskBar"));
        v->addWidget(m_deskBar);
        v->addSpacing(6);
        m_deskPendingCaps = caps(m_deskCard, tr("PENDING"), "desk");
        v->addWidget(m_deskPendingCaps);
        m_deskPending = elide(m_deskCard, "deskPending");
        m_deskPending->setObjectName(QStringLiteral("smDeskPending"));
        v->addWidget(m_deskPending);
        m_deskBlind = new QLabel(tr("DESK IS IN BLIND"), m_deskCard);
        m_deskBlind->setObjectName(QStringLiteral("smDeskBlind"));
        m_deskBlind->setProperty("role", "blind");
        m_deskBlind->setAlignment(Qt::AlignCenter);
        v->addWidget(m_deskBlind);
        v->addStretch(1);
    }
    m_lowerRow->addWidget(m_deskCard, 4);

    // ── Transport column ──
    m_transport = new QWidget(this);
    m_transport->setObjectName(QStringLiteral("smTransport"));
    {
        auto *v = new QVBoxLayout(m_transport);
        v->setContentsMargins(0, 0, 0, 0);
        v->setSpacing(10);
        m_go = button(m_transport, "smGo", tr("GO"));
        m_pause = button(m_transport, "smPause", tr("Pause"));
        m_fadeAll = button(m_transport, "smFadeAll", tr("Fade All"));
        m_panic = button(m_transport, "smPanic", tr("PANIC"));
        connect(m_go, &QPushButton::clicked, this, &ShowModeView::goPressed);
        connect(m_pause, &QPushButton::clicked, this, &ShowModeView::pausePressed);
        connect(m_fadeAll, &QPushButton::clicked, this, &ShowModeView::fadeAllPressed);
        connect(m_panic, &QPushButton::clicked, this, &ShowModeView::panicPressed);
        v->addWidget(m_go, 9);
        v->addWidget(m_pause, 3);
        v->addWidget(m_fadeAll, 3);
        v->addStretch(2);              // PANIC sits apart: no accidental hit
        v->addWidget(m_panic, 3);
    }
    body->addWidget(m_transport);
}

// Font sizes, paddings and the column width follow the window: 1280×720 is
// scale 1 and 4K roughly 3. One stylesheet on the root reaches every child
// through its role/objectName, so a resize is a single restyle.
//
// Each region owns a colour (showRegionColours): a header band across its
// card, the heading in it, a faint wash over the card, and the region's key
// number — so STANDBY, NOW PLAYING, the hits and the desk read as different
// things from across a dark booth without any of them shouting.
void ShowModeView::applyScale()
{
    const double s = std::clamp(std::min(width() / 1280.0, height() / 720.0), 0.7, 3.0);
    if (!qFuzzyCompare(s, m_scale) || styleSheet().isEmpty()) {
        m_scale = s;
        const auto &tk = Theme::tokens();
        const QString mono = monoFamilies();
        const auto rc = showRegionColours();

        // The region rules, one block per region.
        QString regions;
        const struct { const char *name; QColor colour; } regionList[] = {
            {"standby", rc.standby}, {"coming", rc.coming}, {"running", rc.running},
            {"hits", rc.hits}, {"desk", rc.desk},
        };
        for (const auto &r : regionList) {
            const QString name = QLatin1String(r.name);
            const bool neutral = name == QLatin1String("coming");
            const QColor wash   = showMix(tk.bgPanel, r.colour, neutral ? 0.03 : 0.06);
            const QColor band   = showMix(tk.bgPanel, r.colour, neutral ? 0.10 : 0.17);
            const QColor border = showMix(tk.divider, r.colour, neutral ? 0.25 : 0.40);
            const QColor capsC  = showMix(tk.ink40, r.colour, 0.55);
            regions += QStringLiteral(
                "QWidget[role=\"card\"][region=\"%1\"] { background:%2; border:1px solid %3; }"
                "QWidget[role=\"band\"][region=\"%1\"] { background:%4; border-bottom:1px solid %3;"
                "  border-top-left-radius:3px; border-top-right-radius:3px; }"
                "QLabel[role=\"heading\"][region=\"%1\"] { color:%5; }"
                "QLabel[role=\"caps\"][region=\"%1\"] { color:%6; }")
                .arg(name, wash.name(), border.name(), band.name(), r.colour.name(), capsC.name());
        }

        const QString qss = QStringLiteral(
            "QWidget#showModeView { background:%1; }"
            "QWidget[role=\"card\"] { background:%2; border:1px solid %3; border-radius:4px; }"
            "QWidget[role=\"band\"] { background:transparent; }"
            "QWidget[role=\"row\"] { background:transparent; }"
            "QLabel { background:transparent; }"
            // Headings: a readable label, not a whisper.
            "QLabel[role=\"heading\"] { color:%6; font-size:%35px; font-weight:700; letter-spacing:0.12em; }"
            "QLabel[role=\"caps\"] { color:%4; font-size:%5px; font-weight:700; letter-spacing:0.15em; }"
            "QLabel[role=\"headerShow\"] { color:%6; font-size:%7px; font-weight:600; }"
            "QLabel[role=\"headerPos\"] { color:%4; font-size:%7px; }"
            "QLabel[role=\"clock\"] { color:%6; font-size:%7px; font-family:%8; }"
            "QPushButton#smStopwatch { color:%6; background:%9; border:1px solid %10; border-radius:3px;"
            "  font-size:%7px; font-family:%8; padding:%11px %12px; }"
            "QPushButton#smStopwatch[running=\"true\"] { color:%13; border-color:%13; }"
            "QPushButton#smExit { color:%4; background:transparent; border:1px solid %10; border-radius:3px;"
            "  font-size:%14px; padding:%11px %12px; }"
            "QPushButton#smExit:hover { color:%6; background:%9; }"
            // Standby: the number in amber, the name in full ink, the rest quieter.
            "QLabel#smStandbyNumber { color:%13; font-size:%16px; font-weight:800; letter-spacing:-0.02em; font-family:%8; }"
            "QLabel[role=\"standbyName\"] { color:%15; font-size:%17px; font-weight:700; }"
            "QLabel[role=\"standbyMeta\"] { color:%6; font-size:%18px; letter-spacing:0.04em; }"
            "QLabel[role=\"standbyHits\"] { color:%36; font-size:%18px; font-weight:600; }"
            "QLabel[role=\"standbyNotes\"] { color:%15; font-size:%19px; }"
            "QLabel[role=\"standbyEmpty\"] { color:%6; font-size:%17px; font-weight:600; }"
            // Rows: number and time in the mono face; the detail line dimmer.
            "QLabel[role=\"rowLead\"] { color:%15; font-size:%20px; font-weight:700; font-family:%8; }"
            "QLabel[role=\"rowLead\"][region=\"coming\"] { color:%6; }"
            "QLabel[role=\"rowLead\"][region=\"hits\"] { color:%36; font-size:%21px; }"
            "QLabel[role=\"rowTitle\"] { color:%15; font-size:%21px; font-weight:600; }"
            "QLabel[role=\"rowDetail\"] { color:%4; font-size:%14px; }"
            "QLabel[role=\"rowDetail\"][region=\"running\"] { color:%6; }"
            "QLabel[role=\"rowTrail\"] { color:%15; font-size:%22px; font-weight:700; font-family:%8; }"
            "QLabel[role=\"rowTrail\"][region=\"running\"] { color:%28; }"
            "QLabel[role=\"rowTrail\"][region=\"coming\"] { color:%4; font-size:%5px; letter-spacing:0.1em; }"
            "QLabel[role=\"quiet\"] { color:%4; font-size:%14px; }"
            "QLabel[role=\"quiet\"][tone=\"warn\"] { color:%23; }"
            "QLabel[role=\"chip\"] { color:%6; font-size:%5px; font-weight:700; letter-spacing:0.1em; }"
            "QLabel[role=\"chip\"][tone=\"warn\"] { color:%23; }"
            // Hits: the countdown in the hits' lavender, dim when disarmed, amber when frozen.
            "QLabel[role=\"hitCountdown\"] { color:%36; font-size:%24px; font-weight:800; font-family:%8; }"
            "QLabel[role=\"hitCountdown\"][tone=\"off\"] { color:%4; }"
            "QLabel[role=\"hitCountdown\"][tone=\"paused\"] { color:%23; }"
            "QLabel[role=\"hitName\"] { color:%15; font-size:%21px; font-weight:600; }"
            "QLabel[role=\"hitDoes\"] { color:%6; font-size:%14px; }"
            "QPushButton#smArmed { color:%36; background:transparent; border:1px solid %36; border-radius:3px;"
            "  font-size:%5px; font-weight:700; letter-spacing:0.1em; padding:%25px %12px; }"
            "QPushButton#smArmed:!checked { color:%23; border-color:%23; background:%26; }"
            // Desk: its cue number in the desk's blue.
            "QLabel[role=\"deskLink\"] { color:%6; font-size:%14px; font-weight:600; }"
            "QLabel[role=\"deskActive\"] { color:%37; font-size:%27px; font-weight:800; font-family:%8; }"
            "QLabel[role=\"deskTime\"] { color:%6; font-size:%21px; font-family:%8; }"
            "QLabel[role=\"deskLabel\"] { color:%15; font-size:%21px; }"
            "QLabel[role=\"deskPending\"] { color:%6; font-size:%21px; font-weight:600; }"
            "QLabel[role=\"blind\"] { color:%23; background:%26; border-radius:3px; font-size:%18px;"
            "  font-weight:700; letter-spacing:0.1em; padding:%25px; }"
            "QPushButton#smGo { color:%1; background:%28; border:1px solid transparent; border-radius:6px;"
            "  font-size:%29px; font-weight:800; letter-spacing:0.04em; }"
            "QPushButton#smGo:pressed { background:%30; }"
            "QPushButton#smGo:disabled { color:%4; background:%2; border-color:%3; }"
            "QPushButton#smPause, QPushButton#smFadeAll { color:%15; background:%9; border:1px solid %10;"
            "  border-radius:4px; font-size:%31px; font-weight:600; }"
            "QPushButton#smPause:pressed, QPushButton#smFadeAll:pressed { background:%3; }"
            "QPushButton#smPause:disabled, QPushButton#smFadeAll:disabled { color:%4; border-color:%3; }"
            "QPushButton#smPause[paused=\"true\"] { color:%23; border-color:%23; }"
            "QPushButton#smPanic { color:%32; background:%33; border:1px solid %34; border-radius:4px;"
            "  font-size:%31px; font-weight:800; letter-spacing:0.08em; }"
            "QPushButton#smPanic:pressed { color:%1; background:%32; }")
            .arg(tk.bgDeep.name(),                 // 1
                 tk.bgPanel.name(),                // 2
                 tk.divider.name(),                // 3
                 tk.ink40.name(),                  // 4
                 QString::number(px(11, s)),       // 5 caps
                 tk.ink60.name(),                  // 6
                 QString::number(px(15, s)),       // 7 header
                 mono,                             // 8
                 tk.bgInteractive.name())          // 9
            .arg(tk.outline.name(),                // 10
                 QString::number(px(4, s)),        // 11 pad v
                 QString::number(px(12, s)),       // 12 pad h
                 tk.accent.name(),                 // 13
                 QString::number(px(13, s)),       // 14 small
                 tk.ink100.name(),                 // 15
                 QString::number(px(118, s)),      // 16 standby number
                 QString::number(px(28, s)),       // 17 standby name
                 QString::number(px(14, s)))       // 18 meta
            .arg(QString::number(px(22, s)),       // 19 notes
                 QString::number(px(22, s)),       // 20 row lead
                 QString::number(px(17, s)),       // 21 row title
                 QString::number(px(26, s)),       // 22 row trail
                 tk.warn.name(),                   // 23
                 QString::number(px(44, s)),       // 24 hit countdown
                 QString::number(px(2, s)),        // 25 chip pad
                 rgba(tk.warn, 28),                // 26 faint warn
                 QString::number(px(36, s)))       // 27 desk active
            .arg(tk.running.name(),                // 28 GO green
                 QString::number(px(34, s)),       // 29 GO font
                 tk.accentSoft.name(),             // 30
                 QString::number(px(18, s)),       // 31 transport font
                 tk.errBright.name(),              // 32
                 rgba(tk.errBright, 26),           // 33 faint red
                 rgba(tk.errBright, 90),           // 34
                 QString::number(px(13, s)),       // 35 heading
                 rc.hits.name(),                   // 36 hits lavender
                 rc.desk.name())                   // 37 desk blue
            + regions;
        setStyleSheet(qss);

        // Geometry that QSS can't express.
        const int col = std::clamp(int(width() * 0.21), 170, 360);
        m_transport->setFixedWidth(col);
        m_standbyEdge->setFixedWidth(px(8, s));
        m_deskDot->setFixedSize(px(10, s), px(10, s));
        m_deskBar->setThickness(px(5, s));
        for (auto &r : m_runningRows) r.bar->setThickness(px(4, s));
        for (auto &r : m_comingRows) r.edge->setFixedWidth(px(4, s));
        for (auto &r : m_runningRows) r.edge->setFixedWidth(px(4, s));
        for (auto *lay : m_bandLayouts) lay->setContentsMargins(px(16, s), px(6, s), px(12, s), px(6, s));
        // The notes block gets more lines the taller the screen.
        m_standbyNotes->setMaxLines(height() >= 1000 ? 4 : 3);
    }
    // Lead columns size to their widest plausible text so the titles line up
    // (polished first, so the metrics are the stylesheet's font).
    for (auto *w : findChildren<ElideLabel *>()) w->ensurePolished();
    const QFontMetrics lead(m_comingRows.front().lead->font());
    const int leadW = lead.horizontalAdvance(QStringLiteral("888.8"));
    for (auto &r : m_comingRows) r.lead->setFixedWidth(leadW);
    for (auto &r : m_runningRows) r.lead->setFixedWidth(leadW);
    const QFontMetrics trail(m_runningRows.front().trail->font());
    for (auto &r : m_runningRows) r.trail->setFixedWidth(trail.horizontalAdvance(QStringLiteral("-88:88")) + 4);
    const QFontMetrics hitLead(m_hitRows.front().lead->font());
    for (auto &r : m_hitRows) {
        r.lead->setFixedWidth(hitLead.horizontalAdvance(QStringLiteral("8:88.8")) + 4);
        r.trail->setFixedWidth(0);
        r.trail->hide();
    }
}

// ── Rendering ───────────────────────────────────────────────────────────

void ShowModeView::render()
{
    renderHeader();
    renderStandby();
    renderComingUp();
    renderRunning();
    renderHits();
    renderDesk();
    renderTransport();
}

void ShowModeView::renderHeader()
{
    QString show = m_snap.showName.isEmpty() ? tr("Untitled show") : m_snap.showName;
    if (!m_snap.listName.isEmpty()) show += QStringLiteral("  ·  ") + m_snap.listName;
    m_showLabel->setText(show);
    if (m_snap.cueCount <= 0)
        m_positionLabel->setText(tr("No cues"));
    else if (m_snap.position <= 0)
        m_positionLabel->setText(tr("End of %1 cues").arg(m_snap.cueCount));
    else
        m_positionLabel->setText(tr("Cue %1 of %2").arg(m_snap.position).arg(m_snap.cueCount));
}

namespace {

void setEdgeColour(QWidget *edge, const QColor &c)
{
    const QString want = c.isValid()
        ? QStringLiteral("background:%1; border-radius:2px;").arg(c.name())
        : QStringLiteral("background:transparent;");
    if (edge->styleSheet() != want) edge->setStyleSheet(want);
}

void setTone(QWidget *w, const char *tone)
{
    if (w->property("tone").toString() == QLatin1String(tone)) return;
    w->setProperty("tone", QLatin1String(tone));
    w->style()->unpolish(w);
    w->style()->polish(w);
}

void setFlag(QWidget *w, const char *prop, bool on)
{
    const QString v = on ? QStringLiteral("true") : QStringLiteral("false");
    if (w->property(prop).toString() == v) return;
    w->setProperty(prop, v);
    w->style()->unpolish(w);
    w->style()->polish(w);
}

} // namespace

void ShowModeView::renderStandby()
{
    const bool has = m_snap.standby.has_value();
    m_standbyNumber->setVisible(has);
    m_standbyName->setVisible(has);
    m_standbyMeta->setVisible(has);
    m_standbyNotes->setVisible(has);
    m_standbyEmpty->setVisible(!has);
    if (!has) {
        m_standbyHits->hide();
        setEdgeColour(m_standbyEdge, QColor());
        return;
    }
    const auto &c = *m_snap.standby;
    m_standbyNumber->setText(c.number);
    m_standbyName->setText(c.name);
    m_standbyMeta->setText(cueMeta(c));
    m_standbyNotes->setText(c.notes);
    if (c.lightTriggers > 0) {
        QString t = c.lightTriggers == 1 ? tr("1 lighting hit") : tr("%1 lighting hits").arg(c.lightTriggers);
        if (c.firstTriggerSeconds >= 0.0)
            t += tr("  ·  first %1 after GO").arg(showClockText(c.firstTriggerSeconds));
        m_standbyHits->setText(t);
        m_standbyHits->show();
    } else {
        m_standbyHits->hide();
    }
    setEdgeColour(m_standbyEdge, c.colour.isValid() ? c.colour : Theme::tokens().accent);
}

void ShowModeView::renderComingUp()
{
    const auto &list = m_snap.comingUp;
    for (size_t i = 0; i < m_comingRows.size(); ++i) {
        auto &r = m_comingRows[i];
        if (i >= list.size()) {
            setRowVisible(r, false);
            continue;
        }
        const auto &c = list[i];
        r.lead->setText(c.number);
        r.title->setText(c.name);
        QString d = oneLine(c.notes);
        if (d.isEmpty()) d = cueMeta(c);
        r.detail->setText(d);
        r.detail->setVisible(!d.isEmpty());
        r.trail->setText(c.autoContinue ? tr("AUTO") : c.autoFollow ? tr("FOLLOW") : QString());
        r.trail->setVisible(!r.trail->text().isEmpty());
        setEdgeColour(r.edge, c.colour);
        setRowVisible(r, true);
    }
    m_comingEmpty->setVisible(list.empty());
    m_comingEmpty->setText(m_snap.standby ? tr("Nothing after this.") : tr("Nothing after this — end of the list."));
}

void ShowModeView::renderRunning()
{
    const auto &list = m_snap.running;
    const auto &tk = Theme::tokens();
    for (size_t i = 0; i < m_runningRows.size(); ++i) {
        auto &r = m_runningRows[i];
        if (i >= list.size()) {
            setRowVisible(r, false);
            continue;
        }
        const auto &rc = list[i];
        r.lead->setText(rc.cue.number);
        r.title->setText(rc.cue.name);
        QStringList d;
        d << tr("%1 elapsed").arg(showClockText(rc.elapsedSeconds));
        if (rc.paused) d << tr("PAUSED");
        if (rc.looping) d << tr("LOOPING");
        r.detail->setText(d.join(QStringLiteral("  ·  ")));
        r.detail->show();
        QString rem = showRemainingText(rc.remainingSeconds);
        if (rem.isEmpty()) rem = rc.looping ? QStringLiteral("∞") : QStringLiteral("—");
        r.trail->setText(rem);
        r.trail->show();
        r.bar->setProgress(rc.progress, rc.cue.id.toString());
        r.bar->setColour(rc.paused ? tk.warn : tk.running);
        setEdgeColour(r.edge, rc.cue.colour.isValid() ? rc.cue.colour : tk.running);
        setRowVisible(r, true);
    }
    if (list.empty()) {
        m_runningState->setText(tr("Nothing playing"));
        setTone(m_runningState, "quiet");
    } else if (m_snap.paused) {
        m_runningState->setText(tr("PAUSED"));
        setTone(m_runningState, "warn");
    } else if (list.size() > m_runningRows.size()) {
        m_runningState->setText(tr("+%1 more").arg(int(list.size() - m_runningRows.size())));
        setTone(m_runningState, "quiet");
    } else {
        m_runningState->setText(list.size() == 1 ? tr("1 cue") : tr("%1 cues").arg(int(list.size())));
        setTone(m_runningState, "quiet");
    }
    if (m_snap.lastFired) {
        m_lastFired->setText(tr("Last GO: %1  %2").arg(m_snap.lastFired->number, m_snap.lastFired->name));
        m_lastFired->show();
    } else {
        m_lastFired->hide();
    }
}

void ShowModeView::renderHits()
{
    const auto &hits = m_snap.hits;
    const bool armed = m_snap.triggersArmed;
    {
        QSignalBlocker block(m_armed);
        m_armed->setChecked(armed);
        m_armed->setText(armed ? tr("ARMED") : tr("DISARMED"));
    }
    const bool any = !hits.empty();
    m_hitCountdown->setVisible(any);
    m_hitName->setVisible(any);
    m_hitDoes->setVisible(any);
    if (any) {
        const auto &h = hits.front();
        m_hitCountdown->setText(showCountdownText(h.inSeconds));
        QString name = h.name.isEmpty() ? h.does : h.name;
        if (h.exit) name = tr("%1 (end)").arg(name);
        m_hitName->setText(name);
        QStringList d;
        if (!h.name.isEmpty() && !h.does.isEmpty()) d << h.does;
        if (!h.cueNumber.isEmpty() || !h.cueName.isEmpty())
            d << tr("in %1 %2").arg(h.cueNumber, h.cueName).simplified();
        m_hitDoes->setText(d.join(QStringLiteral("  ·  ")));
        setTone(m_hitCountdown, !armed ? "off" : h.paused ? "paused" : "live");
    }
    for (size_t i = 0; i < m_hitRows.size(); ++i) {
        auto &r = m_hitRows[i];
        const size_t k = i + 1;
        if (k >= hits.size()) {
            setRowVisible(r, false);
            continue;
        }
        const auto &h = hits[k];
        r.lead->setText(showCountdownText(h.inSeconds));
        QString name = h.name.isEmpty() ? h.does : h.name;
        if (h.exit) name = tr("%1 (end)").arg(name);
        r.title->setText(name);
        r.detail->setText(!h.name.isEmpty() ? h.does : QString());
        r.detail->setVisible(!r.detail->text().isEmpty());
        setRowVisible(r, true);
    }
    // The state line under it all.
    QString state;
    if (!armed)
        state = any ? tr("Triggers disarmed — nothing will be sent to the desk.")
                    : tr("Triggers disarmed.");
    else if (any && hits.front().paused)
        state = tr("Paused — the countdown is frozen.");
    else if (!any)
        state = m_snap.running.empty() ? tr("Hits appear here while a song with lighting triggers plays.")
                                       : tr("No more hits in what's playing.");
    m_hitsState->setText(state);
    m_hitsState->setVisible(!state.isEmpty());
    setTone(m_hitsState, !armed ? "warn" : "quiet");
}

void ShowModeView::renderDesk()
{
    const auto &d = m_snap.desk;
    const bool on = d.link != ShowDeskStatus::Link::Off;
    // With no feedback at all the card still sits there (so the lower row
    // keeps its shape) but says only why it's quiet.
    QString link = showDeskLinkText(d.link);
    if (on && !d.deskName.isEmpty()) link = QStringLiteral("%1 · %2").arg(d.deskName, link);
    m_deskLink->setText(link);
    m_deskDot->setStyleSheet(QStringLiteral("background:%1; border-radius:%2px;")
                                 .arg(showDeskLinkColour(d.link).name())
                                 .arg(m_deskDot->width() / 2));
    QStringList detail;
    if (!d.address.isEmpty() && on) detail << d.address;
    if (!d.showName.isEmpty() && d.link == ShowDeskStatus::Link::Live) detail << d.showName;
    if (!d.detail.isEmpty() && d.link != ShowDeskStatus::Link::Live) detail << d.detail;
    if (!on) detail << tr("Turn on desk feedback in the lighting desk settings to see its cues here.");
    m_deskDetail->setText(detail.join(QStringLiteral("  ·  ")));
    m_deskDetail->setVisible(!detail.isEmpty());

    const bool live = d.link == ShowDeskStatus::Link::Live;
    m_deskActiveCaps->setVisible(live);
    m_deskActive->setVisible(live);
    m_deskActiveTime->setVisible(live);
    m_deskActiveLabel->setVisible(live);
    m_deskBar->setVisible(live);
    m_deskPendingCaps->setVisible(live);
    m_deskPending->setVisible(live);
    m_deskBlind->setVisible(live && d.blind);
    if (!live) return;
    auto cueText = [](const QString &list, const QString &cue) -> QString {
        if (cue.isEmpty()) return QStringLiteral("—");
        return list.isEmpty() ? cue : QStringLiteral("%1/%2").arg(list, cue);
    };
    m_deskActive->setText(cueText(d.activeList, d.activeCue));
    m_deskActiveLabel->setText(d.activeLabel);
    m_deskActiveLabel->setVisible(!d.activeLabel.isEmpty());
    m_deskActiveTime->setText(d.activeTime);
    // Keyed on the cue so a new cue snaps to its start instead of gliding back.
    m_deskBar->setProgress(d.activeProgress, QStringLiteral("%1/%2").arg(d.activeList, d.activeCue));
    m_deskBar->setColour(d.activeProgress >= 0.999 ? Theme::tokens().running : showRegionColours().desk);
    QString pending = cueText(d.pendingList, d.pendingCue);
    if (!d.pendingLabel.isEmpty()) pending += QStringLiteral("   ") + d.pendingLabel;
    m_deskPending->setText(pending);
}

void ShowModeView::renderTransport()
{
    const bool has = m_snap.standby.has_value();
    m_go->setText(has ? QStringLiteral("GO  %1").arg(m_snap.standby->number) : tr("GO"));
    m_go->setEnabled(has);
    m_pause->setVisible(m_pauseAllowed);
    m_pause->setText(m_snap.paused ? tr("Resume") : tr("Pause"));
    m_pause->setEnabled(m_pauseAllowed && (m_snap.paused || !m_snap.running.empty()));
    setFlag(m_pause, "paused", m_snap.paused);
    m_fadeAll->setVisible(m_fadeAllowed);
    m_fadeAll->setEnabled(m_fadeAllowed && !m_snap.running.empty());
}

void ShowModeView::updateClocks()
{
    m_clock->setText(QTime::currentTime().toString(QStringLiteral("HH:mm:ss")));
    const double sw = stopwatchSeconds();
    m_stopwatch->setText(tr("SHOW %1").arg(showClockText(sw)));
    setFlag(m_stopwatch, "running", m_swRunning);
}

} // namespace quewi::ui
