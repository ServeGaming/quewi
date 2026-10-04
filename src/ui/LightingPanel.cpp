#include "ui/LightingPanel.h"

#include "ui/ShowModeView.h"   // ElideLabel, ThinProgressBar, the text helpers
#include "ui/Theme.h"

#include <QBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QStyle>
#include <QTimer>

namespace quewi::ui {

namespace {

constexpr int kPollMs = 100;
constexpr int kHitRows = 3;      // after the headline hit

QString rgba(const QColor &c, int alpha)
{
    return QStringLiteral("rgba(%1,%2,%3,%4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(alpha);
}

QLabel *caps(QWidget *parent, const QString &text)
{
    auto *l = new QLabel(text, parent);
    l->setProperty("role", "caps");
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

void setTone(QWidget *w, const char *tone)
{
    if (w->property("tone").toString() == QLatin1String(tone)) return;
    w->setProperty("tone", QLatin1String(tone));
    w->style()->unpolish(w);
    w->style()->polish(w);
}

} // namespace

LightingPanel::LightingPanel(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("lightingPanel"));
    setFocusPolicy(Qt::NoFocus);
    setAttribute(Qt::WA_StyledBackground);
    buildUi();
    applyStyle();
    m_tick = new QTimer(this);
    m_tick->setInterval(kPollMs);
    m_tick->setTimerType(Qt::PreciseTimer);
    connect(m_tick, &QTimer::timeout, this, [this] {
        if (m_provider) setSnapshot(m_provider());
    });
    render();
}

void LightingPanel::setSnapshotProvider(std::function<ShowSnapshot()> provider)
{
    m_provider = std::move(provider);
    if (m_provider && isVisible()) setSnapshot(m_provider());
}

void LightingPanel::setSnapshot(const ShowSnapshot &s)
{
    m_snap = s;
    render();
}

void LightingPanel::showEvent(QShowEvent *)
{
    if (m_provider) setSnapshot(m_provider());
    m_tick->start();
}

void LightingPanel::hideEvent(QHideEvent *)
{
    m_tick->stop();
}

void LightingPanel::resizeEvent(QResizeEvent *e)
{
    QWidget::resizeEvent(e);
    relayout();
}

// ── Building ────────────────────────────────────────────────────────────

LightingPanel::HitRow LightingPanel::makeHitRow(QWidget *parent)
{
    HitRow r;
    r.frame = new QWidget(parent);
    auto *g = new QGridLayout(r.frame);
    g->setContentsMargins(0, 0, 0, 0);
    g->setHorizontalSpacing(8);
    g->setVerticalSpacing(0);
    r.when = elide(r.frame, "hitWhen");
    r.when->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    r.when->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    r.name = elide(r.frame, "hitRowName");
    r.does = elide(r.frame, "hitRowDoes");
    g->addWidget(r.when, 0, 0, 2, 1, Qt::AlignVCenter);
    g->addWidget(r.name, 0, 1);
    g->addWidget(r.does, 1, 1);
    g->setColumnStretch(1, 1);
    return r;
}

void LightingPanel::buildUi()
{
    m_box = new QBoxLayout(QBoxLayout::TopToBottom, this);
    m_box->setContentsMargins(12, 10, 12, 10);
    m_box->setSpacing(14);

    // ── Desk: link dot, name, address / detail, settings ──
    m_deskBlock = new QWidget(this);
    m_deskBlock->setObjectName(QStringLiteral("lpDeskBlock"));
    {
        auto *v = new QVBoxLayout(m_deskBlock);
        v->setContentsMargins(0, 0, 0, 0);
        v->setSpacing(3);
        auto *top = new QHBoxLayout;
        top->setSpacing(8);
        m_deskDot = new QLabel(m_deskBlock);
        m_deskDot->setObjectName(QStringLiteral("lpDeskDot"));
        m_deskDot->setFixedSize(9, 9);
        m_deskLink = elide(m_deskBlock, "deskLink");
        m_deskLink->setObjectName(QStringLiteral("lpDeskLink"));
        m_settings = new QPushButton(tr("Desk settings…"), m_deskBlock);
        m_settings->setObjectName(QStringLiteral("lpDeskSettings"));
        m_settings->setFocusPolicy(Qt::NoFocus);
        m_settings->setCursor(Qt::PointingHandCursor);
        connect(m_settings, &QPushButton::clicked, this, &LightingPanel::deskSettingsRequested);
        top->addWidget(m_deskDot);
        top->addWidget(m_deskLink, 1);
        top->addWidget(m_settings);
        v->addLayout(top);
        m_deskDetail = elide(m_deskBlock, "quiet", 2);
        m_deskDetail->setObjectName(QStringLiteral("lpDeskDetail"));
        v->addWidget(m_deskDetail);
        v->addStretch(1);      // top-aligned when the blocks sit in a row
    }
    m_box->addWidget(m_deskBlock);

    // ── The desk's cues: ACTIVE with progress, PENDING ──
    m_cueBlock = new QWidget(this);
    m_cueBlock->setObjectName(QStringLiteral("lpCueBlock"));
    {
        auto *v = new QVBoxLayout(m_cueBlock);
        v->setContentsMargins(0, 0, 0, 0);
        v->setSpacing(2);
        m_activeCaps = caps(m_cueBlock, tr("ACTIVE"));
        v->addWidget(m_activeCaps);
        auto *line = new QHBoxLayout;
        line->setSpacing(10);
        m_active = new QLabel(m_cueBlock);
        m_active->setObjectName(QStringLiteral("lpDeskActive"));
        m_active->setProperty("role", "deskActive");
        m_activeTime = new QLabel(m_cueBlock);
        m_activeTime->setObjectName(QStringLiteral("lpDeskActiveTime"));
        m_activeTime->setProperty("role", "deskTime");
        m_activeTime->setAlignment(Qt::AlignRight | Qt::AlignBottom);
        line->addWidget(m_active);
        line->addStretch(1);
        line->addWidget(m_activeTime);
        v->addLayout(line);
        m_activeLabel = elide(m_cueBlock, "deskLabel");
        m_activeLabel->setObjectName(QStringLiteral("lpDeskActiveLabel"));
        v->addWidget(m_activeLabel);
        m_activeBar = new ThinProgressBar(m_cueBlock);
        m_activeBar->setObjectName(QStringLiteral("lpDeskBar"));
        m_activeBar->setThickness(4);
        v->addSpacing(2);
        v->addWidget(m_activeBar);
        v->addSpacing(6);
        auto *pend = new QHBoxLayout;
        pend->setSpacing(8);
        m_pendingCaps = caps(m_cueBlock, tr("PENDING"));
        m_pending = elide(m_cueBlock, "deskPending");
        m_pending->setObjectName(QStringLiteral("lpDeskPending"));
        pend->addWidget(m_pendingCaps);
        pend->addWidget(m_pending, 1);
        v->addLayout(pend);
        m_blind = new QLabel(tr("DESK IS IN BLIND"), m_cueBlock);
        m_blind->setObjectName(QStringLiteral("lpDeskBlind"));
        m_blind->setProperty("role", "blind");
        m_blind->setAlignment(Qt::AlignCenter);
        v->addWidget(m_blind);
        m_cueEmpty = new QLabel(m_cueBlock);
        m_cueEmpty->setObjectName(QStringLiteral("lpDeskEmpty"));
        m_cueEmpty->setProperty("role", "quiet");
        m_cueEmpty->setWordWrap(true);
        v->addWidget(m_cueEmpty);
        v->addStretch(1);
    }
    m_box->addWidget(m_cueBlock);

    // ── Lighting hits: Armed switch, the next hit big, then a few more ──
    m_hitsBlock = new QWidget(this);
    m_hitsBlock->setObjectName(QStringLiteral("lpHitsBlock"));
    {
        auto *v = new QVBoxLayout(m_hitsBlock);
        v->setContentsMargins(0, 0, 0, 0);
        v->setSpacing(3);
        auto *top = new QHBoxLayout;
        top->setSpacing(8);
        top->addWidget(caps(m_hitsBlock, tr("LIGHTING HITS")));
        top->addStretch(1);
        m_armed = new QPushButton(m_hitsBlock);
        m_armed->setObjectName(QStringLiteral("lpArmed"));
        m_armed->setCheckable(true);
        m_armed->setFocusPolicy(Qt::NoFocus);
        m_armed->setCursor(Qt::PointingHandCursor);
        m_armed->setToolTip(tr("Lighting triggers armed — click to disarm / arm"));
        connect(m_armed, &QPushButton::toggled, this, [this](bool on) {
            if (on != m_snap.triggersArmed) emit armedToggled(on);
        });
        top->addWidget(m_armed);
        v->addLayout(top);
        auto *head = new QHBoxLayout;
        head->setSpacing(10);
        m_hitCountdown = new QLabel(m_hitsBlock);
        m_hitCountdown->setObjectName(QStringLiteral("lpHitCountdown"));
        m_hitCountdown->setProperty("role", "hitCountdown");
        m_hitCountdown->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        auto *headText = new QVBoxLayout;
        headText->setSpacing(0);
        m_hitName = elide(m_hitsBlock, "hitName");
        m_hitName->setObjectName(QStringLiteral("lpHitName"));
        m_hitDoes = elide(m_hitsBlock, "hitDoes", 2);
        m_hitDoes->setObjectName(QStringLiteral("lpHitDoes"));
        headText->addWidget(m_hitName);
        headText->addWidget(m_hitDoes);
        head->addWidget(m_hitCountdown);
        head->addLayout(headText, 1);
        v->addLayout(head);
        v->addSpacing(4);
        for (int i = 0; i < kHitRows; ++i) {
            m_hitRows.push_back(makeHitRow(m_hitsBlock));
            v->addWidget(m_hitRows.back().frame);
        }
        m_hitsState = new QLabel(m_hitsBlock);
        m_hitsState->setObjectName(QStringLiteral("lpHitsState"));
        m_hitsState->setProperty("role", "quiet");
        m_hitsState->setWordWrap(true);
        v->addWidget(m_hitsState);
        v->addStretch(1);
    }
    m_box->addWidget(m_hitsBlock, 1);
}

void LightingPanel::applyStyle()
{
    const auto &tk = Theme::tokens();
    const QString mono = QStringLiteral("'JetBrains Mono','Cascadia Mono','Consolas',monospace");
    setStyleSheet(QStringLiteral(
        "QWidget#lightingPanel { background:%1; }"
        "QLabel { background:transparent; }"
        "QLabel[role=\"caps\"] { color:%2; font-size:10px; font-weight:700; letter-spacing:0.15em; }"
        "QLabel[role=\"quiet\"] { color:%2; font-size:11px; }"
        "QLabel[role=\"quiet\"][tone=\"warn\"] { color:%3; }"
        "QLabel[role=\"deskLink\"] { color:%4; font-size:13px; font-weight:600; }"
        "QPushButton#lpDeskSettings { color:%2; background:transparent; border:1px solid %5;"
        "  border-radius:3px; font-size:11px; padding:2px 8px; }"
        "QPushButton#lpDeskSettings:hover { color:%4; background:%6; }"
        "QLabel[role=\"deskActive\"] { color:%4; font-size:26px; font-weight:800; font-family:%7; }"
        "QLabel[role=\"deskTime\"] { color:%8; font-size:13px; font-family:%7; }"
        "QLabel[role=\"deskLabel\"] { color:%8; font-size:13px; }"
        "QLabel[role=\"deskPending\"] { color:%8; font-size:13px; font-weight:600; }"
        "QLabel[role=\"blind\"] { color:%3; background:%9; border-radius:3px; font-size:11px;"
        "  font-weight:700; letter-spacing:0.1em; padding:2px; }"
        "QPushButton#lpArmed { color:%10; background:transparent; border:1px solid %10; border-radius:3px;"
        "  font-size:10px; font-weight:700; letter-spacing:0.1em; padding:2px 8px; }"
        "QPushButton#lpArmed:!checked { color:%3; border-color:%3; background:%9; }"
        "QLabel[role=\"hitCountdown\"] { color:%10; font-size:30px; font-weight:800; font-family:%7; }"
        "QLabel[role=\"hitCountdown\"][tone=\"paused\"] { color:%3; }"
        "QLabel[role=\"hitCountdown\"][tone=\"off\"] { color:%2; }"
        "QLabel[role=\"hitName\"] { color:%4; font-size:14px; font-weight:600; }"
        "QLabel[role=\"hitDoes\"] { color:%8; font-size:11px; }"
        "QLabel[role=\"hitWhen\"] { color:%4; font-size:13px; font-weight:700; font-family:%7; }"
        "QLabel[role=\"hitWhen\"][tone=\"paused\"] { color:%3; }"
        "QLabel[role=\"hitWhen\"][tone=\"off\"] { color:%2; }"
        "QLabel[role=\"hitRowName\"] { color:%4; font-size:12px; font-weight:600; }"
        "QLabel[role=\"hitRowDoes\"] { color:%8; font-size:11px; }")
        .arg(tk.bgPanel.name(),            // 1
             tk.ink40.name(),              // 2
             tk.warn.name(),               // 3
             tk.ink100.name(),             // 4
             tk.outline.name(),            // 5
             tk.bgInteractive.name(),      // 6
             mono,                         // 7
             tk.ink60.name(),              // 8
             rgba(tk.warn, 28))            // 9
        .arg(tk.accent.name()));           // 10
    // Polish first so the metrics are the stylesheet's font, not the default.
    for (auto &r : m_hitRows) r.when->ensurePolished();
    const QFontMetrics fm(m_hitRows.front().when->font());
    for (auto &r : m_hitRows) r.when->setFixedWidth(fm.horizontalAdvance(QStringLiteral("8:88.8")) + 4);
}

// Side docks stack the blocks; a bottom dock (wide and short) puts them in
// a row so the hits don't end up below the fold.
void LightingPanel::relayout()
{
    const bool wide = isWideLayout();
    if (wide == m_wide) return;
    m_wide = wide;
    m_box->setDirection(wide ? QBoxLayout::LeftToRight : QBoxLayout::TopToBottom);
    m_box->setSpacing(wide ? 24 : 14);
    m_box->setStretch(0, wide ? 3 : 0);
    m_box->setStretch(1, wide ? 4 : 0);
    m_box->setStretch(2, wide ? 5 : 1);
}

// ── Rendering ───────────────────────────────────────────────────────────

void LightingPanel::render()
{
    renderDesk();
    renderHits();
}

void LightingPanel::renderDesk()
{
    const auto &d = m_snap.desk;
    const auto &tk = Theme::tokens();
    const bool on = d.link != ShowDeskStatus::Link::Off;
    const bool live = d.link == ShowDeskStatus::Link::Live;

    QString link = showDeskLinkText(d.link);
    if (on && !d.deskName.isEmpty()) link = QStringLiteral("%1 · %2").arg(d.deskName, link);
    else if (!on && !d.deskName.isEmpty()) link = d.deskName;
    m_deskLink->setText(link);
    m_deskDot->setStyleSheet(QStringLiteral("background:%1; border-radius:4px;")
                                 .arg(showDeskLinkColour(d.link).name()));
    QStringList detail;
    if (on && !d.address.isEmpty()) detail << d.address;
    if (live && !d.showName.isEmpty()) detail << d.showName;
    if (!live && !d.detail.isEmpty()) detail << d.detail;
    if (!on) detail << tr("No desk feedback");
    m_deskDetail->setText(detail.join(QStringLiteral("  ·  ")));
    m_deskDetail->setVisible(!detail.isEmpty());

    m_activeCaps->setVisible(live);
    m_active->setVisible(live);
    m_activeTime->setVisible(live);
    m_activeLabel->setVisible(live);
    m_activeBar->setVisible(live);
    m_pendingCaps->setVisible(live);
    m_pending->setVisible(live);
    m_blind->setVisible(live && d.blind);
    m_cueEmpty->setVisible(!live);
    if (!live) {
        switch (d.link) {
        case ShowDeskStatus::Link::Off:
            m_cueEmpty->setText(tr("Turn on desk feedback to see the desk's active and pending cues here."));
            break;
        case ShowDeskStatus::Link::Connecting:
            m_cueEmpty->setText(tr("Waiting for the desk…"));
            break;
        case ShowDeskStatus::Link::Failed:
            m_cueEmpty->setText(tr("The desk's cues appear here once it can be reached."));
            break;
        case ShowDeskStatus::Link::Live:
            break;
        }
        return;
    }
    auto cueText = [](const QString &list, const QString &cue) -> QString {
        if (cue.isEmpty()) return QStringLiteral("—");
        return list.isEmpty() ? cue : QStringLiteral("%1/%2").arg(list, cue);
    };
    m_active->setText(cueText(d.activeList, d.activeCue));
    m_activeTime->setText(d.activeTime);
    m_activeLabel->setText(d.activeLabel);
    m_activeLabel->setVisible(!d.activeLabel.isEmpty());
    m_activeBar->setProgress(d.activeProgress);
    m_activeBar->setColour(d.activeProgress >= 0.999 ? tk.running : tk.accent);
    QString pending = cueText(d.pendingList, d.pendingCue);
    if (!d.pendingLabel.isEmpty()) pending += QStringLiteral("   ") + d.pendingLabel;
    m_pending->setText(pending);
}

void LightingPanel::renderHits()
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
    auto hitTitle = [](const ShowUpcomingHit &h) {
        QString name = h.name.isEmpty() ? h.does : h.name;
        return h.exit ? LightingPanel::tr("%1 (end)").arg(name) : name;
    };
    if (any) {
        const auto &h = hits.front();
        m_hitCountdown->setText(showCountdownText(h.inSeconds));
        m_hitName->setText(hitTitle(h));
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
            r.frame->hide();
            continue;
        }
        const auto &h = hits[k];
        r.when->setText(showCountdownText(h.inSeconds));
        setTone(r.when, !armed ? "off" : h.paused ? "paused" : "live");
        r.name->setText(hitTitle(h));
        r.does->setText(!h.name.isEmpty() ? h.does : QString());
        r.does->setVisible(!r.does->text().isEmpty());
        r.frame->show();
    }
    QString state;
    if (!armed)
        state = tr("Triggers disarmed — nothing is sent to the desk.");
    else if (any && hits.front().paused)
        state = tr("Paused — the countdown is frozen.");
    else if (!any)
        state = m_snap.running.empty() ? tr("Hits appear here while a song with lighting triggers plays.")
                                       : tr("No more hits in what's playing.");
    m_hitsState->setText(state);
    m_hitsState->setVisible(!state.isEmpty());
    setTone(m_hitsState, !armed ? "warn" : "quiet");
}

} // namespace quewi::ui
