#pragma once

#include <QObject>
#include <QString>

class QFile;
class QNetworkAccessManager;
class QNetworkReply;

namespace quewi {

// Downloads the .msi installer for a newer release and hands it off to
// Windows' msiexec, then quits quewi so the install can lay the new
// files down without file-locking conflicts.
//
// Used instead of "open the .msi URL in a browser" when the user
// clicks Install in the update prompt — saves them the
// browser-find-it-in-Downloads-double-click dance and keeps the
// flow inside the app.
class UpdateInstaller : public QObject {
    Q_OBJECT
public:
    explicit UpdateInstaller(QObject *parent = nullptr);
    ~UpdateInstaller() override;

    // Kicks off an HTTP download of the .msi URL. Emits progress() as
    // bytes arrive, then either downloadFinished() with the local
    // path, or downloadFailed() with a reason. The caller decides
    // whether to launch the installer.
    void download(const QString &msiUrl);

    // Hand the downloaded MSI off to the OS (ShellExecute → file
    // association on Windows, which routes through msiexec and gives
    // the user a UAC + SmartScreen prompt the way a double-click in
    // Explorer would). Returns true if the launch was confirmed and
    // the app will quit shortly after; false if every fallback failed
    // (in which case Explorer is opened pointing at the MSI and the
    // app stays running so the user has a path forward).
    static bool launchAndQuit(const QString &msiPath);

signals:
    void progress(qint64 received, qint64 total);
    void downloadFinished(const QString &localPath);
    void downloadFailed(const QString &reason);

private slots:
    void onReplyReadyRead();
    void onReplyFinished();
    void onReplyError();

private:
    QNetworkAccessManager *m_nam = nullptr;
    QNetworkReply         *m_reply = nullptr;
    QFile                 *m_file = nullptr;
    QString                m_localPath;
    // Server-reported total bytes from the downloadProgress signal.
    // Used post-finish to confirm the file isn't truncated.
    qint64                 m_expectedBytes = -1;
};

} // namespace quewi
