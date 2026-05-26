#include "ui/Theme.h"

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
} // namespace

QString Theme::load(const QString &name)
{
    // High-contrast piggy-backs on the dark QSS but with its own token
    // palette — same shapes and rhythms, brighter inks and surfaces.
    const QString qssName = (name == QLatin1String("quewi-highcontrast"))
                                ? QStringLiteral("quewi-dark")
                                : name;
    const QString path = QStringLiteral(":/themes/%1.qss").arg(qssName);
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    QString src = QString::fromUtf8(f.readAll());

    // Token substitution: any `@name` in the QSS is replaced with the
    // corresponding token's #rrggbb value. Keeps the QSS in sync with
    // Theme::tokens() without a build-time codegen step.
    const Tokens t = (name == QLatin1String("quewi-highcontrast"))
                         ? highContrastTokens()
                         : tokens();
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
    static const Tokens t{};
    return t;
}

} // namespace quewi::ui
