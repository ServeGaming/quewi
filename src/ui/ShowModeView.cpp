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
#include <iterator>

namespace quewi::ui {

namespace {

constexpr int kPollMs = 100;
constexpr int kComingUpRows = 4;
constexpr int kRunningRows = 3;
constexpr int kGroupRun = 3;         // hits in a row before they fold into one line

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
    lines = std::max(1, lines);
    if (lines == m_maxLines) return;
    m_maxLines = lines;
    // setWordWrap() flags the size policy height-for-width; keep it off.
    QSizePolicy sp = sizePolicy();
    if (sp.hasHeightForWidth()) {
        sp.setHeightForWidth(false);
        setSizePolicy(sp);
    }
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

int ElideLabel::linesNeeded(int width, int cap) const
{
    if (cap <= 1 || text().isEmpty() || width <= 0) return 1;
    QTextLayout layout(text(), font());
    QTextOption opt;
    opt.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    layout.setTextOption(opt);
    layout.beginLayout();
    int n = 0;
    while (n < cap) {
        QTextLine line = layout.createLine();
        if (!line.isValid()) break;
        line.setLineWidth(width);
        ++n;
    }
    layout.endLayout();
    return std::max(1, n);
}

bool ElideLabel::isElided() const
{
    const QRect r = contentsRect();
    if (m_maxLines <= 1 || !wordWrap())
        return QFontMetrics(font()).horizontalAdvance(text()) > r.width();
    const int lh = QFontMetrics(font()).lineSpacing();
    const int roomFor = std::clamp(lh > 0 ? r.height() / lh : 1, 1, m_maxLines);
    return linesNeeded(r.width(), roomFor + 1) > roomFor;
}

void ElideLabel::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    const QRect r = contentsRect();
    const QFontMetrics fm(font());
    p.setFont(font());
    p.setPen(palette().color(foregroundRole()));
    const auto flags = alignment();
    const int lh = fm.lineSpacing();
    if (m_maxLines <= 1 || !wordWrap()) {
        const QString t = fm.elidedText(text(), Qt::ElideRight, r.width());
        p.drawText(r, int(flags) | Qt::TextSingleLine, t);
        return;
    }
    // Wrap with QTextLayout, elide the last line that fits — and only the
    // lines the height has room for, whole. A squeezed label loses a line,
    // never half of one.
    const int roomFor = std::clamp(lh > 0 ? r.height() / lh : 1, 1, m_maxLines);
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
        if (int(lines.size()) >= roomFor) break;
    }
    layout.endLayout();
    if (lines.empty()) return;
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

// ── Hit rows ────────────────────────────────────────────────────────────

namespace {

// "Beat 12" → ("Beat ", 12); "12" → ("", 12); "Lightning" → false.
bool splitNumbered(const QString &name, QString &prefix, int &number)
{
    const QString t = name.trimmed();
    int i = t.size();
    while (i > 0 && t.at(i - 1).isDigit()) --i;
    if (i == t.size() || t.size() - i > 6) return false;
    prefix = t.left(i);
    number = t.mid(i).toInt();
    return true;
}

QString hitTitle(const ShowUpcomingHit &h)
{
    QString name = h.name.isEmpty() ? h.does : h.name;
    return h.exit ? ShowModeView::tr("%1 (end)").arg(name) : name;
}

QString secondsText(double s, int decimals)
{
    if (s >= 60.0) return showClockText(s);
    return ShowModeView::tr("%1 s").arg(QString::number(s, 'f', s < 10.0 ? decimals : 1));
}

} // namespace

std::vector<ShowHitRow> showHitRows(const std::vector<ShowUpcomingHit> &hits)
{
    std::vector<ShowHitRow> rows;
    const size_t n = hits.size();
    size_t i = 0;
    while (i < n) {
        const auto &h = hits[i];
        // How far does a run of the same thing reach? Same action, same cue,
        // same frozen-ness, no range ends, and names that are one prefix
        // plus a rising number (or all unnamed).
        size_t j = i + 1;
        QString prefix;
        int first = 0, last = 0;
        const bool unnamed = h.name.isEmpty();
        const bool numbered = !unnamed && splitNumbered(h.name, prefix, first);
        bool consecutive = true;
        if (!h.exit && (unnamed || numbered)) {
            last = first;
            while (j < n) {
                const auto &g = hits[j];
                if (g.exit || g.does != h.does || g.cueNumber != h.cueNumber || g.paused != h.paused) break;
                if (unnamed) {
                    if (!g.name.isEmpty()) break;
                } else {
                    QString p;
                    int num = 0;
                    if (!splitNumbered(g.name, p, num) || p != prefix || num <= last) break;
                    if (num != last + 1) consecutive = false;
                    last = num;
                }
                ++j;
            }
        }
        const size_t run = j - i;
        ShowHitRow row;
        row.when = showCountdownText(h.inSeconds);
        row.firstIn = h.inSeconds;
        row.paused = h.paused;
        if (run < size_t(kGroupRun)) {
            row.title = hitTitle(h);
            row.detail = h.name.isEmpty() ? QString() : h.does;
            row.lastIn = h.inSeconds;
            rows.push_back(row);
            ++i;
            continue;
        }
        const auto &tail = hits[j - 1];
        row.count = int(run);
        row.lastIn = tail.inSeconds;
        QStringList detail;
        if (unnamed) {
            row.title = h.does;
        } else if (consecutive) {
            row.title = QStringLiteral("%1%2–%3").arg(prefix).arg(first).arg(last);
            if (!h.does.isEmpty()) detail << h.does;
        } else {
            row.title = QStringLiteral("%1 … %2").arg(h.name.trimmed(), tail.name.trimmed());
            if (!h.does.isEmpty()) detail << h.does;
        }
        if (unnamed || !consecutive) detail << ShowModeView::tr("%1 hits").arg(int(run));
        // The rhythm: "every 0.47 s" when the gaps are even, else the span.
        double sum = 0.0, lo = 1e9, hi = 0.0;
        for (size_t k = i + 1; k < j; ++k) {
            const double gap = std::max(0.0, hits[k].inSeconds - hits[k - 1].inSeconds);
            sum += gap;
            lo = std::min(lo, gap);
            hi = std::max(hi, gap);
        }
        const double mean = sum / double(run - 1);
        const bool even = mean > 0.0 && hi - lo <= std::max(0.04, mean * 0.25);
        if (even) detail << ShowModeView::tr("every %1").arg(secondsText(mean, 2));
        else      detail << ShowModeView::tr("over %1").arg(secondsText(tail.inSeconds - h.inSeconds, 1));
        row.detail = detail.join(QStringLiteral("  ·  "));
        rows.push_back(row);
        i = j;
    }
    return rows;
}

// ── HitList ─────────────────────────────────────────────────────────────

HitList::HitList(QWidget *parent) : QWidget(parent)
{
    // Preferred height = every row with two lines; minimum nothing, so in a
    // short card this is what gives way (the labels around it are rigid).
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    m_leadFont = m_titleFont = m_detailFont = font();
    const auto &tk = Theme::tokens();
    m_leadColour = showRegionColours().hits;
    m_titleColour = tk.ink100;
    m_detailColour = tk.ink40;
    m_muted = tk.ink40;
    m_pausedColour = tk.warn;
}

void HitList::setHits(const std::vector<ShowUpcomingHit> &hits, bool armed)
{
    m_rows = showHitRows(hits);
    m_armed = armed;
    updateGeometry();
    update();
}

void HitList::setFonts(const QFont &lead, const QFont &title, const QFont &detail)
{
    m_leadFont = lead;
    m_titleFont = title;
    m_detailFont = detail;
    updateGeometry();
    update();
}

void HitList::setColours(const QColor &lead, const QColor &title, const QColor &detail,
                         const QColor &muted, const QColor &paused)
{
    m_leadColour = lead;
    m_titleColour = title;
    m_detailColour = detail;
    m_muted = muted;
    m_pausedColour = paused;
    update();
}

void HitList::setRowGap(int gapPx)
{
    m_gap = std::max(0, gapPx);
    updateGeometry();
    update();
}

// The text sets the row height, not the countdown: a mono face has tall
// line metrics but its digits have no descenders, so the countdown sits on
// the title's baseline and takes no more room than the title does.
int HitList::rowHeight(bool two) const
{
    const int title = QFontMetrics(m_titleFont).height();
    const int detail = QFontMetrics(m_detailFont).height();
    return (two ? title + detail : title) + m_gap;
}

int HitList::moreHeight() const
{
    return QFontMetrics(m_detailFont).height() + m_gap;
}

HitList::Fit HitList::fitFor(int h) const
{
    Fit f;
    const int n = int(m_rows.size());
    if (n == 0) return f;
    const int h2 = rowHeight(true), h1 = rowHeight(false);
    if (n * h2 - m_gap <= h) {
        f.two = true;
        f.rows = n;
        f.height = n * h2 - m_gap;
        return f;
    }
    if (n * h1 - m_gap <= h) {
        f.rows = n;
        f.height = n * h1 - m_gap;
        return f;
    }
    // Not everything fits: whole rows only, then the "+N more" line.
    const int mh = moreHeight();
    if (h < mh - m_gap) return f;                  // not even room to say so
    f.rows = std::clamp((h - mh) / h1, 0, n - 1);
    f.moreLine = true;
    for (int k = f.rows; k < n; ++k) f.more += m_rows[size_t(k)].count;
    f.height = f.rows * h1 + mh - m_gap;
    return f;
}

int HitList::shownRows() const { return fitFor(height()).rows; }
bool HitList::twoLine() const { return fitFor(height()).two; }
int HitList::contentHeight() const { return fitFor(height()).height; }

QString HitList::moreText() const
{
    const Fit f = fitFor(height());
    if (!f.moreLine) return {};
    const double last = m_rows.back().lastIn;
    return tr("+%1 more in the next %2").arg(f.more).arg(showClockText(std::ceil(last - 1e-6)));
}

QSize HitList::sizeHint() const
{
    const int n = int(m_rows.size());
    return QSize(200, n > 0 ? n * rowHeight(true) - m_gap : 0);
}

QSize HitList::minimumSizeHint() const
{
    return QSize(0, 0);
}

void HitList::paintEvent(QPaintEvent *)
{
    const Fit f = fitFor(height());
    if (f.rows == 0 && !f.moreLine) return;
    QPainter p(this);
    const QFontMetrics leadFm(m_leadFont), titleFm(m_titleFont), detailFm(m_detailFont);
    const int leadW = leadFm.horizontalAdvance(QStringLiteral("8:88.8")) + 4;
    const int colGap = std::max(8, m_gap * 2);
    const int x0 = leadW + colGap;
    const int w = std::max(0, width() - x0);
    const int rh = rowHeight(f.two);
    const QString sep = QStringLiteral("  ·  ");
    int y = 0;
    for (int k = 0; k < f.rows; ++k, y += rh) {
        const auto &row = m_rows[size_t(k)];
        const int rowH = rh - m_gap;
        // The countdown: on the title's baseline, right-aligned in its column.
        const int titleBase = f.two ? y + titleFm.ascent() : y + (rowH - titleFm.height()) / 2 + titleFm.ascent();
        p.setFont(m_leadFont);
        p.setPen(!m_armed ? m_muted : row.paused ? m_pausedColour : m_leadColour);
        p.drawText(leadW - leadFm.horizontalAdvance(row.when), f.two ? y + (rowH - leadFm.height()) / 2 + leadFm.ascent() : titleBase,
                   row.when);
        if (f.two) {
            p.setFont(m_titleFont);
            p.setPen(m_titleColour);
            p.drawText(QRect(x0, y, w, titleFm.height()), Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine,
                       titleFm.elidedText(row.title, Qt::ElideRight, w));
            if (!row.detail.isEmpty()) {
                p.setFont(m_detailFont);
                p.setPen(m_detailColour);
                p.drawText(QRect(x0, y + titleFm.height(), w, detailFm.height()),
                           Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine,
                           detailFm.elidedText(row.detail, Qt::ElideRight, w));
            }
            continue;
        }
        // One line: the title keeps its own width (up to most of the row),
        // the action takes the rest and elides first.
        const int baseline = titleBase;
        int titleW = titleFm.horizontalAdvance(row.title);
        if (!row.detail.isEmpty()) titleW = std::min(titleW, int(w * 0.6));
        titleW = std::min(titleW, w);
        p.setFont(m_titleFont);
        p.setPen(m_titleColour);
        p.drawText(x0, baseline, titleFm.elidedText(row.title, Qt::ElideRight, titleW));
        if (!row.detail.isEmpty()) {
            const int sepW = detailFm.horizontalAdvance(sep);
            const int rest = w - titleW - sepW;
            if (rest > detailFm.averageCharWidth() * 4) {
                p.setFont(m_detailFont);
                p.setPen(m_detailColour);
                p.drawText(x0 + titleW, baseline, sep + detailFm.elidedText(row.detail, Qt::ElideRight, rest));
            }
        }
    }
    if (f.moreLine) {
        p.setFont(m_detailFont);
        p.setPen(m_muted);
        p.drawText(QRect(x0, y, w, detailFm.height()), Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine,
                   detailFm.elidedText(moreText(), Qt::ElideRight, w));
    }
}

// ── ShowModeView ────────────────────────────────────────────────────────

ShowModeView::ShowModeView(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("showModeView"));
    setFocusPolicy(Qt::NoFocus);
    setAttribute(Qt::WA_StyledBackground);
    // An explicit (small) minimum: the cards fold to fit whatever height
    // there is, so the layout's own minimum must never be what stops the
    // window shrinking, or grow a window that was asked to be smaller.
    setMinimumSize(480, 320);
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
    // Columns: edge · lead · title · (detail, compact only) · trail.
    g->addWidget(r.edge, 0, 0, withBar ? 3 : 2, 1);
    if (withBar) {
        // A running song: the name gets the whole first line — it's what
        // the SM reads — with the time and the remaining under it, then the bar.
        g->addWidget(r.lead, 0, 1, Qt::AlignVCenter);
        g->addWidget(r.title, 0, 2, 1, 3);
        g->addWidget(r.detail, 1, 2);
        g->addWidget(r.trail, 1, 4, Qt::AlignVCenter);
        r.bar = new ThinProgressBar(r.frame);
        g->addWidget(r.bar, 2, 1, 1, 4);
    } else {
        g->addWidget(r.lead, 0, 1, 2, 1, Qt::AlignVCenter);
        g->addWidget(r.title, 0, 2);
        g->addWidget(r.detail, 1, 2);
        g->addWidget(r.trail, 0, 4, 2, 1, Qt::AlignVCenter);
    }
    g->setColumnStretch(2, 1);
    return r;
}

