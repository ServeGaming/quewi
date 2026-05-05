#include "ui/Theme.h"

#include <QFile>
#include <QHash>

namespace quewi::ui {

QString Theme::load(const QString &name)
{
    const QString path = QStringLiteral(":/themes/%1.qss").arg(name);
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    QString src = QString::fromUtf8(f.readAll());

    // Token substitution: any `@name` in the QSS is replaced with the
    // corresponding token's #rrggbb value. Keeps the QSS in sync with
    // Theme::tokens() without a build-time codegen step.
    const auto &t = tokens();
    const QHash<QString, QColor> map {
        {"bgDeep",        t.bgDeep},
        {"bgPanel",       t.bgPanel},
        {"bgRow",         t.bgRow},
        {"bgRowAlt",      t.bgRowAlt},
        {"bgRowHover",    t.bgRowHover},
        {"bgRowSelected", t.bgRowSelected},
        {"bgInteractive", t.bgInteractive},
        {"ink100",        t.ink100},
        {"ink60",         t.ink60},
        {"ink40",         t.ink40},
        {"divider",       t.divider},
        {"outline",       t.outline},
        {"outlineFocus",  t.outlineFocus},
        {"accent",        t.accent},
        {"accentSoft",    t.accentSoft},
        {"running",       t.running},
        {"loaded",        t.loaded},
        {"warn",          t.warn},
        {"err",           t.err},
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
