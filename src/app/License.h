#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QTimer;

// Trial licensing (docs/dev/licensing.md). A theatre's key is activated once
// online; the server returns a license signed with Ed25519 that names this
// computer and the moment it runs out. quewi checks it offline with the
// public key built in, so a show night needs no internet. Built only into
// release builds (QUEWI_LICENSING); dev and CI builds have it off.
namespace quewi::license {

// What a license says (the signed payload).
struct Claims {
    QString lic;            // the key's id on the server
    QString act;            // this activation's id
    QString keyTail;        // "…-ABCD": enough to recognise the key
    QString name;           // who it's for ("Riverside Players")
    QString machineHash;
    qint64  issuedAt  = 0;  // unix seconds
    qint64  expiresAt = 0;
};

enum class Problem {
    None,
    Missing,          // never activated on this computer
    Malformed,        // not a license
    BadSignature,     // not one the server signed (or tampered with)
    WrongMachine,     // signed for a different computer
    Expired,
    ClockWoundBack,   // the computer's clock is behind a time quewi has already seen
    Revoked,          // the server said so
};

struct Status {
    Problem problem = Problem::Missing;
    Claims  claims;
    qint64  now = 0;
    bool ok() const { return problem == Problem::None; }
    // Whole days left, rounded up (the last day counts); 0 when expired.
    int daysLeft() const;
    // One plain sentence for the activation screen ("Your trial ended on 3 Nov.").
    QString message() const;
};

// Licensing compiled in? (Release builds.) When false, every check passes
// and the activation screen never shows.
bool enabled();
// The server's Ed25519 public key (32 bytes), built in.
QByteArray builtInPublicKey();

// This computer: SHA-256 of a fixed prefix + the OS's machine id (Windows
// MachineGuid, macOS IOPlatformUUID, Linux machine-id), lowercase hex.
// Survives reinstalls and updates.
QString machineHash();
QString machineName();      // shown on the activation screen and to the admin

// "qw-abcd efgh…" -> "QWABCDEFGH…" (what the server compares), and the
// display form "QW-ABCD-EFGH-…".
QString normalizeKey(const QString &key);
QString formatKey(const QString &key);

// Check a license: signature (with `publicKey`), format, machine, time.
// `now` is the later of the clock and the latest time already seen.
Problem verifyToken(const QString &token, const QByteArray &publicKey,
                    const QString &machineHash, qint64 now, Claims *out = nullptr);

// Where the license lives on this computer (QSettings "license/…"), apart
// from the app itself, so updates and reinstalls keep it.
struct Store {
    static QString token();
    static QString key();
    static void    save(const QString &token, const QString &key);
    static void    clear();
    static bool    revoked();
    static void    setRevoked(bool revoked);
    // The latest time quewi has seen (its clock, or the server's). A clock
    // behind this by more than a day has been wound back.
    static qint64  lastSeen();
    static void    noteTime(qint64 unixSeconds);
};

// The stored license, checked now.
Status evaluate(const QByteArray &publicKey = builtInPublicKey());
Status evaluateAt(qint64 now, const QByteArray &publicKey);

// Cheap re-check for the places that matter (GO, open, save): re-verifies
// the stored license at most once a minute. Always true when licensing is
// off.
bool stillValid();
// The startup gate passed (a valid license was in place at start).
void markStartedValid();

// Talking to the license server.
struct Reply {
    bool    ok = false;
    int     httpStatus = 0;
    QString token;          // activate: the signed license
    QString status;         // check: ok / revoked / expired / invalid
    qint64  exp = 0;        // check: the end date the server has now
    qint64  serverTime = 0;
    QString error;          // plain words for the theatre
};

class Client : public QObject {
    Q_OBJECT
public:
    explicit Client(QObject *parent = nullptr);
    // The server's functions URL ("https://….supabase.co/functions/v1").
    void setBaseUrl(const QString &url) { m_base = url; }
    QString baseUrl() const { return m_base; }
    void activate(const QString &key);
    void check(const QString &token);
signals:
    void activated(const quewi::license::Reply &reply);
    void checked(const quewi::license::Reply &reply);
private:
    QNetworkAccessManager *m_net;
    QString m_base;
};

// Runs while quewi is open: checks in with the server at start and every
// 6 hours. A revoked license locks quewi at its NEXT start (never mid-show);
// an end date the admin moved later is picked up by activating again with
// the stored key; the server's time keeps the clock honest.
class Monitor : public QObject {
    Q_OBJECT
public:
    explicit Monitor(QObject *parent = nullptr);
    void start();
signals:
    // For the status bar: "Trial: 3 days left", "This key was revoked…".
    void notice(const QString &text);
private:
    void checkNow();
    Client *m_client;
    QTimer *m_timer;
};

} // namespace quewi::license

Q_DECLARE_METATYPE(quewi::license::Reply)
