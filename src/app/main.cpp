#include "MainWindow.h"
#include "ui/SmoothScroll.h"
#include "ui/Theme.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QSettings>
#include <QSurfaceFormat>
#include <QTimer>

int main(int argc, char *argv[])
{
    // ── Pre-QApplication tuning ───────────────────────────────────────
    // Don't coalesce wheel/touch events — at 165 Hz this matters a lot,
    // because Qt's default coalescing drops intermediate ticks and the
    // smooth-scroll animation re-targets feel laggy.
    QCoreApplication::setAttribute(Qt::AA_CompressHighFrequencyEvents, false);
    QCoreApplication::setAttribute(Qt::AA_CompressTabletEvents,         false);

    // Request a vsynced default surface format. QWidget itself isn't GL-
    // backed, but this carries through to any QOpenGLWidget the app spins
    // up (waveform / spectrogram could move there in a follow-up) and
    // ensures we don't tear when we do.
    QSurfaceFormat fmt;
    fmt.setSwapInterval(1);
    fmt.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
    QSurfaceFormat::setDefaultFormat(fmt);

    QApplication app(argc, argv);
    QApplication::setApplicationName("quewi");
    QApplication::setOrganizationName("ServeGaming");
    QApplication::setApplicationVersion(QStringLiteral(QUEWI_VERSION));
    QApplication::setStyle("Fusion");

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

    quewi::ui::SmoothScroll::install(&app);

    quewi::MainWindow w;
    w.show();

    if (parser.isSet(selftestOpt)) {
        QTimer::singleShot(200, &app, &QCoreApplication::quit);
    }
    return app.exec();
}
