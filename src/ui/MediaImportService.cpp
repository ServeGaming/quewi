#include "ui/MediaImportService.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrl>

namespace quewi::ui {

namespace {
// Stable "latest release" download URLs for the yt-dlp standalone
// binaries (no Python install required — they're self-contained).
constexpr const char *kYtDlpLatestBase =
    "https://github.com/yt-dlp/yt-dlp/releases/latest/download/";

// Thumbnails: for YouTube we can build the URL from the video id
// without an extra metadata fetch. Other sites surface their own
// thumbnail in the JSON.
QString youtubeThumb(const QString &id)
{
    if (id.isEmpty()) return {};
    return QStringLiteral("https://i.ytimg.com/vi/%1/mqdefault.jpg").arg(id);
}
} // namespace

MediaImportService::MediaImportService(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
}

MediaImportService::~MediaImportService()
{
    for (QProcess *p : { m_search, m_stream, m_download }) {
        if (p) { p->kill(); p->waitForFinished(500); }
    }
}

QString MediaImportService::platformAssetName() const
{
#if defined(Q_OS_WIN)
    return QStringLiteral("yt-dlp.exe");
#elif defined(Q_OS_MACOS)
    return QStringLiteral("yt-dlp_macos");
#else
    return QStringLiteral("yt-dlp_linux");
#endif
}

QString MediaImportService::toolPath() const
{
    const QString dir = QStandardPaths::writableLocation(
        QStandardPaths::AppDataLocation) + QStringLiteral("/tools");
#if defined(Q_OS_WIN)
    return dir + QStringLiteral("/yt-dlp.exe");
#else
    return dir + QStringLiteral("/yt-dlp");
#endif
}

bool MediaImportService::isToolReady() const
{
    const QFileInfo fi(toolPath());
    return fi.exists() && fi.size() > 0;
}

void MediaImportService::makeExecutable(const QString &path) const
{
#if !defined(Q_OS_WIN)
    QFile f(path);
    f.setPermissions(f.permissions()
        | QFileDevice::ExeOwner | QFileDevice::ExeUser
        | QFileDevice::ExeGroup | QFileDevice::ExeOther);
#else
    Q_UNUSED(path);
#endif
}

void MediaImportService::ensureTool()
{
    if (isToolReady()) { emit toolReady(); return; }
    startToolDownload();
}

void MediaImportService::updateTool()
{
    startToolDownload();
}

void MediaImportService::startToolDownload()
{
    if (m_toolReply) return;   // already in flight
    const QString dir = QFileInfo(toolPath()).absolutePath();
    QDir().mkpath(dir);

    const QString url = QString::fromLatin1(kYtDlpLatestBase) + platformAssetName();
    QNetworkRequest req((QUrl(url)));
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setTransferTimeout(60'000);
    m_toolReply = m_nam->get(req);

    connect(m_toolReply, &QNetworkReply::downloadProgress,
            this, &MediaImportService::toolDownloadProgress);
    connect(m_toolReply, &QNetworkReply::finished, this, [this] {
        QNetworkReply *r = m_toolReply;
        m_toolReply = nullptr;
        r->deleteLater();
        if (r->error() != QNetworkReply::NoError) {
            emit toolError(tr("Couldn't download yt-dlp: %1").arg(r->errorString()));
            return;
        }
        const QByteArray bytes = r->readAll();
        if (bytes.size() < 100000) {   // a real yt-dlp build is multi-MB
            emit toolError(tr("yt-dlp download looks incomplete (%1 bytes).")
                               .arg(bytes.size()));
            return;
        }
        // Write to a temp name then atomically replace, so a partial
        // write never leaves a corrupt 'ready' binary.
        const QString finalPath = toolPath();
        const QString tmpPath = finalPath + QStringLiteral(".part");
        QFile::remove(tmpPath);
        QFile out(tmpPath);
        if (!out.open(QIODevice::WriteOnly)) {
            emit toolError(tr("Couldn't write yt-dlp to %1").arg(tmpPath));
            return;
        }
        out.write(bytes);
        out.close();
        QFile::remove(finalPath);
        if (!QFile::rename(tmpPath, finalPath)) {
            emit toolError(tr("Couldn't finalise yt-dlp at %1").arg(finalPath));
            return;
        }
        makeExecutable(finalPath);
        emit toolReady();
    });
}

void MediaImportService::search(const QString &query, int maxResults)
{
    cancelSearch();
    if (!isToolReady()) {
        emit searchFailed(tr("yt-dlp isn't ready yet."));
        return;
    }
    const QString trimmed = query.trimmed();
    if (trimmed.isEmpty()) { emit searchResults({}); return; }

    // A full URL searches that page; a bare phrase becomes a search.
    const bool isUrl = trimmed.startsWith(QLatin1String("http://"))
                    || trimmed.startsWith(QLatin1String("https://"));
    const QString target = isUrl
        ? trimmed
        : QStringLiteral("ytsearch%1:%2").arg(maxResults).arg(trimmed);

    m_search = new QProcess(this);
    QStringList args{
        QStringLiteral("--dump-json"),
        QStringLiteral("--flat-playlist"),
        QStringLiteral("--no-warnings"),
        QStringLiteral("--ignore-errors"),
        target
    };
    connect(m_search, &QProcess::finished, this,
        [this](int, QProcess::ExitStatus) {
            QProcess *p = m_search;
            m_search = nullptr;
            if (!p) return;
            const QByteArray out = p->readAllStandardOutput();
            const QString err = QString::fromUtf8(p->readAllStandardError());
            p->deleteLater();

            QList<MediaResult> results;
            // --dump-json emits one JSON object per line.
            for (const QByteArray &line : out.split('\n')) {
                if (line.trimmed().isEmpty()) continue;
                const auto doc = QJsonDocument::fromJson(line);
                if (!doc.isObject()) continue;
                const auto o = doc.object();
                MediaResult r;
                r.id          = o.value(QStringLiteral("id")).toString();
                r.title       = o.value(QStringLiteral("title")).toString();
                r.uploader    = o.value(QStringLiteral("uploader")).toString();
                if (r.uploader.isEmpty())
                    r.uploader = o.value(QStringLiteral("channel")).toString();
                r.durationSec = qint64(o.value(QStringLiteral("duration")).toDouble());
                r.url         = o.value(QStringLiteral("url")).toString();
                if (r.url.isEmpty() || !r.url.startsWith(QLatin1String("http"))) {
                    // flat-playlist sometimes gives only an id; build a
                    // canonical watch URL for YouTube.
                    if (!r.id.isEmpty())
                        r.url = QStringLiteral("https://www.youtube.com/watch?v=%1").arg(r.id);
                }
                r.thumbnailUrl = o.value(QStringLiteral("thumbnail")).toString();
                if (r.thumbnailUrl.isEmpty())
                    r.thumbnailUrl = youtubeThumb(r.id);
                if (!r.title.isEmpty() && !r.url.isEmpty())
                    results.append(r);
            }
            if (results.isEmpty() && !err.isEmpty()) {
                emit searchFailed(err.section('\n', 0, 0));
            } else {
                emit searchResults(results);
            }
        });
    m_search->start(toolPath(), args);
}

void MediaImportService::cancelSearch()
{
    if (m_search) { m_search->kill(); m_search->deleteLater(); m_search = nullptr; }
}

void MediaImportService::resolveStreamUrl(const QString &pageUrl, bool audioOnly)
{
    if (!isToolReady()) { emit streamUrlFailed(tr("yt-dlp isn't ready yet.")); return; }
    if (m_stream) { m_stream->kill(); m_stream->deleteLater(); m_stream = nullptr; }

    m_stream = new QProcess(this);
    const QString fmt = audioOnly
        ? QStringLiteral("bestaudio/best")
        : QStringLiteral("best[ext=mp4]/best");
    QStringList args{ QStringLiteral("-g"), QStringLiteral("-f"), fmt,
                      QStringLiteral("--no-warnings"), pageUrl };
    connect(m_stream, &QProcess::finished, this,
        [this](int, QProcess::ExitStatus) {
            QProcess *p = m_stream;
            m_stream = nullptr;
            if (!p) return;
            const QString out = QString::fromUtf8(p->readAllStandardOutput()).trimmed();
            const QString err = QString::fromUtf8(p->readAllStandardError());
            p->deleteLater();
            // -g can print multiple URLs (video+audio); the first is
            // what we want for an audio preview.
            const QString first = out.section('\n', 0, 0).trimmed();
            if (first.startsWith(QLatin1String("http")))
                emit streamUrlReady(first);
            else
                emit streamUrlFailed(err.section('\n', 0, 0));
        });
    m_stream->start(toolPath(), args);
}

void MediaImportService::download(const QString &pageUrl, bool audioOnly,
                                  const QString &destDir)
{
    cancelDownload();
    if (!isToolReady()) { emit downloadFailed(tr("yt-dlp isn't ready yet.")); return; }
    QDir().mkpath(destDir);
    m_lastDestFile.clear();

    m_download = new QProcess(this);
    m_download->setProcessChannelMode(QProcess::MergedChannels);
    const QString fmt = audioOnly
        ? QStringLiteral("bestaudio/best")
        : QStringLiteral("best[ext=mp4]/best");
    const QString outTemplate =
        destDir + QStringLiteral("/%(title).80B [%(id)s].%(ext)s");
    QStringList args{
        QStringLiteral("-f"), fmt,
        QStringLiteral("--no-playlist"),
        QStringLiteral("--no-part"),
        QStringLiteral("--newline"),       // progress on its own lines
        QStringLiteral("-o"), outTemplate,
        pageUrl
    };

    connect(m_download, &QProcess::readyReadStandardOutput, this, [this] {
        if (!m_download) return;
        const QString chunk = QString::fromUtf8(m_download->readAllStandardOutput());
        static const QRegularExpression pctRe(
            QStringLiteral("\\[download\\]\\s+(\\d+(?:\\.\\d+)?)%"));
        static const QRegularExpression destRe(
            QStringLiteral("\\[download\\]\\s+Destination:\\s+(.+)$"));
        static const QRegularExpression alreadyRe(
            QStringLiteral("\\[download\\]\\s+(.+?) has already been downloaded"));
        static const QRegularExpression mergeRe(
            QStringLiteral("Merging formats into \"(.+?)\""));
        for (const QString &line : chunk.split('\n')) {
            auto m = pctRe.match(line);
            if (m.hasMatch())
                emit downloadProgress(int(m.captured(1).toDouble()));
            auto d = destRe.match(line);
            if (d.hasMatch()) m_lastDestFile = d.captured(1).trimmed();
            auto a = alreadyRe.match(line);
            if (a.hasMatch()) m_lastDestFile = a.captured(1).trimmed();
            auto g = mergeRe.match(line);
            if (g.hasMatch()) m_lastDestFile = g.captured(1).trimmed();
        }
    });
    connect(m_download, &QProcess::finished, this,
        [this, destDir](int code, QProcess::ExitStatus) {
            QProcess *p = m_download;
            m_download = nullptr;
            if (!p) return;
            const QString tail = QString::fromUtf8(p->readAllStandardOutput());
            p->deleteLater();
            if (code != 0) {
                emit downloadFailed(tail.section('\n', -2, -1).trimmed().isEmpty()
                    ? tr("Download failed (exit code %1).").arg(code)
                    : tail.section('\n', -2, -1).trimmed());
                return;
            }
            QString file = m_lastDestFile;
            if (file.isEmpty() || !QFileInfo::exists(file)) {
                // Fallback: newest file in destDir.
                QDir d(destDir);
                const auto entries = d.entryInfoList(QDir::Files, QDir::Time);
                if (!entries.isEmpty()) file = entries.first().absoluteFilePath();
            }
            if (file.isEmpty() || !QFileInfo::exists(file))
                emit downloadFailed(tr("Download finished but the file "
                                       "couldn't be located."));
            else
                emit downloadFinished(file);
        });
    emit downloadProgress(0);
    m_download->start(toolPath(), args);
}

void MediaImportService::cancelDownload()
{
    if (m_download) {
        m_download->kill();
        m_download->deleteLater();
        m_download = nullptr;
    }
}

} // namespace quewi::ui
