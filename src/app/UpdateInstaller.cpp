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
            static const QByteArray kOle2Magic =
                QByteArray::fromHex("d0cf11e0a1b11ae1");
            if (head != kOle2Magic) {
                QFile::remove(m_localPath);
                emit downloadFailed(tr("Downloaded file isn't a valid MSI "
                                       "(magic header mismatch). Try again, "
                                       "or download manually from the "
                                       "release page."));
                return;
            }
        }
    }
    emit downloadFinished(m_localPath);
}

bool UpdateInstaller::launchAndQuit(const QString &msiPath)
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
    {
        const std::wstring wpath = native.toStdWString();
        const HINSTANCE rc = ShellExecuteW(nullptr, nullptr, wpath.c_str(),
                                           nullptr, nullptr, SW_SHOWNORMAL);
        // ShellExecute returns >32 on success, an error code <=32 on failure.
        started = reinterpret_cast<INT_PTR>(rc) > 32;
    }
    if (!started) {
        const QString sysroot = qEnvironmentVariable("SystemRoot",
                                    QStringLiteral("C:/Windows"));
        const QString msiexec = QDir::toNativeSeparators(
            sysroot + QStringLiteral("/System32/msiexec.exe"));
        started = QProcess::startDetached(msiexec,
            { QStringLiteral("/i"), native });
    }
    if (!started) {
        QProcess::startDetached(QStringLiteral("explorer.exe"),
            { QStringLiteral("/select,") + native });
        return false;
    }
#elif defined(Q_OS_MACOS)
    started = QProcess::startDetached(
        QStringLiteral("/usr/bin/open"),
        { msiPath });
    if (!started) {
        // Fall back to revealing the file in Finder.
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
        started = QProcess::startDetached(msiPath, {});
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

    // Give the new process a moment to claim the install session and
    // its UI before we vacate the running exe so the installer can
    // replace it on platforms where in-place replacement isn't allowed.
    QTimer::singleShot(1500, qApp, &QCoreApplication::quit);
    return true;
}

} // namespace quewi
