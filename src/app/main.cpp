#include "MainWindow.h"
#include "ui/Theme.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QSettings>
#include <QTimer>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("quewi");
    QApplication::setOrganizationName("ServeGaming");
    QApplication::setApplicationVersion("0.0.1");
    QApplication::setStyle("Fusion"); // consistent base across platforms; QSS overrides chrome

    // CLI flags (used by CI perf gates and headless smoke tests).
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("quewi — theatre cueing"));
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption selftestOpt(QStringLiteral("selftest"),
        QStringLiteral("Open the main window, process events briefly, then exit 0. Used by CI cold-start gate."));
    QCommandLineOption selftestIdleOpt(QStringLiteral("selftest-idle"),
        QStringLiteral("Open the main window and stay idle (for idle-RSS gates)."));
    parser.addOption(selftestOpt);
    parser.addOption(selftestIdleOpt);
    parser.process(app);

    QSettings s(QStringLiteral("ServeGaming"), QStringLiteral("quewi"));
    const auto themeName = s.value(QStringLiteral("ui/theme"),
                                    QStringLiteral("quewi-dark")).toString();
    const auto qss = quewi::ui::Theme::load(themeName);
    if (!qss.isEmpty()) app.setStyleSheet(qss);

    quewi::MainWindow w;
    w.show();

    if (parser.isSet(selftestOpt)) {
        // Spin the event loop briefly so widgets paint at least once,
        // then quit cleanly. This is what the perf gate measures.
        QTimer::singleShot(200, &app, &QCoreApplication::quit);
    }
    // selftest-idle: just sit there until killed by the CI step.

    return app.exec();
}
