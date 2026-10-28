#include <QTest>
#include <QApplication>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>

#include "app/ActivationDialog.h"
#include "app/License.h"

extern "C" {
#include "monocypher-ed25519.h"
}

using namespace quewi;
using namespace quewi::license;

// Trial licenses: signed by the server (here, by a throwaway key made in the
// test, exactly the way the server signs), checked offline by quewi.
class LicenseTests : public QObject {
    Q_OBJECT

    uint8_t m_secret[64]{};
    QByteArray m_public;
    QVariantMap m_saved;     // the real license/* settings, put back after

    static QSettings settings() { return QSettings(QStringLiteral("ServeGaming"), QStringLiteral("quewi")); }

    QString sign(const QJsonObject &payload, const uint8_t *secret = nullptr) const
    {
        const QByteArray json = QJsonDocument(payload).toJson(QJsonDocument::Compact);
        uint8_t sig[64];
        crypto_ed25519_sign(sig, secret ? secret : m_secret,
                            reinterpret_cast<const uint8_t *>(json.constData()), size_t(json.size()));
        const auto opts = QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals;
        return QString::fromLatin1(json.toBase64(opts) + '.'
                                   + QByteArray(reinterpret_cast<const char *>(sig), 64).toBase64(opts));
    }
    QJsonObject claims(qint64 exp, const QString &machine = machineHash()) const
    {
        return QJsonObject{{"v", 1}, {"lic", "lic-1"}, {"act", "act-1"}, {"key", "…-ABCD"},
                           {"name", "Riverside Players"}, {"mh", machine},
                           {"iat", double(QDateTime::currentSecsSinceEpoch())}, {"exp", double(exp)}};
    }
    static qint64 now() { return QDateTime::currentSecsSinceEpoch(); }

    // A one-shot HTTP server answering with `status` and `body`.
    struct FakeServer {
        QTcpServer server;
        int status = 200;
        QByteArray body;
        QByteArray lastRequest;
        FakeServer()
        {
            server.listen(QHostAddress::LocalHost, 0);
            QObject::connect(&server, &QTcpServer::newConnection, [this] {
                auto *s = server.nextPendingConnection();
                QObject::connect(s, &QTcpSocket::readyRead, s, [this, s] {
                    lastRequest += s->readAll();
                    if (!lastRequest.contains("\r\n\r\n") || !lastRequest.contains("}")) return;
                    s->write("HTTP/1.1 " + QByteArray::number(status) + " X\r\nContent-Type: application/json\r\n"
                             "Content-Length: " + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                    s->disconnectFromHost();
                });
            });
        }
        QString url() const { return QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()); }
    };

private slots:
    void initTestCase()
    {
        uint8_t seed[32];
        for (int i = 0; i < 32; ++i) seed[i] = uint8_t(i * 7 + 3);
        uint8_t pub[32];
        crypto_ed25519_key_pair(m_secret, pub, seed);
        m_public = QByteArray(reinterpret_cast<const char *>(pub), 32);
        auto s = settings();
        for (const char *k : {"license/token", "license/key", "license/revoked", "license/seen"})
            if (s.contains(QLatin1String(k))) m_saved.insert(QLatin1String(k), s.value(QLatin1String(k)));
    }
    void cleanupTestCase()
    {
        auto s = settings();
        for (const char *k : {"license/token", "license/key", "license/revoked", "license/seen"})
            s.remove(QLatin1String(k));
        for (auto it = m_saved.cbegin(); it != m_saved.cend(); ++it) s.setValue(it.key(), it.value());
    }
    void init() { cleanupTestCase(); auto s = settings();
                  for (const char *k : {"license/token", "license/key", "license/revoked", "license/seen"})
                      s.remove(QLatin1String(k)); }

    void aSignedLicenseChecksOutOffline()
    {
        const QString t = sign(claims(now() + 14 * 86400));
        Claims c;
        QCOMPARE(verifyToken(t, m_public, machineHash(), now(), &c), Problem::None);
        QCOMPARE(c.name, QStringLiteral("Riverside Players"));
        QCOMPARE(c.lic, QStringLiteral("lic-1"));
    }

