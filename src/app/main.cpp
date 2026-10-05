#include "audio/SampleStore.h"
#include "MainWindow.h"
#include "ui/SmoothScroll.h"
#include "ui/Theme.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QSettings>
#include <QStatusBar>
#include "ui/WelcomeDialog.h"

#include <QComboBox>
#include <QDropEvent>
#include <QFileOpenEvent>
#include <QMimeData>
#include <QPointer>
#include <QStandardPaths>
#include <QSurfaceFormat>
#include <QTimer>
#include <QUrl>

#include <utility>

#if defined(_MSC_VER) && defined(_DEBUG)
#  include <crtdbg.h>
#  include <windows.h>
#  include <DbgHelp.h>
#  pragma comment(lib, "Dbghelp.lib")
#  include <cstdio>

namespace {

// Path of the diagnostic log: %TEMP%/quewi-debug.log. Written every
// time the CRT trips a debug assertion in this build so we can see
// which std::vector / std::string went out of bounds without
// reproducing under a debugger.
QString debugLogPath() {
    return QStandardPaths::writableLocation(QStandardPaths::TempLocation)
         + QStringLiteral("/quewi-debug.log");
}

void writeBacktrace(FILE *out) {
    void *frames[64] = {};
    const USHORT n = RtlCaptureStackBackTrace(0, 64, frames, nullptr);
    HANDLE proc = GetCurrentProcess();
    static bool symInited = false;
    if (!symInited) { SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_DEFERRED_LOADS);
                      SymInitialize(proc, nullptr, TRUE); symInited = true; }
    char buf[sizeof(SYMBOL_INFO) + 256] = {};
    auto *sym = reinterpret_cast<PSYMBOL_INFO>(buf);
    sym->SizeOfStruct = sizeof(SYMBOL_INFO);
    sym->MaxNameLen   = 255;
    for (USHORT i = 0; i < n; ++i) {
        const auto addr = reinterpret_cast<DWORD64>(frames[i]);
        IMAGEHLP_LINE64 line = {}; line.SizeOfStruct = sizeof(line); DWORD disp = 0;
        if (SymFromAddr(proc, addr, nullptr, sym)) {
            std::fprintf(out, "  [%2u] %s + 0x%llx", i, sym->Name,
                         (unsigned long long)(addr - sym->Address));
            if (SymGetLineFromAddr64(proc, addr, &disp, &line))
                std::fprintf(out, "  (%s:%lu)", line.FileName, line.LineNumber);
            std::fputc('\n', out);
        } else {
            std::fprintf(out, "  [%2u] 0x%llx\n", i, (unsigned long long)addr);
        }
    }
}

int crtReportHook(int type, char *msg, int *retVal) {
    const char *kind = (type == _CRT_ASSERT) ? "ASSERT"
                     : (type == _CRT_ERROR)  ? "ERROR"
                                              : "WARN";
    // Print to stderr (visible if launched from a console).
    std::fprintf(stderr, "\n=== quewi CRT %s ===\n%s\n=== Stack ===\n", kind, msg);
    writeBacktrace(stderr);
    std::fprintf(stderr, "=================\n");
    std::fflush(stderr);
    // Append to the log file too — the user usually launches via Explorer
    // and never sees stderr.
    if (FILE *f = nullptr; fopen_s(&f, debugLogPath().toLocal8Bit().constData(), "a") == 0 && f) {
        std::fprintf(f, "\n=== quewi CRT %s ===\n%s\n=== Stack ===\n", kind, msg);
        writeBacktrace(f);
        std::fprintf(f, "=================\n");
        std::fclose(f);
    }
    *retVal = 0;        // 0 = continue (don't break into debugger)
    return TRUE;        // TRUE = handled, suppress the modal dialog
}

void installCrtAssertCapture() {
    // Drop the modal dialog and instead route assertions through our
    // hook. Continuing past an iterator-debug failure is "undefined,
    // probably crashes soon", but we do it deliberately to capture a
    // call stack so we know which line of our code asked for the
    // out-of-range access.
    _CrtSetReportHook(&crtReportHook);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    // Truncate previous log so we don't accumulate ancient runs.
    QFile::remove(debugLogPath());
    std::fprintf(stderr, "[quewi] CRT assertion capture installed; log: %s\n",
                 debugLogPath().toLocal8Bit().constData());
}

} // namespace
#endif

namespace {

// macOS: double-clicking a .quewi in Finder, dropping one on the Dock icon,
// or "Open With > quewi" doesn't pass a command-line argument; the app gets
// a QFileOpenEvent instead (the document type is declared in Info.plist).
// Only macOS sends these, so on Windows/Linux this filter never does
// anything. It can arrive at any point — while the crash-recovery prompt
// or the Welcome dialog is up, or long after launch — so:
//   - before the main window is shown, the path is queued (and a Welcome
//     dialog that's up is closed: the operator already picked a show);
//   - once it's shown, the file opens exactly as if it were dragged onto
//     the window: the "Save changes?" prompt, and nothing in show mode.
class FileOpenFilter : public QObject {
public:
    using QObject::QObject;