void ShowModeView::setRowVisible(Row &r, bool on)
{
    r.frame->setVisible(on);
}

// Compact: "14  Act 1 curtain  ·  House to half…" on one line — the title
// keeps its own width (shrinking only when it alone is too long), the
// detail takes the rest and elides first.
void ShowModeView::setRowCompact(Row &r, bool compact)
{
    if (r.frame->property("compact").toBool() == compact) return;
    r.frame->setProperty("compact", compact);
    auto *g = static_cast<QGridLayout *>(r.frame->layout());
    g->removeWidget(r.detail);
    if (compact) {
        g->addWidget(r.detail, 0, 3);
        r.title->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        g->setColumnStretch(2, 0);
        g->setColumnStretch(3, 1);
    } else {
        g->addWidget(r.detail, 1, 2);
        r.title->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        g->setColumnStretch(2, 1);
        g->setColumnStretch(3, 0);
    }
    r.detail->setContentsMargins(compact ? 6 : 0, 0, 0, 0);
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
    // An eliding heading: a narrow card shortens "NEXT LIGHTING HIT" rather
    // than pushing its chip out of the band.
    auto *h = new ElideLabel(b.frame);
    h->setText(heading);
    h->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    b.heading = h;
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
    m_header = header;

    // ── Body: the stage column + the transport column ──
    auto *body = new QHBoxLayout;
    body->setSpacing(12);
    root->addLayout(body, 1);

    // The stage: STANDBY and COMING UP take the height they need (fitStage
    // folds them when the screen is short), the lower row gets the rest.
    auto *stage = new QVBoxLayout;
    stage->setSpacing(10);
    body->addLayout(stage, 1);
    m_stage = stage;

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
        v->setContentsMargins(18, 10, 18, 10);
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
        v->addSpacing(4);
        v->addWidget(m_standbyNotes);

        auto *empty = elide(m_standbyCard, "standbyEmpty", 2);
        empty->setText(tr("End of the cue list — nothing on standby."));
        empty->setObjectName(QStringLiteral("smStandbyEmpty"));
        m_standbyEmpty = empty;
        v->addWidget(m_standbyEmpty);
        v->addStretch(1);
    }
    stage->addWidget(m_standbyCard, 1);

    // Coming up.
    m_comingCard = card(this, "smComingCard", "coming");
    m_comingCard->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    {
        auto *outer = new QVBoxLayout(m_comingCard);
        outer->setContentsMargins(0, 0, 0, 0);
        outer->setSpacing(0);
        auto band = makeBand(m_comingCard, "coming", tr("COMING UP"));
        m_comingCaps = band.heading;
        m_comingMore = new QLabel(m_comingCard);
        m_comingMore->setObjectName(QStringLiteral("smComingMore"));
        m_comingMore->setProperty("role", "chip");
        m_comingMore->setProperty("region", "coming");
        m_comingMore->hide();
        band.lay->addWidget(m_comingMore);
        outer->addWidget(band.frame);
        auto *v = new QVBoxLayout;
        v->setContentsMargins(18, 8, 18, 8);
        v->setSpacing(5);
        outer->addLayout(v, 1);
        for (int i = 0; i < kComingUpRows; ++i) {
            m_comingRows.push_back(makeRow(m_comingCard, false, "coming"));
            v->addWidget(m_comingRows.back().frame);
        }
        m_comingEmpty = new QLabel(tr("Nothing after this."), m_comingCard);
        m_comingEmpty->setProperty("role", "quiet");
        v->addWidget(m_comingEmpty);
    }
    stage->addWidget(m_comingCard, 0);

    // Lower row: Now playing | Next lighting hit | Lighting desk. The hits
    // card is the widest: its rows carry a countdown, a name and an action.
    // Spare height goes mostly here (1:3 against STANDBY): it's where the
    // hit list grows another row.
    m_lowerRow = new QHBoxLayout;
    m_lowerRow->setSpacing(10);
    stage->addLayout(m_lowerRow, 3);

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
    m_lowerRow->addWidget(m_runningCard, 4);

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
        v->setContentsMargins(16, 4, 16, 6);
        v->setSpacing(3);
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
        v->addSpacing(3);
        // The rows after the headline: as many as fit whole, then "+N more".
        m_hitList = new HitList(m_hitsCard);
        m_hitList->setObjectName(QStringLiteral("smHitList"));
        v->addWidget(m_hitList);
        m_hitsState = elide(m_hitsCard, "quiet", 2);
        m_hitsState->setObjectName(QStringLiteral("smHitsState"));
        v->addWidget(m_hitsState);
        v->addStretch(1);
    }
    m_lowerRow->addWidget(m_hitsCard, 5);

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
                 QString::number(px(96, s)),       // 16 standby number
                 QString::number(px(28, s)),       // 17 standby name
                 QString::number(px(14, s)))       // 18 meta
            .arg(QString::number(px(20, s)),       // 19 notes
                 QString::number(px(22, s)),       // 20 row lead
                 QString::number(px(17, s)),       // 21 row title
                 QString::number(px(26, s)),       // 22 row trail
                 tk.warn.name(),                   // 23
                 QString::number(px(40, s)),       // 24 hit countdown
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
        const int col = std::clamp(int(width() * 0.19), 170, 340);
        m_transport->setFixedWidth(col);
        m_standbyEdge->setFixedWidth(px(8, s));
        m_deskDot->setFixedSize(px(10, s), px(10, s));
        m_deskBar->setThickness(px(5, s));
        for (auto &r : m_runningRows) r.bar->setThickness(px(4, s));
        for (auto &r : m_comingRows) r.edge->setFixedWidth(px(4, s));
        for (auto &r : m_runningRows) r.edge->setFixedWidth(px(4, s));
        for (auto *lay : m_bandLayouts) lay->setContentsMargins(px(16, s), px(5, s), px(12, s), px(5, s));
        m_stage->setSpacing(px(10, s));
        m_lowerRow->setSpacing(px(10, s));

        // The hit list paints its own text: the row faces follow the rows'
        // stylesheet sizes (lead in mono, like every other countdown).
        QFont lead = font();
        lead.setFamilies({QStringLiteral("JetBrains Mono"), QStringLiteral("Cascadia Mono"), QStringLiteral("Consolas")});
        lead.setStyleHint(QFont::Monospace);
        lead.setPixelSize(px(17, s));
        lead.setWeight(QFont::Bold);
        QFont title = font();
        title.setPixelSize(px(17, s));
        title.setWeight(QFont::DemiBold);
        QFont detail = font();
        detail.setPixelSize(px(13, s));
        m_hitList->setFonts(lead, title, detail);
        m_hitList->setColours(rc.hits, tk.ink100, tk.ink40, tk.ink40, tk.warn);
        m_hitList->setRowGap(px(2, s));
    }
    // Lead columns size to their widest plausible text so the titles line up
    // (everything polished first, so every metric below — and the fit's —
    // is the stylesheet's font, even before the first show).
    for (auto *w : findChildren<QWidget *>()) w->ensurePolished();
    const QFontMetrics lead(m_comingRows.front().lead->font());
    const int leadW = lead.horizontalAdvance(QStringLiteral("888.8"));
    for (auto &r : m_comingRows) r.lead->setFixedWidth(leadW);
    for (auto &r : m_runningRows) r.lead->setFixedWidth(leadW);
    const QFontMetrics trail(m_runningRows.front().trail->font());
    for (auto &r : m_runningRows) r.trail->setFixedWidth(trail.horizontalAdvance(QStringLiteral("-88:88")) + 4);
    fitStage();
}