    void anythingElseIsRefused()
    {
        const QString good = sign(claims(now() + 86400));
        // Tampered: the end date pushed out, signature kept.
        auto parts = good.split('.');
        auto payload = claims(now() + 400 * 86400);
        const auto opts = QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals;
        const QString tampered = QString::fromLatin1(QJsonDocument(payload).toJson(QJsonDocument::Compact).toBase64(opts))
                               + '.' + parts[1];
        QCOMPARE(verifyToken(tampered, m_public, machineHash(), now()), Problem::BadSignature);
        // Signed by someone else's key.
        uint8_t otherSecret[64], otherPub[32], seed[32] = {1};
        crypto_ed25519_key_pair(otherSecret, otherPub, seed);
        QCOMPARE(verifyToken(sign(claims(now() + 86400), otherSecret), m_public, machineHash(), now()),
                 Problem::BadSignature);
        // Another computer's.
        QCOMPARE(verifyToken(sign(claims(now() + 86400, QStringLiteral("ab12"))), m_public, machineHash(), now()),
                 Problem::WrongMachine);
        // Run out.
        QCOMPARE(verifyToken(sign(claims(now() - 10)), m_public, machineHash(), now()), Problem::Expired);
        // Not a license at all.
        QCOMPARE(verifyToken(QStringLiteral("hello"), m_public, machineHash(), now()), Problem::Malformed);
        QCOMPARE(verifyToken(QString(), m_public, machineHash(), now()), Problem::Missing);
    }

    void theClockCantBeWoundBack()
    {
        Store::save(sign(claims(now() + 3 * 86400)), QStringLiteral("QW-ABCD"));
        QVERIFY(evaluateAt(now(), m_public).ok());
        // quewi has already seen a time 5 days from now (e.g. from the
        // server): a clock saying "now" was wound back.
        Store::noteTime(now() + 5 * 86400);
        QCOMPARE(evaluateAt(now(), m_public).problem, Problem::ClockWoundBack);
        // A clock a few hours behind is fine (time zones, a late sync)…
        auto s = settings();
        s.setValue(QStringLiteral("license/seen"), now() + 3600);
        QVERIFY(evaluateAt(now(), m_public).ok());
        // …and time is judged by the later of the two: seen past the end = expired.
        s.setValue(QStringLiteral("license/seen"), now() + 3 * 86400 + 60);
        QCOMPARE(evaluateAt(now() + 3 * 86400 + 60, m_public).problem, Problem::Expired);
    }

    void revokedAndDaysLeft()
    {
        Store::save(sign(claims(now() + 2 * 86400 + 100)), QStringLiteral("qw abcd efgh"));
        auto st = evaluateAt(now(), m_public);
        QVERIFY(st.ok());
        QCOMPARE(st.daysLeft(), 3);              // the part-day counts
        QCOMPARE(Store::key(), QStringLiteral("QWABCDEFGH"));
        Store::setRevoked(true);
        QCOMPARE(evaluateAt(now(), m_public).problem, Problem::Revoked);
        Store::save(sign(claims(now() + 86400)), QStringLiteral("QW-NEW"));   // a new key clears it
        QVERIFY(evaluateAt(now(), m_public).ok());
    }

    void keys()
    {
        QCOMPARE(normalizeKey(QStringLiteral(" qw-ab3d efgh-jk ")), QStringLiteral("QWAB3DEFGHJK"));
        QCOMPARE(formatKey(QStringLiteral("qwabcdefghjkmnpqrs")), QStringLiteral("QW-ABCD-EFGH-JKMN-PQRS"));
        QCOMPARE(machineHash().size(), 64);
        QCOMPARE(machineHash(), machineHash());   // stable
    }

