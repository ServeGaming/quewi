#pragma once

#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

namespace quewi {

// Polls the project's GitHub Releases API and compares the latest
// published tag against the running build's version. Used both at
// startup (silent mode — only surface a dialog if there's actually an
// update) and from File → Check for updates… (verbose mode — confirm
// "you're up to date" even when there isn't one).
//
// The "download" action performs an in-app install (see
// UpdateInstaller): the portable ZIP is swapped in place where the
// install dir is writable, falling back to re-running the .msi with
// elevation otherwise.
class UpdateChecker : public QObject {
    Q_OBJECT
public:
    explicit UpdateChecker(QObject *parent = nullptr);
    ~UpdateChecker() override;

    enum class Mode { Silent, Verbose };

    // Kicks off an async HTTP GET to the GitHub releases API. The
    // result lands on one of the signals below. Multiple in-flight
    // checks are coalesced — calling start() again before the previous
    // one finishes is a no-op.
    void start(Mode mode);

    QString currentVersion() const;

signals:
    // A release strictly newer than this build was found.
    void updateAvailable(const QString &version,
                         const QString &downloadUrl,
                         const QString &releaseUrl,
                         quewi::UpdateChecker::Mode mode);
    // No newer release exists. Only meaningful in Verbose mode.
    void upToDate(quewi::UpdateChecker::Mode mode);
    // Network or parse failure. Verbose mode shows it; Silent mode
    // logs and ignores so a flaky internet connection at launch
    // doesn't pop dialogs at the operator.
    void checkFailed(const QString &reason, quewi::UpdateChecker::Mode mode);

private slots:
    void onReplyFinished();

private:
    QNetworkAccessManager *m_nam = nullptr;
    QNetworkReply         *m_reply = nullptr;
    Mode                   m_mode = Mode::Silent;
};

} // namespace quewi
