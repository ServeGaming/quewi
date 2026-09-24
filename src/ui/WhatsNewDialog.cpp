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
// 1.0.2: effects presets + new effects, pad import, the updater fix. The
// 1.0.1 highlights follow for anyone jumping straight from 1.0.0.
const Highlight kHighlights[] = {
    { "Effects presets",
      "Open a cue in the audio editor and pick from 28 ready-made chains: "
      "Telephone, Megaphone, Next room, Cathedral, Monster, Chipmunk, Underwater "
      "and more. Save your own from the same Presets menu." },
    { "Four new effects",
      "Distortion, Lo-Fi (bit crusher), Pitch Shift (higher or deeper, same speed) "
      "and Tremolo, which turns into an auto-pan with Stereo up." },
    { "Sound effects straight onto a pad",
      "Right-click an empty soundboard pad → Import from URL… Search, preview, "
      "download, and it lands on the pad, with an option to trim it in the audio "
      "editor straight after." },
    { "Update Render",
      "Once a cue plays a render, re-rendering updates that same file with no "
      "save dialog. Effect-only edits are now saved, and a rendered cue no "
      "longer plays its effects twice." },
    { "Updates that finish",
      "quewi now closes itself when you install an update, so the installer can "
      "run. (If this update left quewi open, that was the old bug, fixed here.)" },
    // ── 1.0.1 ──
    { "A key for every pad",
      "Right-click a soundboard pad → Set keybind… On Windows the keys can work "
      "system-wide, so pads fire even while quewi sits behind a game, Discord or "
      "OBS. Pick where they work from the Keys menu on the soundboard." },
    { "DCA GO, from anywhere",
      "A second GO in the transport bar fires the next DCA cue at the console, "
      "without switching to the Mix page." },
    { "One GO, sound and scene",
      "Link a sound cue to a DCA cue in the Inspector and firing either one fires "
      "both. Double-click a DCA cell to tick who's on it." },
    { "Tear off what you need",
      "Drag an Inspector section's title out to float it (on a second monitor, "
      "say). Close it and it docks back where it came from." },
    { "Playback that does what you told it",
      "Fades hold their level, loops wrap cleanly, Fade Out actually fades, and "
      "auto-continue waits for post-wait. Pause is a real pause: press it again "
      "to resume exactly where you were." },
    { "Your desk, only your mics",
      "A DCA GO now touches only the mics in your show and leaves band, playback "
      "and talkback alone. The X32 link notices when the console drops and "
      "reconnects by itself." },
    { "Long tracks, light on memory",
      "A three-hour track used to take about 4 GB of RAM. It now takes about "
      "45 MB." },
    { "Heads up: New MSC cue is now Ctrl+Alt+M",
      "It clashed with the Mix grid's Ctrl+Shift+M, so neither worked. Show Mode "
      "also really locks now: no editing shortcut gets through mid-show." },
    { "Coffee",
      "A new warm theme: View → Theme → Coffee." },
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
        "<a href='https://servegaming.github.io/quewi/about/release-notes/' "
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
