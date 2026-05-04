#pragma once

#include <QString>

namespace quewi::ui {

// Loads a QSS theme from resources/themes/. See design.md §13.
class Theme {
public:
    static QString load(const QString &name);
};

} // namespace quewi::ui
