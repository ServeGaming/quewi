#include "MainWindow.h"
#include "ui/Theme.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("quewi");
    QApplication::setOrganizationName("ServeGaming");
    QApplication::setApplicationVersion("0.0.1");
    QApplication::setStyle("Fusion"); // consistent base across platforms; QSS overrides chrome

    const auto qss = quewi::ui::Theme::load("quewi-dark");
    if (!qss.isEmpty()) app.setStyleSheet(qss);

    quewi::MainWindow w;
    w.show();
    return app.exec();
}
