#include "License.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QHostInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QSysInfo>
#include <QTimer>

#include <algorithm>

extern "C" {
#include "monocypher-ed25519.h"
}

#ifndef QUEWI_LICENSE_PUBKEY
#  define QUEWI_LICENSE_PUBKEY ""
#endif
#ifndef QUEWI_LICENSE_SERVER
#  define QUEWI_LICENSE_SERVER "https://mdvoeinmfjcyedvuffwq.supabase.co/functions/v1"
#endif

namespace quewi::license {

namespace {

QSettings settings() { return QSettings(QStringLiteral("ServeGaming"), QStringLiteral("quewi")); }

qint64 nowSecs() { return QDateTime::currentSecsSinceEpoch(); }

// A day's slack before a clock behind the latest seen time counts as wound
// back (time zones, a dead CMOS battery fixed by the next sync, …).
constexpr qint64 kClockSlack = 24 * 3600;

QString tr(const char *s) { return QCoreApplication::translate("License", s); }

QString dateText(qint64 secs)
{
    return QLocale().toString(QDateTime::fromSecsSinceEpoch(secs).date(), QLocale::LongFormat);
}

} // namespace

bool enabled()
{
#ifdef QUEWI_LICENSING
    return true;
#else
    return false;
#endif
}

QByteArray builtInPublicKey()
{
    return QByteArray::fromHex(QByteArrayLiteral(QUEWI_LICENSE_PUBKEY));
}

QString machineHash()
{
    QByteArray id = QSysInfo::machineUniqueId();
    // Some Linux containers have no machine id: fall back to the host name,
    // which is at least stable on one computer.
    if (id.isEmpty()) id = "host:" + QSysInfo::machineHostName().toUtf8();
    return QString::fromLatin1(
        QCryptographicHash::hash("quewi-machine-v1:" + id, QCryptographicHash::Sha256).toHex());
}

QString machineName()
{
    const QString host = QSysInfo::machineHostName();
    return host.isEmpty() ? QSysInfo::prettyProductName()
                          : QStringLiteral("%1 (%2)").arg(host, QSysInfo::prettyProductName());
}

QString normalizeKey(const QString &key)
{
    QString out;
    for (const QChar c : key)
        if (c.isLetterOrNumber()) out += c.toUpper();
    return out;
}

QString formatKey(const QString &key)
{
    const QString n = normalizeKey(key);
    if (!n.startsWith(QLatin1String("QW"))) return n;
    QString out = QStringLiteral("QW");
    for (int i = 2; i < n.size(); i += 4) out += QLatin1Char('-') + n.mid(i, 4);
    return out;
}

Problem verifyToken(const QString &token, const QByteArray &publicKey,
                    const QString &machine, qint64 now, Claims *out)
{
    if (token.isEmpty()) return Problem::Missing;
    const auto parts = token.trimmed().split(QLatin1Char('.'));
    if (parts.size() != 2 || publicKey.size() != 32) return Problem::Malformed;
    const auto opts = QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals;
    const auto payload = QByteArray::fromBase64Encoding(parts[0].toLatin1(), opts);
    const auto sig     = QByteArray::fromBase64Encoding(parts[1].toLatin1(), opts);
    if (!payload || !sig || sig.decoded.size() != 64) return Problem::Malformed;

    // The signature first: nothing in an unsigned payload is believed.
    if (crypto_ed25519_check(reinterpret_cast<const uint8_t *>(sig.decoded.constData()),
                             reinterpret_cast<const uint8_t *>(publicKey.constData()),
                             reinterpret_cast<const uint8_t *>(payload.decoded.constData()),
                             size_t(payload.decoded.size())) != 0)
        return Problem::BadSignature;

    const auto doc = QJsonDocument::fromJson(payload.decoded);
    if (!doc.isObject()) return Problem::Malformed;
    const auto o = doc.object();
    if (o.value(QStringLiteral("v")).toInt() != 1) return Problem::Malformed;
    Claims c;
    c.lic         = o.value(QStringLiteral("lic")).toString();
    c.act         = o.value(QStringLiteral("act")).toString();
    c.keyTail     = o.value(QStringLiteral("key")).toString();
    c.name        = o.value(QStringLiteral("name")).toString();
    c.machineHash = o.value(QStringLiteral("mh")).toString();
    c.issuedAt    = qint64(o.value(QStringLiteral("iat")).toDouble());
    c.expiresAt   = qint64(o.value(QStringLiteral("exp")).toDouble());
    if (out) *out = c;
    if (c.lic.isEmpty() || c.expiresAt <= 0) return Problem::Malformed;
    if (c.machineHash != machine) return Problem::WrongMachine;
    if (now >= c.expiresAt) return Problem::Expired;
    return Problem::None;
}

// ── Store ───────────────────────────────────────────────────────────────────

QString Store::token()  { return settings().value(QStringLiteral("license/token")).toString(); }
QString Store::key()    { return settings().value(QStringLiteral("license/key")).toString(); }
bool    Store::revoked() { return settings().value(QStringLiteral("license/revoked"), false).toBool(); }

void Store::save(const QString &token, const QString &key)
{
    auto s = settings();
    s.setValue(QStringLiteral("license/token"), token);
    if (!key.isEmpty()) s.setValue(QStringLiteral("license/key"), normalizeKey(key));
    s.setValue(QStringLiteral("license/revoked"), false);
}

void Store::clear()
{
    auto s = settings();
    s.remove(QStringLiteral("license/token"));
    s.remove(QStringLiteral("license/key"));
    s.remove(QStringLiteral("license/revoked"));
}

void Store::setRevoked(bool r) { settings().setValue(QStringLiteral("license/revoked"), r); }

qint64 Store::lastSeen() { return settings().value(QStringLiteral("license/seen"), 0).toLongLong(); }

void Store::noteTime(qint64 t)
{
    if (t > lastSeen()) settings().setValue(QStringLiteral("license/seen"), t);
}

// ── Status ──────────────────────────────────────────────────────────────────

int Status::daysLeft() const
{
    if (claims.expiresAt <= now) return 0;
    return int((claims.expiresAt - now + 86399) / 86400);
}

QString Status::message() const
{
    switch (problem) {
    case Problem::None:
        // A master key (the owner's) runs for years: no countdown.
        if (daysLeft() > 5 * 365) return tr("This computer is licensed.");
        return daysLeft() == 1 ? tr("Your trial runs until %1 (today is the last day).").arg(dateText(claims.expiresAt))
                               : tr("Your trial runs until %1 (%2 days left).").arg(dateText(claims.expiresAt)).arg(daysLeft());
    case Problem::Missing:
        return tr("Enter your trial key to start quewi.");
    case Problem::Expired:
        return tr("Your trial ended on %1. Enter a new key to keep using quewi.").arg(dateText(claims.expiresAt));
    case Problem::WrongMachine:
        return tr("This license belongs to a different computer. Enter a key for this one.");
    case Problem::ClockWoundBack:
        return tr("This computer's clock is behind. Set the date and time correctly (or connect to the internet) and try again.");
    case Problem::Revoked:
        return tr("This trial key has been turned off. Contact us for a new one.");
    case Problem::Malformed:
    case Problem::BadSignature:
        return tr("The saved license isn't valid. Enter your trial key again.");
    }
    return {};
}

Status evaluateAt(qint64 now, const QByteArray &publicKey)
{
    Status st;
    st.now = now;
    const qint64 seen = Store::lastSeen();
    if (seen > 0 && now + kClockSlack < seen) {
        st.problem = Problem::ClockWoundBack;
        verifyToken(Store::token(), publicKey, machineHash(), seen, &st.claims);
        return st;
    }
    // Judge time by the later of the clock and what's been seen before.
    const qint64 t = std::max(now, seen);
    st.now = t;
    st.problem = verifyToken(Store::token(), publicKey, machineHash(), t, &st.claims);
    if (st.problem == Problem::None && Store::revoked()) st.problem = Problem::Revoked;
    return st;
}

Status evaluate(const QByteArray &publicKey)
{
    const qint64 now = nowSecs();
    const Status st = evaluateAt(now, publicKey);
    if (st.problem != Problem::ClockWoundBack) Store::noteTime(now);
    return st;
}

namespace { bool g_startedValid = false; }

void markStartedValid() { g_startedValid = true; }

bool stillValid()
{
    if (!enabled()) return true;
    static qint64 checkedAt = 0;
    static bool   good = false;
    const qint64 now = nowSecs();
    if (now - checkedAt >= 60 || now < checkedAt) {
        checkedAt = now;
        const Problem p = verifyToken(Store::token(), builtInPublicKey(), machineHash(),
                                      std::max(now, Store::lastSeen()));
        // Genuine and for this computer. A trial that runs out (or is
        // turned off) while quewi is open lasts until it's closed: GO must
        // never stop in the middle of a show.
        good = g_startedValid && (p == Problem::None || p == Problem::Expired);
    }
    return good;
}

// ── Client ──────────────────────────────────────────────────────────────────

Client::Client(QObject *parent)
    : QObject(parent)
    , m_net(new QNetworkAccessManager(this))
    , m_base(QString::fromUtf8(QUEWI_LICENSE_SERVER))
{
}

namespace {

Reply readReply(QNetworkReply *r)
{
    Reply out;
    out.httpStatus = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const auto body = QJsonDocument::fromJson(r->readAll()).object();
    out.token      = body.value(QStringLiteral("token")).toString();
    out.status     = body.value(QStringLiteral("status")).toString();
    out.exp        = qint64(body.value(QStringLiteral("exp")).toDouble());
    out.serverTime = qint64(body.value(QStringLiteral("serverTime")).toDouble());
    const QString err = body.value(QStringLiteral("error")).toString();
    if (r->error() != QNetworkReply::NoError && out.httpStatus == 0) {
        out.error = tr("Couldn't reach the license server (%1). Check the internet connection and try again.")
                        .arg(r->errorString());
    } else if (out.httpStatus == 404) {
        out.error = tr("That key wasn't found. Check it for typos (letters and numbers only; dashes don't matter).");
    } else if (out.httpStatus == 409) {
        out.error = tr("That key is already in use on another computer. Each key works on one computer.");
    } else if (out.httpStatus == 403 && err.contains(QLatin1String("expired"), Qt::CaseInsensitive)) {
        out.error = tr("That key's trial has ended.");
    } else if (out.httpStatus == 403) {
        out.error = tr("That key has been turned off.");
    } else if (out.httpStatus >= 400) {
        out.error = err.isEmpty() ? tr("The license server said no (error %1).").arg(out.httpStatus) : err;
    }
    out.ok = out.httpStatus == 200 && out.error.isEmpty();
    return out;
}

QNetworkRequest jsonRequest(const QString &url)
{
    QNetworkRequest req{QUrl(url)};
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setTransferTimeout(20000);
    return req;
}

} // namespace

void Client::activate(const QString &key)
{
    const QJsonObject body{
        {QStringLiteral("key"),         normalizeKey(key)},
        {QStringLiteral("machineHash"), machineHash()},
        {QStringLiteral("machineName"), machineName()},
        {QStringLiteral("appVersion"),  QCoreApplication::applicationVersion()},
    };
    auto *r = m_net->post(jsonRequest(m_base + QStringLiteral("/activate")), QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(r, &QNetworkReply::finished, this, [this, r] {
        r->deleteLater();
        emit activated(readReply(r));
    });
}

void Client::check(const QString &token)
{
    const QJsonObject body{{QStringLiteral("token"), token}};
    auto *r = m_net->post(jsonRequest(m_base + QStringLiteral("/check")), QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(r, &QNetworkReply::finished, this, [this, r] {
        r->deleteLater();
        emit checked(readReply(r));
    });
}

// ── Monitor ─────────────────────────────────────────────────────────────────

Monitor::Monitor(QObject *parent)
    : QObject(parent)
    , m_client(new Client(this))
    , m_timer(new QTimer(this))
{
    m_timer->setInterval(6 * 3600 * 1000);
    connect(m_timer, &QTimer::timeout, this, &Monitor::checkNow);
    connect(m_client, &Client::checked, this, [this](const Reply &r) {
        if (r.httpStatus == 0) return;                       // offline: fine, try later
        if (r.serverTime > 0) Store::noteTime(r.serverTime); // the server's clock keeps ours honest
        if (r.status == QLatin1String("revoked")) {
            Store::setRevoked(true);
            emit notice(tr("This trial key has been turned off. quewi will keep running now, "
                           "but will ask for a new key next time it starts."));
            return;
        }
        if (r.status == QLatin1String("expired") || r.status == QLatin1String("invalid")) {
            // Ended on the server (the admin shortened it, or it's simply over):
            // like a revoke, it takes effect at the next start.
            Store::setRevoked(true);
            emit notice(tr("This trial has ended. quewi will keep running now, but will ask for a new key next time it starts."));
            return;
        }
        const Status st = evaluateAt(QDateTime::currentSecsSinceEpoch(), builtInPublicKey());
        // The admin moved the end date (later or earlier): fetch a license
        // that says the new one.
        if (r.exp > 0 && r.exp != st.claims.expiresAt && !Store::key().isEmpty()) {
            m_client->activate(Store::key());
            return;
        }
        if (st.ok() && st.daysLeft() <= 3)
            emit notice(st.message());
    });
    connect(m_client, &Client::activated, this, [this](const Reply &r) {
        if (!r.ok || r.token.isEmpty()) return;
        Claims c;
        if (verifyToken(r.token, builtInPublicKey(), machineHash(),
                        std::max(QDateTime::currentSecsSinceEpoch(), Store::lastSeen()), &c) == Problem::None) {
            Store::save(r.token, Store::key());
            emit notice(tr("Your trial now runs until %1.").arg(dateText(c.expiresAt)));
        } else if (r.httpStatus == 403) {
            Store::setRevoked(true);      // re-activating was refused: ended or turned off
        }
    });
}

void Monitor::start()
{
    if (!enabled()) return;
    m_timer->start();
    QTimer::singleShot(3000, this, &Monitor::checkNow);
    const Status st = evaluate();
    if (st.ok() && st.daysLeft() <= 3) emit notice(st.message());
}

void Monitor::checkNow()
{
    const QString t = Store::token();
    if (!t.isEmpty()) m_client->check(t);
}

} // namespace quewi::license
