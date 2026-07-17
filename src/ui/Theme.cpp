#include "ui/Theme.h"

#include <QApplication>
#include <QFile>
#include <QHash>

namespace quewi::ui {

namespace {
// High-contrast preset — for visually-impaired operators and badly-lit
// FOH positions where the warm-grey default washes out under a desk
// lamp. Pure black surfaces, fully saturated state colours, brighter
// accent. Layered greys are still tonally distinct so the row /
// alt-row stripes stay readable. Same QSS file as dark; only the
// token substitutions change.
Theme::Tokens highContrastTokens()
{
    Theme::Tokens t;
    t.bgDeep         = QColor(0x00, 0x00, 0x00);
    t.bgPanel        = QColor(0x0A, 0x0A, 0x0A);
    t.bgRow          = QColor(0x12, 0x12, 0x12);
    t.bgRowAlt       = QColor(0x1A, 0x1A, 0x1A);
    t.bgRowHover     = QColor(0x2C, 0x2C, 0x2C);
    t.bgRowSelected  = QColor(0x44, 0x32, 0x14);     // dark amber backing
    t.bgInteractive  = QColor(0x1F, 0x1F, 0x1F);
    t.bgInverse      = QColor(0x00, 0x00, 0x00);
    t.ink100         = QColor(0xFF, 0xFF, 0xFF);
    t.ink60          = QColor(0xD0, 0xD0, 0xD0);
    t.ink40          = QColor(0x9C, 0x9C, 0x9C);
    t.inkOnAccent    = QColor(0x00, 0x00, 0x00);
    t.divider        = QColor(0x55, 0x55, 0x55);
    t.outline        = QColor(0x80, 0x80, 0x80);
    t.outlineFocus   = QColor(0xFF, 0xC1, 0x46);
    t.accent         = QColor(0xFF, 0xC1, 0x46);     // bright amber
    t.accentSoft     = QColor(0xCC, 0x8E, 0x1F);
    t.accentHover    = QColor(0xFF, 0xD4, 0x6F);
    t.running        = QColor(0x32, 0xE0, 0x6F);     // saturated green
    t.loaded         = QColor(0x4F, 0xC2, 0xFF);     // bright cyan/blue
    t.warn           = QColor(0xFF, 0xC1, 0x46);
    t.warnBright     = QColor(0xFF, 0xE0, 0x66);
    t.err            = QColor(0xFF, 0x6B, 0x4D);
    t.errBright      = QColor(0xFF, 0x8A, 0x6F);
    t.info           = QColor(0x6F, 0xC8, 0xFF);
    return t;
}

// ── Fun themes ──────────────────────────────────────────────────────
// Each reuses quewi-dark.qss (same shapes/rhythm) with its own palette,
// exactly like high-contrast. Surfaces stay tonally layered so rows,
// hover and selection remain legible.

// Midnight — deep indigo surfaces, cyan accent. Cool and calm.
Theme::Tokens midnightTokens()
{
    Theme::Tokens t;
    t.bgDeep = QColor(0x14,0x17,0x1F); t.bgPanel = QColor(0x1A,0x1E,0x2A);
    t.bgRow = QColor(0x1F,0x24,0x30);  t.bgRowAlt = QColor(0x1B,0x20,0x2C);
    t.bgRowHover = QColor(0x2A,0x31,0x42); t.bgRowSelected = QColor(0x25,0x34,0x4A);
    t.bgInteractive = QColor(0x25,0x2C,0x3A); t.bgInverse = QColor(0,0,0);
    t.ink100 = QColor(0xE6,0xEC,0xF5); t.ink60 = QColor(0xA6,0xB0,0xC2);
    t.ink40 = QColor(0x6B,0x75,0x88);  t.inkOnAccent = QColor(0x0A,0x0E,0x16);
    t.divider = QColor(0x2A,0x31,0x42); t.outline = QColor(0x3A,0x43,0x57);
    t.outlineFocus = QColor(0x4F,0xC2,0xE0);
    t.accent = QColor(0x4F,0xC2,0xE0); t.accentSoft = QColor(0x3A,0x93,0xAB);
    t.accentHover = QColor(0x6F,0xD8,0xF0);
    t.running = QColor(0x5F,0xD0,0xA0); t.loaded = QColor(0x6F,0xA8,0xFF);
    t.warn = QColor(0xE0,0xB8,0x4E); t.warnBright = QColor(0xF0,0xD0,0x70);
    t.err = QColor(0xE0,0x6A,0x6A); t.errBright = QColor(0xFF,0x7A,0x7A);
    t.info = QColor(0x6F,0xC8,0xFF);
    return t;
}

// Forest — dark green surfaces, lime accent. Warm and organic.
Theme::Tokens forestTokens()
{
    Theme::Tokens t;
    t.bgDeep = QColor(0x16,0x20,0x1A); t.bgPanel = QColor(0x1C,0x29,0x22);
    t.bgRow = QColor(0x21,0x30,0x29);  t.bgRowAlt = QColor(0x1D,0x2A,0x23);
    t.bgRowHover = QColor(0x2C,0x3E,0x33); t.bgRowSelected = QColor(0x2E,0x44,0x34);
    t.bgInteractive = QColor(0x28,0x38,0x2F); t.bgInverse = QColor(0,0,0);
    t.ink100 = QColor(0xE8,0xEF,0xE2); t.ink60 = QColor(0xAE,0xBE,0xA8);
    t.ink40 = QColor(0x73,0x86,0x7A);  t.inkOnAccent = QColor(0x12,0x18,0x0F);
    t.divider = QColor(0x2A,0x3A,0x30); t.outline = QColor(0x3C,0x4F,0x44);
    t.outlineFocus = QColor(0x9B,0xD4,0x6A);
    t.accent = QColor(0x9B,0xD4,0x6A); t.accentSoft = QColor(0x76,0xA8,0x4E);
    t.accentHover = QColor(0xB5,0xE8,0x84);
    t.running = QColor(0x6F,0xCF,0x63); t.loaded = QColor(0x5F,0xB0,0xAF);
    t.warn = QColor(0xE0,0xC0,0x4E); t.warnBright = QColor(0xF0,0xDC,0x70);
    t.err = QColor(0xD8,0x8A,0x6A); t.errBright = QColor(0xFF,0x9A,0x7A);
    t.info = QColor(0x7A,0xC8,0xA0);
    return t;
}

// Synthwave — deep purple surfaces, magenta accent. Fun and bold.
Theme::Tokens synthwaveTokens()
{
    Theme::Tokens t;
    t.bgDeep = QColor(0x1A,0x14,0x26); t.bgPanel = QColor(0x22,0x1A,0x30);
    t.bgRow = QColor(0x2A,0x1F,0x3C);  t.bgRowAlt = QColor(0x24,0x1B,0x34);
    t.bgRowHover = QColor(0x38,0x28,0x4E); t.bgRowSelected = QColor(0x44,0x24,0x5A);
    t.bgInteractive = QColor(0x32,0x26,0x4A); t.bgInverse = QColor(0,0,0);
    t.ink100 = QColor(0xF5,0xE6,0xFF); t.ink60 = QColor(0xC2,0xA6,0xD8);
    t.ink40 = QColor(0x88,0x70,0xA0);  t.inkOnAccent = QColor(0x16,0x08,0x1A);
    t.divider = QColor(0x38,0x28,0x4E); t.outline = QColor(0x50,0x3A,0x6A);
    t.outlineFocus = QColor(0xFF,0x5A,0xC8);
    t.accent = QColor(0xFF,0x5A,0xC8); t.accentSoft = QColor(0xCC,0x3D,0x9C);
    t.accentHover = QColor(0xFF,0x7A,0xD8);
    t.running = QColor(0x5A,0xE0,0xA0); t.loaded = QColor(0x5A,0xC8,0xFF);
    t.warn = QColor(0xFF,0xC8,0x5A); t.warnBright = QColor(0xFF,0xE0,0x66);
    t.err = QColor(0xFF,0x6B,0x8A); t.errBright = QColor(0xFF,0x8A,0xAA);
    t.info = QColor(0x6F,0xD0,0xFF);
    return t;
}

// Light — mirrors the hardcoded values in quewi-light.qss so that
// C++-painted widgets and the derived QPalette agree with the light
// stylesheet. quewi-light.qss has no @placeholders (the substitution
// pass is a no-op there); this token set exists purely for the painted
// widgets and QPalette roles. Every hex here already appears in
// quewi-light.qss or the dark token set — no new colours; the two
// accent variants are derived (darker/lighter) rather than invented.
Theme::Tokens lightTokens()
{
    Theme::Tokens t;
    t.bgDeep         = QColor(0xF4, 0xF5, 0xF8);     // window
    t.bgPanel        = QColor(0xFF, 0xFF, 0xFF);     // cards / lists
    t.bgRow          = QColor(0xFF, 0xFF, 0xFF);
    t.bgRowAlt       = QColor(0xF4, 0xF5, 0xF8);
    t.bgRowHover     = QColor(0xEE, 0xF2, 0xF8);
    t.bgRowSelected  = QColor(0xCF, 0xE2, 0xFF);
    t.bgInteractive  = QColor(0xFF, 0xFF, 0xFF);
    t.bgInverse      = QColor(0x00, 0x00, 0x00);     // letterbox stays black
    t.ink100         = QColor(0x1A, 0x1D, 0x24);
    t.ink60          = QColor(0x4A, 0x4F, 0x5A);
    t.ink40          = QColor(0x9C, 0xA3, 0xAF);
    t.inkOnAccent    = QColor(0xFF, 0xFF, 0xFF);
    t.divider        = QColor(0xD7, 0xD9, 0xDF);
    t.outline        = QColor(0xC7, 0xCA, 0xD2);
    t.outlineFocus   = QColor(0x25, 0x63, 0xEB);
    t.accent         = QColor(0x25, 0x63, 0xEB);     // light's blue accent
    t.accentSoft     = QColor(0x25, 0x63, 0xEB).darker(125);
    t.accentHover    = QColor(0x25, 0x63, 0xEB).lighter(115);
    // State colours carry over from dark — they are legible on white and
    // keeping them avoids inventing a parallel light-state palette.
    // errBright switches to the light QSS's own bright red.
    t.errBright      = QColor(0xE8, 0x4A, 0x2F);
    t.info           = QColor(0x4F, 0x8E, 0xAF);     // dusty blue (progress chunk)
    return t;
}

// Maps a theme name to its palette. Default = the warm dark tokens.
Theme::Tokens tokensForName(const QString &name)
{
    if (name == QLatin1String("quewi-highcontrast")) return highContrastTokens();
    if (name == QLatin1String("quewi-midnight"))     return midnightTokens();
    if (name == QLatin1String("quewi-forest"))       return forestTokens();
    if (name == QLatin1String("quewi-synthwave"))    return synthwaveTokens();
    if (name == QLatin1String("quewi-light"))        return lightTokens();
    return Theme::Tokens{};   // warm-dark default
}

// Tokens → QPalette. Role mapping is semantic, not mechanical: painted
// widgets read Highlight for "the accent", Mid/Midlight for "a quiet
// outline / an even quieter one", Base/AlternateBase for "a recessed
// surface / a slightly raised one". Without this, main() sets Fusion +
// a QSS but never a palette, so every one of those reads resolved to
// Fusion's stock colours — the root cause of the "foreign blue"
// artifacts (drag indicator, corner-pin handles, stage view rings).
QPalette paletteFromTokens(const Theme::Tokens &t)
{
    QPalette pal;
    pal.setColor(QPalette::Window,          t.bgDeep);
    pal.setColor(QPalette::WindowText,      t.ink100);
    pal.setColor(QPalette::Base,            t.bgPanel);
    pal.setColor(QPalette::AlternateBase,   t.bgRow);
    pal.setColor(QPalette::Text,            t.ink100);
    pal.setColor(QPalette::PlaceholderText, t.ink40);
    pal.setColor(QPalette::Button,          t.bgInteractive);
    pal.setColor(QPalette::ButtonText,      t.ink100);
    pal.setColor(QPalette::BrightText,      t.errBright);
    pal.setColor(QPalette::Highlight,       t.accent);
    pal.setColor(QPalette::HighlightedText, t.inkOnAccent);
    // The 3D-bevel roles, flattened onto the outline scale. Fusion uses
    // these for frame shading; our painted widgets use them as "quiet
    // line" (Mid) and "quieter line" (Midlight).
    pal.setColor(QPalette::Light,           t.bgRowHover);
    pal.setColor(QPalette::Midlight,        t.divider);
    pal.setColor(QPalette::Mid,             t.outline);
    pal.setColor(QPalette::Dark,            t.bgDeep);
    pal.setColor(QPalette::Shadow,          t.bgInverse);
    pal.setColor(QPalette::Link,            t.accent);
    pal.setColor(QPalette::LinkVisited,     t.accentSoft);
    pal.setColor(QPalette::ToolTipBase,     t.bgInteractive);
    pal.setColor(QPalette::ToolTipText,     t.ink100);
    // Disabled group — dim ink, no accent.
    pal.setColor(QPalette::Disabled, QPalette::WindowText,      t.ink40);
    pal.setColor(QPalette::Disabled, QPalette::Text,            t.ink40);
    pal.setColor(QPalette::Disabled, QPalette::ButtonText,      t.ink40);
    pal.setColor(QPalette::Disabled, QPalette::Base,            t.bgPanel);
    pal.setColor(QPalette::Disabled, QPalette::Highlight,       t.bgRowSelected);
    pal.setColor(QPalette::Disabled, QPalette::HighlightedText, t.ink40);
    return pal;
}

// The palette the C++-painted widgets read. load() updates it so the fun
// themes / high-contrast / light actually reach widgets that paint from
// Theme::tokens() (soundboard pads, video scrubber) rather than via QSS.
Theme::Tokens &activeTokensRef()
{
    static Theme::Tokens t{};
    return t;
}
} // namespace

QString Theme::load(const QString &name)
{
    // Light is a standalone hardcoded QSS. Every other theme (dark,
    // high-contrast, and the fun palettes) shares quewi-dark.qss and only
    // swaps the @token palette — same shapes and rhythms, different colours.
    const QString qssName = (name == QLatin1String("quewi-light"))
                                ? QStringLiteral("quewi-light")
                                : QStringLiteral("quewi-dark");
    const QString path = QStringLiteral(":/themes/%1.qss").arg(qssName);
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    QString src = QString::fromUtf8(f.readAll());

    // Token substitution: any `@name` in the QSS is replaced with the
    // corresponding token's #rrggbb value. Keeps the QSS in sync with
    // the palette without a build-time codegen step. (Light has no
    // @placeholders, so this is a no-op there.)
    const Tokens t = tokensForName(name);
    activeTokensRef() = t;   // so C++-painted widgets pick up the theme too

    // Re-derive the application palette from the new tokens. This is
    // the other half of theming: QSS only reaches the widgets it names,
    // while every palette().color(...) read in a QPainter widget — and
    // Fusion's own drawing of anything the QSS skips (combo arrows,
    // frame shading) — resolves through the app palette. Applying it
    // here (rather than at each call site) means a theme switch at
    // runtime re-themes the painted widgets too. Guarded so headless
    // unit tests can call load() without a QApplication.
    if (auto *app = qobject_cast<QApplication *>(QCoreApplication::instance()))
        app->setPalette(paletteFromTokens(t));
    const QHash<QString, QColor> map {
        {"bgDeep",        t.bgDeep},
        {"bgPanel",       t.bgPanel},
        {"bgRow",         t.bgRow},
        {"bgRowAlt",      t.bgRowAlt},
        {"bgRowHover",    t.bgRowHover},
        {"bgRowSelected", t.bgRowSelected},
        {"bgInteractive", t.bgInteractive},
        {"bgInverse",     t.bgInverse},
        {"ink100",        t.ink100},
        {"ink60",         t.ink60},
        {"ink40",         t.ink40},
        {"inkOnAccent",   t.inkOnAccent},
        {"divider",       t.divider},
        {"outline",       t.outline},
        {"outlineFocus",  t.outlineFocus},
        {"accent",        t.accent},
        {"accentSoft",    t.accentSoft},
        {"accentHover",   t.accentHover},
        {"running",       t.running},
        {"loaded",        t.loaded},
        {"warn",          t.warn},
        {"warnBright",    t.warnBright},
        {"err",           t.err},
        {"errBright",     t.errBright},
        {"info",          t.info},
    };
    for (auto it = map.constBegin(); it != map.constEnd(); ++it) {
        src.replace(QStringLiteral("@%1").arg(it.key()),
                    it.value().name(QColor::HexRgb));
    }
    return src;
}

const Theme::Tokens &Theme::tokens()
{
    return activeTokensRef();
}

QPalette Theme::palette()
{
    return paletteFromTokens(activeTokensRef());
}

} // namespace quewi::ui
