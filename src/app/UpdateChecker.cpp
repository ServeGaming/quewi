#include "UpdateChecker.h"

#include <QCoreApplication>
#include <QFile>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStringList>
#include <QUrl>

#ifndef QUEWI_VERSION
#  define QUEWI_VERSION "0.0.0"
#endif

namespace quewi {

namespace {

// Parse "1.2.3" or "v1.2.3" into a 3-element vector. Trailing pre-release
// labels ("-rc1", "+build4") are dropped — close-enough for >= comparison
// against published tags. Empty / malformed input returns {0,0,0} so a
// bad tag never falsely advertises an "update".
QList<int> parseVersion(QString s)
{
    s = s.trimmed();
    if (s.startsWith(QChar('v')) || s.startsWith(QChar('V'))) s.remove(0, 1);
    const int dash = s.indexOf(QChar('-'));
    if (dash >= 0) s.truncate(dash);
    const int plus = s.indexOf(QChar('+'));
    if (plus >= 0) s.truncate(plus);
    QList<int> v{0, 0, 0};
    const auto parts = s.split(QChar('.'));
    for (int i = 0; i < std::min<int>(3, parts.size()); ++i) {
        v[i] = parts[i].toInt();
    }
    return v;
}

bool isStrictlyNewer(const QList<int> &remote, const QList<int> &local)
{
    for (int i = 0; i < 3; ++i) {
        if (remote[i] > local[i]) return true;
        if (remote[i] < local[i]) return false;
    }
    return false;
}

} // namespace

UpdateChecker::UpdateChecker(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
}

UpdateChecker::~UpdateChecker() = default;

QString UpdateChecker::currentVersion() const
{
    return QStringLiteral(QUEWI_VERSION);
}

void UpdateChecker::start(Mode mode)
{
    if (m_reply) return;   // already in flight
    m_mode = mode;

    QNetworkRequest req(QUrl(QStringLiteral(
        "https://api.github.com/repos/ServeGaming/quewi/releases/latest")));
    // GitHub asks for a User-Agent header on API requests; without it the
    // request is rejected with "Missing User-Agent". Identify the build
    // so anyone watching their access log can see who's polling.
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("quewi/%1 (UpdateChecker)")
                      .arg(QStringLiteral(QUEWI_VERSION)));
    req.setRawHeader("Accept", "application/vnd.github+json");
    // Five-second redirect/timeout window — keeps an offline FOH from
    // sitting on a stalled HTTP connection.
    req.setTransferTimeout(5000);

    m_reply = m_nam->get(req);
    connect(m_reply, &QNetworkReply::finished,
            this, &UpdateChecker::onReplyFinished);
}

void UpdateChecker::onReplyFinished()
{
    QNetworkReply *reply = m_reply;
    m_reply = nullptr;
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        emit checkFailed(reply->errorString(), m_mode);
        return;
    }
    const auto bytes = reply->readAll();
    const auto doc = QJsonDocument::fromJson(bytes);
    if (!doc.isObject()) {
        emit checkFailed(tr("Couldn't parse GitHub response."), m_mode);
        return;
    }
    const auto obj = doc.object();
    const auto tag = obj.value(QStringLiteral("tag_name")).toString();
    if (tag.isEmpty()) {
        emit checkFailed(tr("No tag in latest release."), m_mode);
        return;
    }
    const auto remote = parseVersion(tag);
    const auto local  = parseVersion(QStringLiteral(QUEWI_VERSION));
    if (!isStrictlyNewer(remote, local)) {
        emit upToDate(m_mode);
        return;
    }

    // Find the platform-appropriate installer asset:
    //   Windows → .zip (portable) if our install dir is writable,
    //             else .msi (installed flow)
    //   macOS   → .dmg
    //   Linux   → .AppImage
    // The portable-zip preference on Windows means an install that
    // can self-update (user-writable folder, not Program Files)
    // picks up the artifact UpdateInstaller can swap in place. MSI
    // installs to Program Files fall through to .msi and re-run the
    // installer the old way.
    QString preferredExt;
    QString fallbackExt;
#if defined(Q_OS_WIN)
    // Probe install dir for write access — same check
    // UpdateInstaller does at swap time, replicated here so we
    // download the right artifact in the first place.
    {
        const QString installDir = QCoreApplication::applicationDirPath();
        const QString writeProbe = installDir + QStringLiteral("/.quewi-write-probe");
        bool writable = false;
        QFile probe(writeProbe);
        if (probe.open(QIODevice::WriteOnly)) {
            probe.close();
            QFile::remove(writeProbe);
            writable = true;
        }
        if (writable) {
            preferredExt = QStringLiteral(".zip");
            fallbackExt  = QStringLiteral(".msi");
        } else {
            preferredExt = QStringLiteral(".msi");
        }
    }
#elif defined(Q_OS_MACOS)
    preferredExt = QStringLiteral(".dmg");
#elif defined(Q_OS_LINUX)
    preferredExt = QStringLiteral(".AppImage");
#endif

    QString msiUrl;
    const auto assets = obj.value(QStringLiteral("assets")).toArray();
    // Two-pass: preferred extension first, then fallback if no match.
    auto findAsset = [&assets](const QString &ext) -> QString {
        if (ext.isEmpty()) return QString();
        for (const auto &v : assets) {
            const auto a = v.toObject();
            const auto name = a.value(QStringLiteral("name")).toString();
            if (name.endsWith(ext, Qt::CaseInsensitive)) {
                return a.value(QStringLiteral("browser_download_url")).toString();
            }
        }
        return QString();
    };
    msiUrl = findAsset(preferredExt);
    if (msiUrl.isEmpty()) msiUrl = findAsset(fallbackExt);
    const auto pageUrl = obj.value(QStringLiteral("html_url")).toString();
    // DELIBERATELY leave msiUrl empty when no platform asset matched.
    // The earlier code fell back to pageUrl here, which led to the
    // UpdateInstaller cheerfully downloading the GitHub release page
    // HTML (~200 KB) and then failing the MSI 5 MB size floor with
    // "file looks incomplete" — confusing the operator into thinking
    // the network was bad. Now MainWindow can branch on
    // msiUrl.isEmpty() and explain that the installer hasn't been
    // built yet (CI typically takes ~10 minutes after a tag push).

    QString tagDisplay = tag;
    if (tagDisplay.startsWith(QChar('v'))) tagDisplay.remove(0, 1);

    emit updateAvailable(tagDisplay, msiUrl, pageUrl, m_mode);
}

} // namespace quewi
