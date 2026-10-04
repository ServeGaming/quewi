#include <QTest>
#include <QApplication>
#include <QLineEdit>
#include <QSettings>
#include <QSignalSpy>
#include <QWidget>

#include "ui/SafeKey.h"

using namespace quewi;

// The safe key: hold it to GO / to delete cues. QSettings here are the
// user's real quewi settings, so the safety keys are saved first and put
// back afterwards.
class SafeKeyTests : public QObject {
    Q_OBJECT

    QVariantMap m_saved;
    static QSettings settings() { return QSettings(QStringLiteral("ServeGaming"), QStringLiteral("quewi")); }

private slots:
    void initTestCase()
    {
        auto s = settings();
        for (const auto &k : {QStringLiteral("safety/safeKeyForGo"), QStringLiteral("safety/safeKeyForDelete"),
                              QStringLiteral("safety/safeKey")})
            if (s.contains(k)) m_saved.insert(k, s.value(k));
    }
    void cleanupTestCase()
    {
        auto s = settings();
        for (const auto &k : {QStringLiteral("safety/safeKeyForGo"), QStringLiteral("safety/safeKeyForDelete"),
                              QStringLiteral("safety/safeKey")}) {
            if (m_saved.contains(k)) s.setValue(k, m_saved.value(k));
            else s.remove(k);
        }
        ui::SafeKey::instance()->reload();
    }

    void offByDefaultAllowsEverything()
    {
        ui::SafeKey::save(false, false, QStringLiteral("Shift"));
        auto *k = ui::SafeKey::instance();
        QVERIFY(k->allows(ui::SafeKey::Action::Go));
        QVERIFY(k->allows(ui::SafeKey::Action::Delete));
    }

    void plainKeyMustBeHeld()
    {
        ui::SafeKey::save(true, true, QStringLiteral("F12"));
        auto *k = ui::SafeKey::instance();
        QCOMPARE(k->keyName(), QStringLiteral("F12"));
        QVERIFY(!k->allows(ui::SafeKey::Action::Go));
        QVERIFY(k->blockedMessage(ui::SafeKey::Action::Go).contains(QStringLiteral("F12")));

        QWidget w;
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        QTest::keyPress(&w, Qt::Key_F12);
        QVERIFY(k->isHeld());
        QVERIFY(k->allows(ui::SafeKey::Action::Go));
        QVERIFY(k->allows(ui::SafeKey::Action::Delete));
        QTest::keyRelease(&w, Qt::Key_F12);
        QVERIFY(!k->isHeld());
        QVERIFY(!k->allows(ui::SafeKey::Action::Delete));
    }

    void modifierChordReachesGoAndDelete()
    {
        ui::SafeKey::save(true, true, QStringLiteral("Shift"));
        auto *k = ui::SafeKey::instance();
        k->setGoKey(QKeySequence(Qt::Key_Space));
        k->setDeleteKey(QKeySequence(Qt::Key_Delete));
        QSignalSpy go(k, &ui::SafeKey::goChord), del(k, &ui::SafeKey::deleteChord);

        QWidget w;
        w.setFocusPolicy(Qt::StrongFocus);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        w.setFocus();
        QTest::keyClick(&w, Qt::Key_Space, Qt::ShiftModifier);
        QCOMPARE(go.count(), 1);
        QTest::keyClick(&w, Qt::Key_Space);                  // plain Space: not the chord
        QCOMPARE(go.count(), 1);
        QTest::keyClick(&w, Qt::Key_Delete, Qt::ShiftModifier);
        QCOMPARE(del.count(), 1);

        // Typing a capital-letter space in a text field isn't GO.
        QLineEdit edit;
        edit.show();
        QVERIFY(QTest::qWaitForWindowExposed(&edit));
        edit.setFocus();
        QTest::keyClick(&edit, Qt::Key_Space, Qt::ShiftModifier);
        QCOMPARE(go.count(), 1);
        QCOMPARE(edit.text(), QStringLiteral(" "));

        // Only the gated actions get the chord.
        ui::SafeKey::save(true, false, QStringLiteral("Shift"));
        QTest::keyClick(&w, Qt::Key_Delete, Qt::ShiftModifier);
        QCOMPARE(del.count(), 1);
    }
};

QTEST_MAIN(SafeKeyTests)
#include "test_safe_key.moc"