    QPointer<quewi::MainWindow> window;   // set once the main window is shown
    QPointer<QDialog>           welcome;  // the launch Welcome dialog, while up
    QStringList                 pending;  // arrived before the window was shown

    bool eventFilter(QObject *obj, QEvent *ev) override
    {
        if (ev->type() != QEvent::FileOpen || obj != QCoreApplication::instance())
            return QObject::eventFilter(obj, ev);
        auto *fe = static_cast<QFileOpenEvent *>(ev);
        QString path = fe->file();
        if (path.isEmpty()) path = fe->url().toLocalFile();
        if (path.isEmpty()) return QObject::eventFilter(obj, ev);

        if (window) {
            open(path);
        } else {
            pending.append(path);
            if (welcome) welcome->reject();
        }
        return true;
    }

    // The window is up: open everything that was queued.
    void flush()
    {
        const QStringList paths = std::exchange(pending, {});
        for (const QString &p : paths) open(p);
    }

private:
    void open(const QString &path)
    {
        // Deferred: the open may show a modal "Save changes?" prompt, which
        // shouldn't run inside the OS's open-document callback.
        QTimer::singleShot(0, window.data(), [w = window, path] {
            if (!w) return;
            if (w->isMinimized()) w->showNormal();
            w->raise();
            w->activateWindow();
            // Show mode switches drops off to lock the show; respect that.
            if (!w->acceptDrops()) {
                w->statusBar()->showMessage(
                    QObject::tr("Show mode is on, so %1 wasn't opened. Leave show mode and open it again.")
                        .arg(QFileInfo(path).fileName()), 6000);
                return;
            }
            // Same path as dragging the file onto the window (MainWindow::
            // dropEvent): a .quewi opens as the show, after the usual
            // save-changes prompt.
            QMimeData mime;
            mime.setUrls({ QUrl::fromLocalFile(path) });
            QDropEvent drop(QPointF(), Qt::CopyAction, &mime, Qt::NoButton, Qt::NoModifier);
            QCoreApplication::sendEvent(w.data(), &drop);
        });
    }
};

} // namespace

