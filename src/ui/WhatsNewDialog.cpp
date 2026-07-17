#include "ui/WhatsNewDialog.h"
#include "ui/Theme.h"

#include <QApplication>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QVBoxLayout>

namespace quewi::ui {

namespace {

struct Highlight { const char *title; const char *body; };

// Curated highlights for this release. Written to read like a person wrote
// them — specific, second-person, no filler. Update this list each release.
//
// 1.0 note: many users are arriving from 0.9.103 or earlier (the updater bug
// kept them there), so this list keeps the best of the late-0.9 work alongside
// the 1.0 headline — for them it's all new.
const Highlight kHighlights[] = {
    { "quewi Mix — your console, on cues",
      "TheatreMix-style DCA mixing, built in. Every cue puts the right mics on "
      "the right faders and mutes the rest, and the grid highlights exactly what "
      "each GO will change. Speaks Behringer X32/M32 and Yamaha DM7 — and it "
      "never touches your fader levels. The mix stays yours." },
    { "Channels & ensembles",
      "Name each mic once — strip, name, actor, backup — then group them "
      "(“Ensemble Women”, “Orchestra”) to assign twenty mics in one cell. Edit "
      "the group and every cue that uses it follows." },
    { "Sharper, everywhere",
      "A front-to-back design pass: the beveled boxes, stock-blue selections and "
      "off-palette corners are gone. Every control and hand-drawn view now takes "
      "its colours from one warm palette — in all five themes." },
    { "Soundboard layers",
      "Stack pages of pads behind one board and flip between them mid-show — "
      "Act 1, Act 2, spot FX. Switchable from the board or over OSC." },
    { "Auto-follow, the way it should be",
      "Auto-continue fires the next cue the instant you hit GO; auto-follow waits "
      "for the track to actually finish, then continues." },
    { "Scrub the waveform",
      "Click or drag anywhere on a cue's waveform to move the playhead, with a "
      "live marker that follows playback." },
    { "Run the whole show from OSC",
      "Controllers like HeliOSC can fire and navigate cues, ride a live mix "
      "(level / pan / seek), switch lists and soundboard layers, dial in a cue's "
      "EQ and compressor, and edit any cue — remotely." },
    { "Steadier on its feet",
      "A top-to-bottom crash audit closed a class of rare-but-real crashes across "
      "effects, undo, shutdown, and malformed network input. It should just keep "
      "running." },
};

} // namespace

WhatsNewDialog::WhatsNewDialog(QWidget *parent) : QDialog(parent)
{
    const auto &tk = Theme::tokens();
    setWindowTitle(tr("What's new"));
    setModal(true);
    setMinimumWidth(560);
    setStyleSheet(QStringLiteral("QDialog{background:%1;}").arg(tk.bgDeep.name()));

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ── Header band ─────────────────────────────────────────────────────
    auto *header = new QWidget(this);
    header->setStyleSheet(QStringLiteral("background:%1;").arg(tk.bgPanel.name()));
    auto *hl = new QHBoxLayout(header);
    hl->setContentsMargins(24, 22, 24, 22);
    hl->setSpacing(16);

    auto *icon = new QLabel(header);
    QPixmap logo(QStringLiteral(":/icons/quewi.png"));
    if (!logo.isNull())
        icon->setPixmap(logo.scaled(48, 48, Qt::KeepAspectRatio,
                                    Qt::SmoothTransformation));
    hl->addWidget(icon, 0, Qt::AlignTop);

    auto *titleCol = new QVBoxLayout();
    titleCol->setSpacing(2);
    auto *title = new QLabel(tr("What's new"), header);
    title->setStyleSheet(QStringLiteral(
        "color:%1; font-size:22px; font-weight:800;").arg(tk.ink100.name()));
    auto *sub = new QLabel(tr("quewi %1").arg(QApplication::applicationVersion()), header);
    sub->setStyleSheet(QStringLiteral(
        "color:%1; font-size:12px; font-weight:700; letter-spacing:0.12em;")
        .arg(tk.accent.name()));
    titleCol->addWidget(title);
    titleCol->addWidget(sub);
    hl->addLayout(titleCol);
    hl->addStretch(1);
    root->addWidget(header);

    auto *rule = new QFrame(this);
    rule->setFixedHeight(2);
    rule->setStyleSheet(QStringLiteral("background:%1;").arg(tk.accent.name()));
    root->addWidget(rule);

    // ── Highlights (scrolls if it overflows) ────────────────────────────
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setStyleSheet(QStringLiteral(
        "QScrollArea{background:%1;border:none;}").arg(tk.bgDeep.name()));

    auto *body = new QWidget(scroll);
    body->setStyleSheet(QStringLiteral("background:%1;").arg(tk.bgDeep.name()));
    auto *bl = new QVBoxLayout(body);
    bl->setContentsMargins(24, 20, 24, 20);
    bl->setSpacing(18);

    for (const auto &h : kHighlights) {
        auto *row = new QWidget(body);
        auto *rl = new QHBoxLayout(row);
        rl->setContentsMargins(0, 0, 0, 0);
        rl->setSpacing(14);

        auto *bar = new QFrame(row);   // accent tick down the left of each item
        bar->setFixedWidth(3);
        bar->setStyleSheet(QStringLiteral(
            "background:%1; border-radius:1.5px;").arg(tk.accent.name()));
        rl->addWidget(bar);

        auto *col = new QVBoxLayout();
        col->setSpacing(3);
        auto *t = new QLabel(tr(h.title), row);
        t->setStyleSheet(QStringLiteral(
            "color:%1; font-size:15px; font-weight:700;").arg(tk.ink100.name()));
        auto *b = new QLabel(tr(h.body), row);
        b->setWordWrap(true);
        b->setStyleSheet(QStringLiteral(
            "color:%1; font-size:13px;").arg(tk.ink60.name()));
        col->addWidget(t);
        col->addWidget(b);
        rl->addLayout(col, 1);

        bl->addWidget(row);
    }
    bl->addStretch(1);
    scroll->setWidget(body);
    root->addWidget(scroll, 1);

    // ── Footer ──────────────────────────────────────────────────────────
    auto *footer = new QWidget(this);
    footer->setStyleSheet(QStringLiteral("background:%1;").arg(tk.bgPanel.name()));
    auto *fl = new QHBoxLayout(footer);
    fl->setContentsMargins(24, 14, 24, 14);
    fl->setSpacing(12);

    auto *notes = new QLabel(footer);
    notes->setText(QStringLiteral(
        "<a href='https://github.com/ServeGaming/quewi/releases' "
        "style='color:%1; text-decoration:none;'>%2</a>")
        .arg(tk.ink60.name(), tr("Full release notes")));
    notes->setOpenExternalLinks(true);
    notes->setStyleSheet(QStringLiteral("font-size:12px;"));
    fl->addWidget(notes);
    fl->addStretch(1);

    auto *ok = new QPushButton(tr("Got it"), footer);
    ok->setCursor(Qt::PointingHandCursor);
    ok->setDefault(true);
    ok->setStyleSheet(QStringLiteral(
        "QPushButton{background:%1; color:%2; border:none; border-radius:6px;"
        "  padding:8px 24px; font-weight:700;}"
        "QPushButton:hover{background:%3;}")
        .arg(tk.accent.name(), tk.inkOnAccent.name(), tk.accentHover.name()));
    connect(ok, &QPushButton::clicked, this, &QDialog::accept);
    fl->addWidget(ok);
    root->addWidget(footer);

    resize(560, 620);
}

bool WhatsNewDialog::maybeShowForThisVersion(QWidget *parent)
{
    QSettings s(QStringLiteral("ServeGaming"), QStringLiteral("quewi"));
    const QString current   = QApplication::applicationVersion();
    const QString lastShown = s.value(QStringLiteral("ui/whatsNewVersion")).toString();

    // Record the running version no matter what, so the sheet shows at most
    // once per version and never re-pops on the next launch.
    s.setValue(QStringLiteral("ui/whatsNewVersion"), current);

    // Quiet on a first-ever install (nothing recorded yet) and when the
    // version is unchanged — only an actual update pops the sheet.
    if (lastShown.isEmpty() || lastShown == current) return false;

    WhatsNewDialog dlg(parent);
    dlg.exec();
    return true;
}

} // namespace quewi::ui
