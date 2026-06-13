#pragma once

#include <QList>
#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;
class QProcess;

namespace quewi::ui {

// One search result row from the importer.
struct MediaResult {
    QString id;            // site video id (for thumbnail construction)
    QString url;           // canonical page URL to download/preview
    QString title;
    QString uploader;      // channel / author, may be empty
    qint64  durationSec = 0;
    QString thumbnailUrl;  // may be empty for non-YouTube sources
};

// Wraps the yt-dlp command-line tool for searching, previewing, and
// downloading media from URLs (YouTube and ~1800 other sites). The
// binary is fetched on demand into the app-data folder and can be
// self-updated, so the installer stays small and the tool stays fresh
// (yt-dlp breaks frequently as sites change their internals).
//
// Everything is async over QProcess / QNetworkAccessManager; nothing
// blocks the GUI thread.
class MediaImportService : public QObject {
    Q_OBJECT
public:
    explicit MediaImportService(QObject *parent = nullptr);
    ~MediaImportService() override;

    // Absolute path the yt-dlp binary lives at (whether or not it
    // exists yet).
    QString toolPath() const;
    bool    isToolReady() const;

    // Ensure the tool is present; downloads the latest release if it
    // isn't. Emits toolReady() when usable or toolError() on failure.
    void ensureTool();

    // Force a re-download of the latest yt-dlp (the "Update" button).
    void updateTool();

    // Search the given query (defaults to a YouTube search; a full URL
    // is used directly). Emits searchResults() or searchFailed().
    void search(const QString &query, int maxResults = 20);
    void cancelSearch();

    // Resolve a direct media stream URL for in-app preview (audio).
    // Emits streamUrlReady() / streamUrlFailed().
    void resolveStreamUrl(const QString &pageUrl, bool audioOnly);

    // Download `pageUrl` into `destDir`. audioOnly picks a single
    // pre-muxed audio stream (no ffmpeg merge needed); otherwise a
    // pre-muxed mp4. Emits downloadProgress() then downloadFinished()
    // with the final file path, or downloadFailed().
    void download(const QString &pageUrl, bool audioOnly,
                  const QString &destDir);
    void cancelDownload();

signals:
    void toolReady();
    void toolError(const QString &message);
    void toolDownloadProgress(qint64 received, qint64 total);

    void searchResults(const QList<quewi::ui::MediaResult> &results);
    void searchFailed(const QString &message);

    void streamUrlReady(const QString &directUrl);
    void streamUrlFailed(const QString &message);

    void downloadProgress(int percent);
    void downloadFinished(const QString &filePath);
    void downloadFailed(const QString &message);

private:
    QString platformAssetName() const;   // yt-dlp.exe / yt-dlp_macos / yt-dlp_linux
    void    startToolDownload();
    void    makeExecutable(const QString &path) const;

    QNetworkAccessManager *m_nam = nullptr;
    QNetworkReply         *m_toolReply = nullptr;
    QProcess              *m_search = nullptr;
    QProcess              *m_stream = nullptr;
    QProcess              *m_download = nullptr;
    QString                m_lastDestFile;   // parsed from yt-dlp stdout
};

} // namespace quewi::ui