// Fold the stage column to the height there is. STANDBY shows the notes
// lines its text needs (up to 3, 4 on a tall screen) and COMING UP its
// rows; the lower row keeps a floor it can't be squeezed under. When the
// two don't fit above that floor, give way in this order: coming-up rows
// go to one line, the notes lose a line, a coming-up row goes ("+1 more"
// on its band), and so on — never a line cut in half.
void ShowModeView::fitStage()
{
    if (m_fitting || !m_hitList) return;
    m_fitting = true;
    const double s = m_scale;
    const auto rm = layout()->contentsMargins();

    // Sub-layouts cache their hints until the next activation, and each
    // widget's layout item caches the widget's hint until that widget's
    // updateGeometry() — so a row whose own grid was just rearranged still
    // reports its old height to the card. Flush each card's whole tree
    // first, so every measurement below is of what's there now.
    auto invalidateDeep = [](auto &&self, QLayout *l) -> void {
        for (int i = 0; i < l->count(); ++i) {
            QLayoutItem *it = l->itemAt(i);
            if (auto *sub = it->layout()) {
                self(self, sub);
            } else if (auto *w = it->widget()) {
                if (w->layout()) self(self, w->layout());
                w->updateGeometry();
            }
        }
        l->invalidate();
    };
    // Widths, worked out rather than read: before the first show nothing
    // has been laid out yet, and the answer must be right from the start.
    const int stageW = std::max(300, width() - rm.left() - rm.right() - 12 - m_transport->minimumWidth());
    const int lowerW = stageW - 2 * m_stage->spacing();
    // (A little under the real width: a line that only just fits is better
    // given its own line than elided.)
    const int notesW = std::max(80, stageW - 2 - m_standbyEdge->minimumWidth() - 36 - 12);
    const int hitsW  = std::max(80, lowerW * 5 / 13 - 34);
    const int deskW  = std::max(80, lowerW * 4 / 13 - 34);
    const int notesCap = height() >= 1000 ? 4 : 3;
    const int notesNeed = m_standbyNotes->isHidden() ? 0 : m_standbyNotes->linesNeeded(notesW, notesCap);
    const int comingHave = int(std::min<size_t>(m_snap.comingUp.size(), m_comingRows.size()));
    const int hitsStateLines = m_hitsState->linesNeeded(hitsW, 2);
    const int deskDetailLines = m_deskDetail->linesNeeded(deskW, 2);

    // Nothing that feeds the answer has changed since last time: done. This
    // runs after every 100 ms render, so it has to be cheap in the common
    // case — the key is built from the snapshot, not from layout queries.
    const auto &d = m_snap.desk;
    const QString key = QStringLiteral("%1 %2 %3 %4 %5 %6 | %7 %8 %9 %10 %11 %12 %13 %14 %15 %16")
        .arg(width()).arg(height()).arg(notesNeed).arg(comingHave).arg(hitsStateLines).arg(deskDetailLines)
        .arg(m_scale)
        .arg(std::min<size_t>(m_snap.running.size(), m_runningRows.size())).arg(m_snap.lastFired.has_value())
        .arg(m_snap.hits.size() > 1).arg(m_snap.hits.empty()).arg(int(d.link)).arg(d.blind)
        .arg(d.activeLabel.isEmpty()).arg(m_snap.standby.has_value()).arg(m_hitsState->isHidden());
    if (key == m_fitKey) {
        m_fitting = false;
        return;
    }
    m_fitKey = key;
    for (QWidget *c : {m_standbyCard, m_comingCard, m_runningCard, m_hitsCard, m_deskCard})
        invalidateDeep(invalidateDeep, c->layout());

    // The two-line status labels take the lines they need, no more.
    m_hitsState->setMaxLines(hitsStateLines);
    m_deskDetail->setMaxLines(deskDetailLines);

    // The lower row's floor: enough for the band, the headline hit, three
    // one-line rows (and whatever the other two cards need). On a 720-high
    // screen this is what tips COMING UP to three rows rather than the hit
    // list to one — the hits are what a busy song is about.
    for (QWidget *c : {m_runningCard, m_hitsCard, m_deskCard}) c->setMinimumHeight(0);
    int lowerMin = px(240, s);
    for (QWidget *c : {m_runningCard, m_hitsCard, m_deskCard})
        lowerMin = std::max(lowerMin, c->minimumSizeHint().height());
    for (QWidget *c : {m_runningCard, m_hitsCard, m_deskCard}) c->setMinimumHeight(lowerMin);

    const int headerH = m_header->sizeHint().height();
    const int avail = height() - rm.top() - rm.bottom() - headerH - layout()->spacing()
                      - 2 * m_stage->spacing() - lowerMin;

    auto apply = [&](const StageFit &f) {
        m_standbyNotes->setMaxLines(std::max(1, std::min(f.notesLines, std::max(1, notesNeed))));
        for (size_t i = 0; i < m_comingRows.size(); ++i) {
            setRowCompact(m_comingRows[i], f.comingCompact);
            setRowVisible(m_comingRows[i], int(i) < std::min(f.comingRows, comingHave));
        }
        const int dropped = std::max(0, comingHave - f.comingRows);
        m_comingMore->setText(tr("+%1 more").arg(dropped));
        m_comingMore->setVisible(dropped > 0);
    };
    auto fits = [&] {
        invalidateDeep(invalidateDeep, m_standbyCard->layout());
        invalidateDeep(invalidateDeep, m_comingCard->layout());
        return m_standbyCard->sizeHint().height() + m_comingCard->sizeHint().height() <= avail;
    };

    const StageFit steps[] = {
        {notesCap, false, 4}, {notesCap, true, 4}, {2, true, 4}, {2, true, 3},
        {1, true, 3}, {1, true, 2}, {1, true, 1}, {1, true, 0},
    };
    StageFit chosen = steps[std::size(steps) - 1];
    for (const auto &f : steps) {
        apply(f);
        if (fits()) {
            chosen = f;
            break;
        }
    }
    apply(chosen);
    m_fit = chosen;
    m_fitting = false;
}

