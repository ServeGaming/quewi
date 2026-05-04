#pragma once

#include <QString>

namespace quewi::ui {

// Loads a QSS theme from resources/themes/. See UX.md §13 and DESIGN.md.
class Theme {
public:
    static QString load(const QString &name);
};

} // namespace quewi::ui