int main(int argc, char *argv[])
{
#if defined(_MSC_VER) && defined(_DEBUG)
    installCrtAssertCapture();
#endif

    // ── Pre-QApplication tuning ───────────────────────────────────────
    // Force Qt Multimedia onto its FFmpeg backend. The platform-native
    // backends — AVFoundation on macOS, WMF on Windows — cannot decode
    // Opus or VP9/WebM, which is exactly what YouTube serves as "best
    // audio"/"best video". Users dropping a downloaded .opus/.webm in as
    // a cue got silent failure (the native decoder errors out). The
    // FFmpeg backend (bundled by macdeployqt / windeployqt as the
    // ffmpegmediaplugin) decodes essentially everything, so pin it
    // before QApplication brings the multimedia plugins up. Honour an
    // explicit override if the user set one in their environment.
    if (qEnvironmentVariableIsEmpty("QT_MEDIA_BACKEND")) {
        qputenv("QT_MEDIA_BACKEND", "ffmpeg");
    }

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
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/quewi.png")));

    // Finder / Dock "open this show" (macOS only; see FileOpenFilter).
    // Installed this early so an open that arrives during startup is kept.
    auto *fileOpenFilter = new FileOpenFilter(&app);
    app.installEventFilter(fileOpenFilter);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("quewi — theatre cueing"));
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption selftestOpt(QStringLiteral("selftest"),
        QStringLiteral("Open the main window, process events briefly, then exit 0. Used by CI cold-start gate."));
    QCommandLineOption selftestIdleOpt(QStringLiteral("selftest-idle"),
        QStringLiteral("Open the main window and stay idle (for idle-RSS gates)."));
    QCommandLineOption tourOpt(QStringLiteral("screenshot-tour"),
        QStringLiteral("Build a demo show, save a picture of each main screen to <dir>, then exit."),
        QStringLiteral("dir"));
    parser.addOption(selftestOpt);
    parser.addOption(selftestIdleOpt);
    parser.addOption(tourOpt);
    parser.process(app);

    QSettings s(QStringLiteral("ServeGaming"), QStringLiteral("quewi"));
    const auto themeName = s.value(QStringLiteral("ui/theme"),
                                    QStringLiteral("quewi-dark")).toString();
    // Theme::load() does two things: it returns the token-substituted
    // QSS, and it applies a QPalette derived from the same tokens
    // application-wide. The palette half matters — without it, every
    // QPainter widget that reads palette().color(...) and every
    // sub-control the QSS doesn't name falls through to Fusion's stock
    // colours (the blue-selection / raised-bevel class of bug). It must
    // run after setStyle("Fusion") above, since setStyle resets the
    // application palette to the style's default.
    const auto qss = quewi::ui::Theme::load(themeName);
    if (!qss.isEmpty()) app.setStyleSheet(qss);

    quewi::ui::SmoothScroll::install(&app);

    // Make every QComboBox dropdown show its full item list rather
    // than scrolling at Qt's default of 10. Combined with
    // `combobox-popup: 0` in the theme QSS this gives the Qt-drawn
    // popup permission to grow up to the screen edge. We catch the
    // boxes via an application-level event filter on QEvent::Show
    // so freshly-built dialogs (Preferences, Inspector children,
    // device pickers) inherit the behaviour without each call site
    // having to remember setMaxVisibleItems.
    class ComboFilter : public QObject {
    public:
        using QObject::QObject;
        bool eventFilter(QObject *obj, QEvent *ev) override {
            if (ev->type() == QEvent::Show) {
                if (auto *cb = qobject_cast<QComboBox *>(obj)) {
                    cb->setMaxVisibleItems(999);
                }
            }
            return QObject::eventFilter(obj, ev);
        }
    };
    auto *comboFilter = new ComboFilter(&app);
    app.installEventFilter(comboFilter);

    // Long audio files decode into memory-mapped cache files; clear any left
    // behind by a quewi that crashed (they can be gigabytes).
    quewi::audio::SampleStore::sweepStaleCache();

    quewi::MainWindow w;

    // Welcome launchpad — Xcode-style picker so the operator's first
    // surface is a deliberate choice (New / Open / Recent) rather
    // than an empty Untitled cue list. Suppressed when:
    //   - a show was passed on the command line (cue-driven workflows
    //     and shell associations should land straight in the show);
    //   - the user disabled it via the "Show on launch" checkbox;
    //   - we're running --selftest (CI cold-start gate).
    const auto positional = parser.positionalArguments();
    const bool selftest = parser.isSet(selftestOpt) || parser.isSet(selftestIdleOpt) || parser.isSet(tourOpt);

    // Crash recovery first — before the Welcome dialog or a show passed on
    // the command line, either of which would otherwise replace the
    // recovered work. Never in selftest: a modal prompt would hang CI.
    const bool recovered = !selftest && w.recoverFromJournalIfPresent();

    QString welcomeChosenPath;
    if (!recovered
        && positional.isEmpty()
        && fileOpenFilter->pending.isEmpty()   // Finder already chose a show (macOS)
        && !selftest
        && quewi::ui::WelcomeDialog::showOnLaunchEnabled())
    {
        quewi::ui::WelcomeDialog welcome;
        fileOpenFilter->welcome = &welcome;
        welcome.exec();
        if (welcome.action() == quewi::ui::WelcomeDialog::Action::OpenExisting
            || welcome.action() == quewi::ui::WelcomeDialog::Action::OpenRecent) {
            welcomeChosenPath = welcome.chosenPath();
        }
        // NewShow + None both fall through to the default Untitled
        // state the MainWindow already creates.
    }

    // An update was started from a prompt shown before the event loop ran:
    // the installer is waiting for this process to exit, so do exactly that.
    if (w.isQuittingForUpdate()) return 0;

    w.show();
    if (!selftest) w.runStartupChecks();

    // First-run-after-update toast. If the version stored in
    // QSettings differs from the binary's version, this is either
    // (a) the very first launch ever (lastVersion empty) — skip the
    // toast, the welcome dialog already greets new users; or
    // (b) a launch after an in-app update — show a brief status
    // confirmation so the operator knows the swap actually landed
    // and they're not still on the old version.
    {
        QSettings versionStore(QStringLiteral("ServeGaming"),
                               QStringLiteral("quewi"));
        const QString lastVersion =
            versionStore.value(QStringLiteral("lastSeenVersion")).toString();
        const QString thisVersion = QStringLiteral(QUEWI_VERSION);
        if (!lastVersion.isEmpty() && lastVersion != thisVersion) {
            // Status-bar nudge — non-modal, auto-clears after 4 s.
            // Fires after the event loop starts so it lands on the
            // visible status bar rather than being absorbed by the
            // window-being-shown noise.
            QTimer::singleShot(150, &w, [&w, lastVersion, thisVersion] {
                w.statusBar()->showMessage(
                    QObject::tr("Updated from %1 to %2")
                        .arg(lastVersion, thisVersion),
                    4000);
            });
        }
        versionStore.setValue(QStringLiteral("lastSeenVersion"), thisVersion);
    }

    if (recovered) {
        // The recovered show stays; a file passed on the command line is not
        // opened over it (the operator just chose to recover).
    } else if (!positional.isEmpty()) {
        QTimer::singleShot(0, &w, [&w, positional]{
            w.loadShowFromPath(positional.first());
        });
    } else if (!welcomeChosenPath.isEmpty()) {
        QTimer::singleShot(0, &w, [&w, welcomeChosenPath]{
            w.loadShowFromPath(welcomeChosenPath);
        });
    }

    // Shows opened from Finder during startup (macOS), queued after the
    // above so they win; from now on they open straight away. Over a
    // recovered show they go through the save-changes prompt.
    fileOpenFilter->window = &w;
    fileOpenFilter->flush();

    if (parser.isSet(selftestOpt)) {
        QTimer::singleShot(200, &app, &QCoreApplication::quit);
    }
    if (parser.isSet(tourOpt)) {
        const QString dir = parser.value(tourOpt);
        QTimer::singleShot(300, &w, [&w, dir] { w.runScreenshotTour(dir); });
    }
    return app.exec();
}
