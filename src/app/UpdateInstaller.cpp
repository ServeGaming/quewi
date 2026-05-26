#include "UpdateInstaller.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>
#include <QUrl>

#ifdef Q_OS_WIN
#  include <windows.h>
#  include <shellapi.h>
#endif

#ifndef QUEWI_VERSION
#  define QUEWI_VERSION "0.0.0"
#endif

namespace quewi {

UpdateInstaller::UpdateInstaller(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
}

UpdateInstaller::~UpdateInstaller()
{
    if (m_reply)  m_reply->deleteLater();
    if (m_file)   { m_file->close(); m_file->deleteLater(); }
}

void UpdateInstaller::download(const QString &msiUrl)
{
    if (m_reply) {
        emit downloadFailed(tr("A download is already in progress."));
        return;
    }
    const QUrl url(msiUrl);
    if (!url.isValid() || url.scheme() != QStringLiteral("https")) {
        emit downloadFailed(tr("Refusing to download from a non-HTTPS URL: %1")
                            .arg(msiUrl));
        return;
    }

    // Stage to the user's Downloads folder — same place a browser
    // would put the MSI if the operator downloaded it themselves.
    // %TEMP% bit us in v0.9.x: Defender real-time protection or other
    // antivirus can quarantine an MSI in temp within seconds, and on
    // multi-user machines an elevated msiexec running as a different
    // admin can't see the original user's per-profile temp directory.
    // Downloads is per-user but standard, durable, and msiexec-safe.
    const QString name = QFileInfo(url.path()).fileName();
    const QString fileName = name.isEmpty() ? QStringLiteral("quewi-update.msi")
                                            : name;
    QString downloadsDir = QStandardPaths::writableLocation(
        QStandardPaths::DownloadLocation);
    if (downloadsDir.isEmpty()) {
        downloadsDir = QStandardPaths::writableLocation(
            QStandardPaths::TempLocation);
    }
    QDir().mkpath(downloadsDir);
    m_localPath = downloadsDir + QStringLiteral("/") + fileName;
    QFile::remove(m_localPath);
    m_file = new QFile(m_localPath, this);
    if (!m_file->open(QIODevice::WriteOnly)) {
        const auto err = m_file->errorString();
        m_file->deleteLater(); m_file = nullptr;
        emit downloadFailed(tr("Couldn't open %1: %2").arg(m_localPath, err));
        return;
    }
    m_expectedBytes = -1;

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("quewi/%1 (UpdateInstaller)")
                      .arg(QStringLiteral(QUEWI_VERSION)));
    // GitHub's download URLs redirect to S3-backed CDNs; opt into
    // following redirects up to the network manager's default policy.
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setTransferTimeout(60'000);   // 60 s of stall = abort

    m_reply = m_nam->get(req);
    connect(m_reply, &QNetworkReply::readyRead,
            this,    &UpdateInstaller::onReplyReadyRead);
    connect(m_reply, &QNetworkReply::finished,
            this,    &UpdateInstaller::onReplyFinished);
    connect(m_reply, &QNetworkReply::errorOccurred,
            this,    &UpdateInstaller::onReplyError);
    connect(m_reply, &QNetworkReply::downloadProgress, this,
        [this](qint64 received, qint64 total) {
            if (total > 0) m_expectedBytes = total;
            emit progress(received, total);
        });
}

void UpdateInstaller::onReplyReadyRead()
{
    if (m_file && m_reply) m_file->write(m_reply->readAll());
}

void UpdateInstaller::onReplyError()
{
    // Saved here so onReplyFinished can decide whether to emit
    // success or failure — finished() always fires, with or without
    // an error.
    if (m_reply) {
        // Don't emit twice; the finished slot handles the failure path.
    }
}

void UpdateInstaller::onReplyFinished()
{
    QNetworkReply *reply = m_reply;
    m_reply = nullptr;
    const auto err = reply->error();
    const auto errStr = reply->errorString();
    reply->deleteLater();

    if (m_file) {
        m_file->write(reply ? reply->readAll() : QByteArray{});
        m_file->close();
        m_file->deleteLater();
        m_file = nullptr;
    }

    if (err != QNetworkReply::NoError) {
        QFile::remove(m_localPath);
        emit downloadFailed(errStr);
        return;
    }

    // Validation gauntlet. Each check exists because something in
    // production has gone wrong here:
    //  1. Size floor — error pages from a missed redirect are tiny.
    //  2. Content-Length match — partial downloads pass the size
    //     floor but msiexec rejects truncated installers with
    //     "package could not be opened".
    //  3. OLE2 magic — MSIs are OLE compound documents starting
    //     with D0 CF 11 E0 A1 B1 1A E1. An HTML body or random
    //     binary garbage will fail this even at the right size.
    const qint64 actualBytes = QFileInfo(m_localPath).size();
    // Sniff for HTML before the size floor, because a stray HTML body
    // is what the size floor most often catches and "looks incomplete"
    // is a misleading explanation. The error should tell the operator
    // what actually happened: the URL pointed at a web page, not an
    // installer. UpdateChecker no longer routes us at the release page
    // URL by default, but any future bug there or a manual paste of a
    // wrong URL would land here.
    {
        QFile probe(m_localPath);
        if (probe.open(QIODevice::ReadOnly)) {
            const QByteArray head = probe.read(512).toLower();
            probe.close();
            if (head.contains("<!doctype html")
                || head.contains("<html")
                || head.contains("<head")) {
                QFile::remove(m_localPath);
                emit downloadFailed(tr("The download URL returned an "
                    "HTML page, not an installer. The release may "
                    "still be building — try again in a few "
                    "minutes, or download manually from the release "
                    "page."));
                return;
            }
        }
    }
    if (actualBytes < 5'000'000) {
        QFile::remove(m_localPath);
        emit downloadFailed(tr("Download finished but the file looks "
                               "incomplete (%1 bytes). Try again, or "
                               "download manually from the release page.")
                            .arg(actualBytes));
        return;
    }
    if (m_expectedBytes > 0 && actualBytes != m_expectedBytes) {
        QFile::remove(m_localPath);
        emit downloadFailed(tr("Download was truncated (got %1 of %2 bytes). "
                               "Check your connection and try again.")
                            .arg(actualBytes).arg(m_expectedBytes));
        return;
    }
    {
        QFile probe(m_localPath);
        if (probe.open(QIODevice::ReadOnly)) {
            const QByteArray head = probe.read(8);
            probe.close();
            // Per-platform magic header check. Each format has a
            // stable signature in its first few bytes that an HTML
            // error page or partial download can't accidentally
            // satisfy.
            //   .msi      OLE2 compound document → D0 CF 11 E0 A1 B1 1A E1
            //   .dmg      koly trailer + bzip2/UDIF; first bytes are
            //             usually "x" (78) for bzip2 streams or 0xED
            //             for UDIF — too variable to fingerprint
            //             reliably from the head, so we skip.
            //   AppImage  ELF executable → 7F 45 4C 46 (\x7fELF).
            bool ok = true;
            QString why;
#if defined(Q_OS_WIN)
            static const QByteArray kMagic =
                QByteArray::fromHex("d0cf11e0a1b11ae1");
            if (head != kMagic) {
                ok = false;
                why = tr("Downloaded file isn't a valid MSI "
                         "(magic header mismatch).");
            }
#elif defined(Q_OS_LINUX)
            static const QByteArray kMagic = QByteArray::fromHex("7f454c46");
            if (!head.startsWith(kMagic)) {
                ok = false;
                why = tr("Downloaded file isn't a valid AppImage "
                         "(ELF header missing).");
            }
#endif
            if (!ok) {
                QFile::remove(m_localPath);
                emit downloadFailed(why + QStringLiteral(" ")
                    + tr("Try again, or download manually from the release page."));
                return;
            }
        }
    }
    emit downloadFinished(m_localPath);
}

bool UpdateInstaller::launchInstaller(const QString &msiPath)
{
    // Per-platform handoff:
    //   Windows: ShellExecuteW with the default verb — exactly what a
    //            double-click in Explorer does. SmartScreen / UAC
    //            prompts surface visibly. Falls back to an explicit
    //            msiexec resolved from %SystemRoot%\System32, then to
    //            Explorer pointing at the file as a last resort.
    //   macOS:   /usr/bin/open hands the file to LaunchServices, which
    //            mounts a .dmg or runs a .pkg the same way Finder
    //            would. Falls back to opening the containing folder.
    //   Linux:   xdg-open hands the file to the desktop's default
    //            handler. AppImage paths get +x first since the
    //            download arrives without exec bits. Falls back to
    //            opening the containing folder.
    // We only quit on confirmed launch — otherwise the user keeps the
    // app open with a clear path forward (the "Open folder" button on
    // the failure dialog) instead of a blank desktop.
    const QString native = QDir::toNativeSeparators(msiPath);
    if (!QFileInfo::exists(msiPath)) {
        return false;
    }
    bool started = false;

#if defined(Q_OS_WIN)
    // ShellExecuteW returning >32 only means Windows found a handler —
    // it does NOT mean the handler actually ran. SmartScreen, Defender,
    // group policy, or a stale msiexec service can kill the new
    // process within milliseconds, invisibly. Quewi would then quit
    // 1.5 s later and the user is left with nothing — which is
    // exactly the reported symptom ("downloads, closes, no installer").
    //
    // Fix: use ShellExecuteExW with SEE_MASK_NOCLOSEPROCESS so we get
    // the process handle back. Wait up to 3 s for either: (a) the
    // process is still alive — install UI genuinely came up — or
    // (b) it exited — silently blocked, abort the auto-quit so the
    // user gets the fallback dialog with an Open Folder button.
    {
        std::wstring wpath = native.toStdWString();
        SHELLEXECUTEINFOW sei{};
        sei.cbSize = sizeof(sei);
        sei.fMask  = SEE_MASK_NOCLOSEPROCESS;
        sei.lpFile = wpath.c_str();
        sei.nShow  = SW_SHOWNORMAL;
        const BOOL ok = ShellExecuteExW(&sei);
        if (ok && sei.hProcess) {
            const DWORD wait = WaitForSingleObject(sei.hProcess, 3000);
            DWORD exitCode = 0;
            GetExitCodeProcess(sei.hProcess, &exitCode);
            CloseHandle(sei.hProcess);
            // WAIT_TIMEOUT = still running after 3 s → genuinely up.
            // STILL_ACTIVE (259) on exit-code read = same.
            // Anything else = it died on us.
            if (wait == WAIT_TIMEOUT || exitCode == STILL_ACTIVE) {
                started = true;
            } else {
                qWarning("UpdateInstaller: installer process exited "
                         "within 3 s of launch (exit code %lu). "
                         "SmartScreen / Defender / policy likely "
                         "blocked it.", exitCode);
                started = false;
            }
        } else {
            qWarning("UpdateInstaller: ShellExecuteExW failed, "
                     "hInstApp=%p", sei.hInstApp);
        }
    }
    if (!started) {
        // Fallback: invoke msiexec directly. Same end result if the
        // handler resolution was the problem; no help if the issue
        // is SmartScreen blocking unsigned MSIs — but cheap to try.
        const QString sysroot = qEnvironmentVariable("SystemRoot",
                                    QStringLiteral("C:/Windows"));
        const QString msiexec = QDir::toNativeSeparators(
            sysroot + QStringLiteral("/System32/msiexec.exe"));
        // QProcess::startDetached returns true on spawn — we can't
        // wait-for-exit cheaply here. Accept the optimistic answer;
        // if msiexec also dies invisibly the user sees the same
        // failure dialog moments later (quewi stays open since
        // we only quit further down on confirmed alive process).
        started = QProcess::startDetached(msiexec,
            { QStringLiteral("/i"), native });
    }
    if (!started) {
        QProcess::startDetached(QStringLiteral("explorer.exe"),
            { QStringLiteral("/select,") + native });
        return false;
    }
#elif defined(Q_OS_MACOS)
    // In-place update path: when quewi is running from inside a
    // proper .app bundle, we can mount the downloaded .dmg, extract
    // the new bundle, replace the currently-installed one, and
    // relaunch — no Finder window, no "drag to Applications", no
    // user clicks. The same flow that mac apps using Sparkle take.
    //
    // The path of the running bundle comes from QCoreApplication:
    // applicationDirPath() returns .../quewi.app/Contents/MacOS, so
    // we walk up two levels to land on the .app bundle. If we're
    // running from `build/macos-release/.../quewi.app` (developer
    // build) that path is still writable, so the swap works there
    // too — no special-casing needed.
    {
        QDir d(QCoreApplication::applicationDirPath());
        d.cdUp(); d.cdUp();                  // → quewi.app
        const QString bundlePath = d.absolutePath();
        const bool runningFromBundle =
            bundlePath.endsWith(QStringLiteral(".app"))
            && QFileInfo(bundlePath).isWritable();

        if (runningFromBundle) {
            const QString helperPath = QDir::tempPath()
                + QStringLiteral("/quewi-update-")
                + QString::number(QCoreApplication::applicationPid())
                + QStringLiteral(".sh");
            QFile h(helperPath);
            if (h.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                // Helper mounts the DMG, rsyncs the new .app over
                // the running .app, strips quarantine attrs (so
                // Gatekeeper doesn't flag the fresh bundle), then
                // detaches the DMG and execs the upgraded app.
                // All steps best-effort with || true on the dmg
                // detach so a stuck mount doesn't strand the user.
                QTextStream ts(&h);
                ts << "#!/bin/bash\n"
                   << "set -e\n"
                   << "PID=" << QCoreApplication::applicationPid() << "\n"
                   << "DMG=\"" << msiPath << "\"\n"
                   << "BUNDLE=\"" << bundlePath << "\"\n"
                   << "for i in $(seq 1 60); do\n"
                   << "  if ! kill -0 \"$PID\" 2>/dev/null; then break; fi\n"
                   << "  sleep 0.5\n"
                   << "done\n"
                   << "MOUNT=$(/usr/bin/hdiutil attach -nobrowse \"$DMG\" "
                   <<   "| grep '/Volumes/' | tail -1 "
                   <<   "| sed -E 's,^.*(/Volumes/[^\\\\t]+).*$,\\1,')\n"
                   << "if [ -z \"$MOUNT\" ] || [ ! -d \"$MOUNT/quewi.app\" ]; then\n"
                   << "  /usr/bin/hdiutil detach \"$MOUNT\" -quiet 2>/dev/null || true\n"
                   << "  /usr/bin/open \"$DMG\"\n"
                   << "  exit 1\n"
                   << "fi\n"
                   << "/usr/bin/rsync -a --delete \"$MOUNT/quewi.app/\" \"$BUNDLE/\"\n"
                   << "/usr/bin/xattr -cr \"$BUNDLE\" 2>/dev/null || true\n"
                   << "/usr/bin/hdiutil detach \"$MOUNT\" -quiet 2>/dev/null || true\n"
                   << "rm -f \"$DMG\"\n"
                   << "/usr/bin/open \"$BUNDLE\"\n"
                   << "rm -f \"$0\"\n";
                h.close();
                QFile::setPermissions(helperPath,
                    QFileDevice::ReadOwner | QFileDevice::WriteOwner
                  | QFileDevice::ExeOwner  | QFileDevice::ReadGroup
                  | QFileDevice::ExeGroup  | QFileDevice::ReadOther
                  | QFileDevice::ExeOther);
                if (QProcess::startDetached(
                        QStringLiteral("/bin/bash"), { helperPath })) {
                    started = true;
                }
            }
        }
    }

    // Fallback: hand the .dmg to LaunchServices the way the old
    // flow did — Finder mounts it, the user drags to Applications.
    if (!started) {
        started = QProcess::startDetached(
            QStringLiteral("/usr/bin/open"),
            { msiPath });
    }
    if (!started) {
        // Last resort: reveal the file in Finder.
        QProcess::startDetached(
            QStringLiteral("/usr/bin/open"),
            { QStringLiteral("-R"), msiPath });
        return false;
    }
#elif defined(Q_OS_LINUX)
    // AppImage downloads arrive without the executable bit. Set it
    // before handing off so the desktop's xdg-open can run it. .deb /
    // .rpm / .tar.* go straight to xdg-open.
    if (msiPath.endsWith(QStringLiteral(".AppImage"),
                         Qt::CaseInsensitive)) {
        QFile f(msiPath);
        f.setPermissions(f.permissions()
            | QFileDevice::ExeOwner | QFileDevice::ExeUser
            | QFileDevice::ExeGroup | QFileDevice::ExeOther);

        // In-place update path: when quewi is itself running as an
        // AppImage, $APPIMAGE is set by the AppImage runtime to the
        // absolute path of the running file. We can stage a tiny
        // shell helper that waits for our process to exit, swaps the
        // new AppImage over the old one, and execs the result. The
        // user ends up running the upgraded AppImage from the same
        // path they were running before — no manual file-management,
        // no duplicate AppImages cluttering Downloads.
        const QByteArray appImage = qgetenv("APPIMAGE");
        if (!appImage.isEmpty() && QFileInfo::exists(appImage)) {
            const QString runningPath = QString::fromLocal8Bit(appImage);
            const QString helperPath  = QDir::tempPath()
                + QStringLiteral("/quewi-update-")
                + QString::number(QCoreApplication::applicationPid())
                + QStringLiteral(".sh");
            QFile h(helperPath);
            if (h.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                // Wait for the running quewi to exit (PID poll, max
                // 30 s) so the AppImage isn't open when we swap it.
                // Then mv + chmod + exec the new file. The helper
                // self-destructs at the end.
                QTextStream ts(&h);
                ts << "#!/bin/bash\n"
                   << "set -e\n"
                   << "PID=" << QCoreApplication::applicationPid() << "\n"
                   << "for i in $(seq 1 60); do\n"
                   << "  if ! kill -0 \"$PID\" 2>/dev/null; then break; fi\n"
                   << "  sleep 0.5\n"
                   << "done\n"
                   << "mv -f " << QString(msiPath).replace('"', "\\\"") << " "
                              << QString(runningPath).replace('"', "\\\"") << "\n"
                   << "chmod +x " << QString(runningPath).replace('"', "\\\"") << "\n"
                   << "rm -f \"$0\"\n"
                   << "exec " << QString(runningPath).replace('"', "\\\"") << "\n";
                h.close();
                QFile::setPermissions(helperPath,
                    QFileDevice::ReadOwner  | QFileDevice::WriteOwner
                  | QFileDevice::ExeOwner   | QFileDevice::ReadGroup
                  | QFileDevice::ExeGroup   | QFileDevice::ReadOther
                  | QFileDevice::ExeOther);
                if (QProcess::startDetached(
                        QStringLiteral("/bin/bash"), { helperPath })) {
                    // The helper will exec the new AppImage in our
                    // place. We just need to quit now.
                    started = true;
                }
            }
        }

        // Fallback: not running as an AppImage (built locally, run
        // via dev tools) — just launch the new file in place. User
        // sees the new version; the old one is wherever they had
        // their dev build.
        if (!started) {
            started = QProcess::startDetached(msiPath, {});
        }
    }
    if (!started) {
        started = QProcess::startDetached(
            QStringLiteral("xdg-open"), { msiPath });
    }
    if (!started) {
        QProcess::startDetached(
            QStringLiteral("xdg-open"),
            { QFileInfo(msiPath).absolutePath() });
        return false;
    }
#else
    // Unknown platform — surface the path and let the user run it.
    return false;
#endif

    // NOTE: we intentionally do NOT quit the application here. MSIs
    // use Windows Restart Manager to ask the user to close any running
    // process that holds the install target — quewi getting an explicit
    // "close" prompt from the installer is the safe path. Auto-quitting
    // here was the cause of the "downloads, closes, no installer"
    // reports: when SmartScreen/UAC silently rejected the launch, quewi
    // disappeared anyway and the user was stranded.
    return true;
}

} // namespace quewi