    void theServersAnswersInPlainWords()
    {
        FakeServer srv;
        Client c;
        c.setBaseUrl(srv.url());
        QSignalSpy got(&c, &Client::activated);
        srv.status = 409;
        srv.body = R"({"error":"used on another computer"})";
        c.activate(QStringLiteral("QW-AAAA"));
        QVERIFY(got.wait(5000));
        auto r = got.last().at(0).value<Reply>();
        QVERIFY(!r.ok);
        QVERIFY2(r.error.contains(QStringLiteral("another computer")), qPrintable(r.error));
        QVERIFY(srv.lastRequest.contains(machineHash().toLatin1()));
        QVERIFY(srv.lastRequest.contains("\"key\":\"QWAAAA\""));

        FakeServer srv2;
        c.setBaseUrl(srv2.url());
        srv2.status = 404;
        srv2.body = R"({"error":"unknown key"})";
        c.activate(QStringLiteral("QW-BBBB"));
        QVERIFY(got.wait(5000));
        QVERIFY(got.last().at(0).value<Reply>().error.contains(QStringLiteral("wasn't found")));

        // Nobody listening: says so, not a crash.
        c.setBaseUrl(QStringLiteral("http://127.0.0.1:1"));
        c.activate(QStringLiteral("QW-CCCC"));
        QVERIFY(got.wait(25000));
        r = got.last().at(0).value<Reply>();
        QCOMPARE(r.httpStatus, 0);
        QVERIFY(r.error.contains(QStringLiteral("Couldn't reach")));
    }

    void theActivationScreenActivates()
    {
        FakeServer srv;
        srv.body = QJsonDocument(QJsonObject{{"token", sign(claims(now() + 14 * 86400))},
                                             {"serverTime", double(now())}}).toJson(QJsonDocument::Compact);
        Status st;     // Missing
        ActivationDialog dlg(st);
        dlg.setPublicKey(m_public);
        dlg.client()->setBaseUrl(srv.url());
        dlg.show();
        auto *key = dlg.findChild<QLineEdit *>(QStringLiteral("licenseKey"));
        QVERIFY(key);
        key->setText(QStringLiteral("qw-abcd-efgh-jkmn-pqrs"));
        QSignalSpy done(&dlg, &QDialog::accepted);
        QSignalSpy quit(&dlg, &QDialog::rejected);
        QTest::keyClick(key, Qt::Key_Return);
        QTest::keyClick(key, Qt::Key_Return);    // pressed again while it's asking
        QVERIFY(done.wait(5000));
        // Enter never reaches Quit (it once did: the Enter that started
        // activating went on to the next default button and quit quewi).
        QCOMPARE(quit.count(), 0);
        QVERIFY(!Store::token().isEmpty());
        QCOMPARE(Store::key(), QStringLiteral("QWABCDEFGHJKMNPQRS"));
        QVERIFY(evaluateAt(now(), m_public).ok());

        // A license for another computer from the server is refused, saying why.
        Store::clear();
        FakeServer srv2;
        srv2.body = QJsonDocument(QJsonObject{{"token", sign(claims(now() + 86400, QStringLiteral("ff")))}})
                        .toJson(QJsonDocument::Compact);
        ActivationDialog dlg2(st);
        dlg2.setPublicKey(m_public);
        dlg2.client()->setBaseUrl(srv2.url());
        dlg2.show();
        auto *key2 = dlg2.findChild<QLineEdit *>(QStringLiteral("licenseKey"));
        key2->setText(QStringLiteral("QW-ZZZZ-ZZZZ"));
        auto *err = dlg2.findChild<QLabel *>(QStringLiteral("licenseError"));
        QTest::keyClick(key2, Qt::Key_Return);
        QTRY_VERIFY_WITH_TIMEOUT(err->isVisible(), 5000);
        QVERIFY2(err->text().contains(QStringLiteral("different computer")), qPrintable(err->text()));
        QVERIFY(Store::token().isEmpty());
    }
};

QTEST_MAIN(LicenseTests)
#include "test_license.moc"
