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
    // The previous implementation spawned msiexec detached and quit
    // immediately. On unsigned MSIs (we don't sign yet) SmartScreen
    // can silently block the launch, leaving the user with nothing —
    // quewi closes, no installer ever shows. Fix:
    //   1. Use ShellExecuteW with the default verb — this is exactly
    //      what double-clicking the .msi in Explorer does. UAC and
    //      SmartScreen prompts surface visibly.
    //   2. Fall back to msiexec resolved from %SystemRoot%\System32
    //      so we don't depend on PATH.
    //   3. Final fallback: open Explorer pointing at the MSI so the
    //      user can run it manually.
    // We only quit if launch is confirmed. Otherwise the user keeps
    // quewi open with a clear error path instead of a blank desktop.
    const QString native = QDir::toNativeSeparators(msiPath);
    // Last-line guard: between download-validation and this call, the
    // user could close & reopen quewi (forgetting where the staged
    // file was), or antivirus could have swept the file. If it's gone
    // there's no point quitting the app.
    if (!QFileInfo::exists(msiPath)) {
        return false;
    }
    bool started = false;

#ifdef Q_OS_WIN
    {
        const std::wstring wpath = native.toStdWString();
        const HINSTANCE rc = ShellExecuteW(nullptr, nullptr, wpath.c_str(),
                                           nullptr, nullptr, SW_SHOWNORMAL);
        // ShellExecute returns >32 on success, an error code <=32 on failure.
        started = reinterpret_cast<INT_PTR>(rc) > 32;
    }
#endif

    if (!started) {
        const QString sysroot = qEnvironmentVariable("SystemRoot",
                                    QStringLiteral("C:/Windows"));
        const QString msiexec = QDir::toNativeSeparators(
            sysroot + QStringLiteral("/System32/msiexec.exe"));
        started = QProcess::startDetached(msiexec,
            { QStringLiteral("/i"), native });
    }

    if (!started) {
        // Last resort — open Explorer so the user can find and run
        // the file themselves. Don't quit; leaving them with neither
        // installer nor app is the failure mode we just hit.
        QProcess::startDetached(QStringLiteral("explorer.exe"),
            { QStringLiteral("/select,") + native });
        return false;
    }

    // Give the new process a moment to claim the install session and
    // its UI before we vacate the running exe so MSI can replace it.
    QTimer::singleShot(1500, qApp, &QCoreApplication::quit);
    return true;
}

} // namespace quewi
