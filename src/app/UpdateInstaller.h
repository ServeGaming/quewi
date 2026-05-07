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

    // Hand the downloaded MSI off to msiexec /i and quit the app so
    // the installer can write into the install dir without a file
    // lock on quewi.exe. Falls back to opening the file via the OS
    // shell if msiexec spawn fails.
    static void launchAndQuit(const QString &msiPath);

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
};

} // namespace quewi
