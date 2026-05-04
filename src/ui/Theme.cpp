#include "ui/Theme.h"

#include <QFile>

namespace quewi::ui {

QString Theme::load(const QString &name)
{
    const QString path = QStringLiteral(":/themes/%1.qss").arg(name);
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    return QString::fromUtf8(f.readAll());
}

} // namespace quewi::ui
