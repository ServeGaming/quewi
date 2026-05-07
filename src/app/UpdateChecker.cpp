#include "UpdateChecker.h"

#include <QCoreApplication>
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

    // Find the .msi asset (Windows installer) for the download URL.
    // Fall back to the release page if no MSI is attached yet —
    // sometimes CI is mid-build when a manual check fires.
    QString msiUrl;
    const auto assets = obj.value(QStringLiteral("assets")).toArray();
    for (const auto &v : assets) {
        const auto a = v.toObject();
        const auto name = a.value(QStringLiteral("name")).toString();
        if (name.endsWith(QStringLiteral(".msi"), Qt::CaseInsensitive)) {
            msiUrl = a.value(QStringLiteral("browser_download_url")).toString();
            break;
        }
    }
    const auto pageUrl = obj.value(QStringLiteral("html_url")).toString();
    if (msiUrl.isEmpty()) msiUrl = pageUrl;

    QString tagDisplay = tag;
    if (tagDisplay.startsWith(QChar('v'))) tagDisplay.remove(0, 1);

    emit updateAvailable(tagDisplay, msiUrl, pageUrl, m_mode);
}

} // namespace quewi