// ── Rendering ───────────────────────────────────────────────────────────

void ShowModeView::render()
{
    // A resize while hidden (the usual way in: resized, then shown) never
    // reaches resizeEvent; make sure the scale matches the size first, so
    // the fit below measures with the right fonts.
    const double s = std::clamp(std::min(width() / 1280.0, height() / 720.0), 0.7, 3.0);
    if (!qFuzzyCompare(s, m_scale)) applyScale();
    renderHeader();
    renderStandby();
    renderComingUp();
    renderRunning();
    renderHits();
    renderDesk();
    renderTransport();
    fitStage();
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
    m_standbyNotes->setVisible(!c.notes.trimmed().isEmpty());   // no empty line reserved
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
        // Rows past what the height allows stay hidden (fitStage decides).
        if (i >= list.size() || int(i) >= m_fit.comingRows) {
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
        // The time, and PAUSED when it is; a loop is already the "∞" on the right.
        QStringList d;
        d << showClockText(rc.elapsedSeconds);
        if (rc.paused) d << tr("PAUSED");
        r.detail->setText(d.join(QStringLiteral(" · ")));
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
    // Everything after the headline, grouped and fitted by the list itself.
    m_hitList->setHits(hits.size() > 1 ? std::vector<ShowUpcomingHit>(hits.begin() + 1, hits.end())
                                       : std::vector<ShowUpcomingHit>{},
                       armed);
    m_hitList->setVisible(hits.size() > 1);
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
