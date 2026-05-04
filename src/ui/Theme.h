#pragma once

#include <QColor>
#include <QString>

namespace quewi::ui {

// Loads a QSS theme from resources/themes/. See UX.md §13 and DESIGN.md.
class Theme {
public:
    static QString load(const QString &name);

    // ── Tokens ────────────────────────────────────────────────────────
    // Single source of truth for both QSS (string-substituted at load) and
    // any C++-side QPainter code (TimelineCanvas, ParametricEqDialog,
    // SpectrogramWidget, PeakMeter, …). Values mirror the Stitch
    // DESIGN.md frontmatter for the "Technical Precision" theme so that
    // changing them in one place changes the whole app.
    struct Tokens {
        // Surfaces — tonal layering, no shadows.
        QColor bgDeep            { 0x10, 0x14, 0x19 };  // window / outermost
        QColor bgPanel           { 0x18, 0x1c, 0x22 };  // panel
        QColor bgRow             { 0x1c, 0x20, 0x26 };  // row
        QColor bgRowAlt          { 0x14, 0x18, 0x1d };  // row alt (subtle)
        QColor bgRowSelected     { 0x27, 0x2a, 0x30 };  // selected row
        QColor bgInteractive     { 0x31, 0x35, 0x3b };  // form fields, buttons

        // Inks — text colours.
        QColor ink100            { 0xe0, 0xe2, 0xeb };  // primary
        QColor ink60             { 0xc0, 0xc7, 0xd4 };  // secondary
        QColor ink40             { 0x8a, 0x91, 0x9e };  // dim / disabled

        // Outlines / dividers.
        QColor divider           { 0x26, 0x2a, 0x38 };  // panel-to-panel
        QColor outline           { 0x41, 0x47, 0x52 };  // form / button border
        QColor outlineFocus      { 0xa4, 0xc9, 0xff };  // 2 px focus ring

        // Functional state colours.
        QColor accent            { 0xa4, 0xc9, 0xff };  // primary
        QColor accentStrong      { 0x4a, 0x9e, 0xff };  // primary-container
        QColor running           { 0x60, 0xdf, 0x85 };  // green – sounding
        QColor loaded            { 0x4a, 0x9e, 0xff };  // blue – pre-rolled
        QColor warn              { 0xff, 0xb8, 0x69 };  // amber
        QColor err               { 0xff, 0xb4, 0xab };  // red
    };

    // The active token set. For now there's only one ("Technical Precision")
    // — multiple themes will branch off this in a follow-up.
    static const Tokens &tokens();
};

} // namespace quewi::ui
