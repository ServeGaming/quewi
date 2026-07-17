#pragma once

#include <QColor>
#include <QPalette>
#include <QString>

namespace quewi::ui {

// Loads a QSS theme from resources/themes/. See UX.md §13 and DESIGN.md.
//
// Visual direction:
//   • Warm dark grays (slight brown undertone, not blue).
//   • Creamy off-white ink (#E8E2D4) instead of cool white.
//   • Restrained pastel state colours — mossy green, dusty blue,
//     amber, terracotta. No purple, no neon, no glow.
//   • Radius scale is exactly three values: 3 px on controls, 4 px on
//     panels / popups / cards, 2 px on tiny indicators (progress
//     chunks, hairline tracks). The GO button alone breaks scale
//     (6 px) — at hero size, 3 px reads as a sharp corner.
//   • Resting borders are 1 px everywhere; the only 2 px border is the
//     focus ring. Rules that thicken a border on :focus shave 1 px of
//     padding per side so controls don't shift when tabbed into.
//   • One accent only: amber (#C58B4A) — used sparingly for selection
//     and focus rings.
class Theme {
public:
    static QString load(const QString &name);

    // ── Tokens ────────────────────────────────────────────────────────
    // Single source of truth for both the QSS (string-substituted at
    // load) and any C++-painted widget. Changing a value here updates
    // the entire app on the next theme reload.
    struct Tokens {
        // Surfaces — tonal layering, no shadows.
        QColor bgDeep            { 0x1F, 0x1D, 0x1B };  // window
        QColor bgPanel           { 0x26, 0x24, 0x22 };  // panel
        QColor bgRow             { 0x2A, 0x28, 0x25 };  // row
        QColor bgRowAlt          { 0x25, 0x23, 0x1F };  // alt row (subtle)
        QColor bgRowHover        { 0x3A, 0x35, 0x30 };  // row hover
        QColor bgRowSelected     { 0x3C, 0x37, 0x33 };  // selected row
        QColor bgInteractive     { 0x34, 0x30, 0x2C };  // form fields, buttons
        QColor bgInverse         { 0x00, 0x00, 0x00 };  // video letterbox / pure black

        // Inks — text colours, slightly warm off-white.
        QColor ink100            { 0xE8, 0xE2, 0xD4 };  // primary
        QColor ink60             { 0xB5, 0xAC, 0x9C };  // secondary
        QColor ink40             { 0x7A, 0x73, 0x68 };  // dim / disabled
        QColor inkOnAccent       { 0x1A, 0x18, 0x15 };  // text on amber chip / button

        // Outlines / dividers.
        QColor divider           { 0x38, 0x33, 0x2E };  // panel-to-panel
        QColor outline           { 0x4A, 0x44, 0x3D };  // form / button border
        QColor outlineFocus      { 0xC5, 0x8B, 0x4A };  // 2 px focus ring

        // Functional state colours — restrained pastels.
        QColor accent            { 0xC5, 0x8B, 0x4A };  // amber
        QColor accentSoft        { 0xA0, 0x71, 0x3D };  // pressed amber
        QColor accentHover       { 0xE0, 0xAD, 0x58 };  // hover amber (lighter)
        QColor running           { 0x6F, 0xAE, 0x63 };  // mossy green
        QColor loaded            { 0x4F, 0x8E, 0xAF };  // dusty blue
        QColor warn              { 0xD7, 0xA2, 0x4E };  // amber
        QColor warnBright        { 0xE8, 0xC8, 0x61 };  // bright yellow (warn-hover)
        QColor err               { 0xC2, 0x6A, 0x55 };  // terracotta
        QColor errBright         { 0xFF, 0x5A, 0x5A };  // bright red (preflight fail)
        QColor info              { 0x62, 0xB4, 0xFF };  // info blue (EQ active band, preflight info)
    };

    // The active token set. Multiple themes will branch off this in a
    // follow-up; for now there's only one — the warm dark default.
    static const Tokens &tokens();

    // The active tokens expressed as a QPalette. QSS only reaches the
    // widgets it names; every QPainter-based widget that reads
    // palette().color(QPalette::Highlight) etc. otherwise falls through
    // to Fusion's stock colours (blue selection, cool greys). load()
    // applies this palette application-wide so those reads resolve to
    // the theme in one place — no per-widget token plumbing.
    static QPalette palette();
};

} // namespace quewi::ui
