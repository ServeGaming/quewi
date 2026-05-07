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
#include <QUrl>

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

    // Stage to %TEMP%/quewi-update-X.Y.Z.msi. Windows sweeps temp on
    // its own; we don't track the file once msiexec takes over.
    const QString name = QFileInfo(url.path()).fileName();
    m_localPath = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
                  + QStringLiteral("/")
                  + (name.isEmpty()
                     ? QStringLiteral("quewi-update.msi")
                     : name);
    QFile::remove(m_localPath);
    m_file = new QFile(m_localPath, this);
    if (!m_file->open(QIODevice::WriteOnly)) {
        const auto err = m_file->errorString();
        m_file->deleteLater(); m_file = nullptr;
        emit downloadFailed(tr("Couldn't open %1: %2").arg(m_localPath, err));
        return;
    }

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
    connect(m_reply, &QNetworkReply::downloadProgress,
            this,    &UpdateInstaller::progress);
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
    if (QFileInfo(m_localPath).size() < 1024) {
        // Anything under 1 KB is almost certainly an error page that
        // the redirect chain delivered; refuse to hand a junk file
        // to msiexec.
        QFile::remove(m_localPath);
        emit downloadFailed(tr("Download finished but the file looks empty."));
        return;
    }
    emit downloadFinished(m_localPath);
}

void UpdateInstaller::launchAndQuit(const QString &msiPath)
{
    // Spawn msiexec /i <path> as a *detached* process. Detached so
    // killing quewi.exe doesn't take the installer with it; quewi
    // quits immediately afterwards so the MSI's file replacement
    // step doesn't hit a "file in use" lock on the running exe.
    const QStringList args { QStringLiteral("/i"), msiPath };
    if (!QProcess::startDetached(QStringLiteral("msiexec"), args)) {
        // Fallback — let the OS shell handle it (typically also
        // routes to msiexec, just via the file association).
        QDesktopServices::openUrl(QUrl::fromLocalFile(msiPath));
    }
    QCoreApplication::quit();
}

} // namespace quewi
