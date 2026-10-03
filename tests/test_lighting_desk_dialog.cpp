#include <QTest>
#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QSpinBox>

#include "core/LightingDesk.h"
#include "ui/LightingDeskDialog.h"

using namespace quewi;

// The Lighting Desk setup dialog: nothing reaches QSettings until Save,
// Save writes what the fields say, and the MSC desk swaps the IP/port
// fields for MIDI ones.
class LightingDeskDialogTests : public QObject {
    Q_OBJECT

    // QSettings here is the user's real quewi settings: every test changes
    // lighting/desk/*, so it's saved first and put back afterwards.
    QVariantMap m_savedDesk;

    static void seed(core::LightingDesk::Type type, const QString &host, int port)
    {
        core::LightingDesk d;
        d.type = type;
        d.host = host;
        d.port = port;
        d.eosFaderBank = 7;      // non-default, so we can see it survive a Save
        d.save();
    }

    template <typename T>
    static T *child(QWidget &w, const char *name)
    {
        auto *c = w.findChild<T *>(QString::fromLatin1(name));
        if (!c) qWarning("no child %s", name);
        return c;
    }

    static QPushButton *saveButton(ui::LightingDeskDialog &dlg)
    {
        auto *box = dlg.findChild<QDialogButtonBox *>();
        return box ? box->button(QDialogButtonBox::Save) : nullptr;
    }

private slots:
    void initTestCase()
    {
        QSettings s(QStringLiteral("ServeGaming"), QStringLiteral("quewi"));
        s.beginGroup(QStringLiteral("lighting/desk"));
        for (const auto &k : s.childKeys()) m_savedDesk.insert(k, s.value(k));
        s.endGroup();
    }

    void cleanupTestCase()
    {
        QSettings s(QStringLiteral("ServeGaming"), QStringLiteral("quewi"));
        s.beginGroup(QStringLiteral("lighting/desk"));
        s.remove(QString());                      // everything in the group
        for (auto it = m_savedDesk.cbegin(); it != m_savedDesk.cend(); ++it)
            s.setValue(it.key(), it.value());
        s.endGroup();
        s.sync();
    }

    void loadsWhatWasSaved()
    {
        seed(core::LightingDesk::Type::Eos, QStringLiteral("10.0.0.5"), 8001);
        ui::LightingDeskDialog dlg;
        QCOMPARE(child<QLineEdit>(dlg, "deskHost")->text(), QStringLiteral("10.0.0.5"));
        QCOMPARE(child<QSpinBox>(dlg, "deskPort")->value(), 8001);
        QVERIFY(child<QRadioButton>(dlg, "deskType_eos")->isChecked());
        QCOMPARE(dlg.currentDesk().eosFaderBank, 7);
    }

    void cancelWritesNothing()
    {
        seed(core::LightingDesk::Type::Eos, QStringLiteral("10.0.0.5"), 8000);
        ui::LightingDeskDialog dlg;
        child<QLineEdit>(dlg, "deskHost")->setText(QStringLiteral("192.168.9.9"));
        child<QSpinBox>(dlg, "deskPort")->setValue(9000);
        child<QRadioButton>(dlg, "deskType_ma3")->setChecked(true);
        child<QLineEdit>(dlg, "deskPrefix")->setText(QStringLiteral("gma3"));

        // Editing alone must not touch settings…
        auto d = core::LightingDesk::load();
        QCOMPARE(d.host, QStringLiteral("10.0.0.5"));
        QCOMPARE(d.port, 8000);
        QCOMPARE(d.type, core::LightingDesk::Type::Eos);
        QVERIFY(d.ma3Prefix.isEmpty());

        // …and neither must Cancel.
        dlg.reject();
        QCOMPARE(dlg.result(), int(QDialog::Rejected));
        d = core::LightingDesk::load();
        QCOMPARE(d.host, QStringLiteral("10.0.0.5"));
        QCOMPARE(d.port, 8000);
        QCOMPARE(d.type, core::LightingDesk::Type::Eos);
        QVERIFY(d.ma3Prefix.isEmpty());
    }

    void saveWritesFields()
    {
        seed(core::LightingDesk::Type::Eos, QStringLiteral("10.0.0.5"), 8000);
        ui::LightingDeskDialog dlg;
        child<QRadioButton>(dlg, "deskType_ma3")->setChecked(true);
        child<QLineEdit>(dlg, "deskHost")->setText(QStringLiteral("192.168.9.9"));
        child<QSpinBox>(dlg, "deskPort")->setValue(9000);
        child<QLineEdit>(dlg, "deskPrefix")->setText(QStringLiteral("/gma3"));

        auto *save = saveButton(dlg);
        QVERIFY(save);
        QVERIFY(save->isEnabled());
        save->click();
        QCOMPARE(dlg.result(), int(QDialog::Accepted));

        const auto d = core::LightingDesk::load();
        QCOMPARE(d.type, core::LightingDesk::Type::Ma3);
        QCOMPARE(d.host, QStringLiteral("192.168.9.9"));
        QCOMPARE(d.port, 9000);
        QCOMPARE(d.ma3Prefix, QStringLiteral("gma3"));   // slashes stripped
        QCOMPARE(d.eosFaderBank, 7);                     // untouched by the dialog
    }

    void badHostBlocksSave()
    {
        seed(core::LightingDesk::Type::Eos, QStringLiteral("10.0.0.5"), 8000);
        ui::LightingDeskDialog dlg;
        auto *host  = child<QLineEdit>(dlg, "deskHost");
        auto *error = child<QLabel>(dlg, "deskHostError");
        auto *save  = saveButton(dlg);
        QVERIFY(host && error && save);

        host->setText(QStringLiteral("192.168.1.x y"));
        QVERIFY(!error->isHidden());
        QVERIFY(!save->isEnabled());
        host->setText(QString());
        QVERIFY(!error->isHidden());
        QVERIFY(!save->isEnabled());

        host->setText(QStringLiteral("eos.local"));
        QVERIFY(error->isHidden());
        QVERIFY(save->isEnabled());

        // Save with a bad host must not write.
        host->setText(QStringLiteral("not valid!"));
        save->click();
        QCOMPARE(dlg.result(), int(QDialog::Rejected));   // still open / unaccepted
        QCOMPARE(core::LightingDesk::load().host, QStringLiteral("10.0.0.5"));
    }

    void localhostNote()
    {
        seed(core::LightingDesk::Type::Eos, QStringLiteral("10.0.0.5"), 8000);
        ui::LightingDeskDialog dlg;
        dlg.show();
        auto *note = child<QLabel>(dlg, "deskHostNote");
        QVERIFY(note);
        QVERIFY(!note->isVisible());
        child<QLineEdit>(dlg, "deskHost")->setText(QStringLiteral("127.0.0.1"));
        QVERIFY(note->isVisible());
        child<QLineEdit>(dlg, "deskHost")->setText(QStringLiteral("localhost"));
        QVERIFY(note->isVisible());
    }

    void mscDeskSwapsFields()
    {
        seed(core::LightingDesk::Type::Eos, QStringLiteral("10.0.0.5"), 8000);
        ui::LightingDeskDialog dlg;
        dlg.show();
        auto *host   = child<QLineEdit>(dlg, "deskHost");
        auto *port   = child<QSpinBox>(dlg, "deskPort");
        auto *prefix = child<QLineEdit>(dlg, "deskPrefix");
        auto *midi   = child<QComboBox>(dlg, "deskMidiPort");
        auto *device = child<QSpinBox>(dlg, "deskDeviceId");
        QVERIFY(host && port && prefix && midi && device);

        QVERIFY(host->isVisible());
        QVERIFY(port->isVisible());
        QVERIFY(!prefix->isVisible());     // Eos has no prefix
        QVERIFY(!midi->isVisible());
        QVERIFY(!device->isVisible());

        child<QRadioButton>(dlg, "deskType_ma3")->setChecked(true);
        QVERIFY(prefix->isVisible());

        child<QRadioButton>(dlg, "deskType_ma2msc")->setChecked(true);
        QVERIFY(!host->isVisible());
        QVERIFY(!port->isVisible());
        QVERIFY(!prefix->isVisible());
        QVERIFY(midi->isVisible());
        QVERIFY(device->isVisible());

        // A bad IP doesn't matter for an MSC desk.
        host->setText(QString());
        QVERIFY(saveButton(dlg)->isEnabled());
        device->setValue(12);
        saveButton(dlg)->click();
        const auto d = core::LightingDesk::load();
        QCOMPARE(d.type, core::LightingDesk::Type::Ma2Msc);
        QCOMPARE(d.mscDeviceId, 12);
        QVERIFY(d.midiPort.isEmpty());      // "(first available)"
    }

    void ownOscPortIsQuoted()
    {
        QSettings s(QStringLiteral("ServeGaming"), QStringLiteral("quewi"));
        const QVariant saved = s.value(QStringLiteral("osc/udpPort"));
        s.setValue(QStringLiteral("osc/udpPort"), 8500);
        {
            seed(core::LightingDesk::Type::Eos, QStringLiteral("10.0.0.5"), 8000);
            ui::LightingDeskDialog dlg;
            auto *note = child<QLabel>(dlg, "deskOwnPortNote");
            QVERIFY(note);
            QVERIFY(note->text().contains(QStringLiteral("8500")));
            QVERIFY(note->text().contains(QStringLiteral("HeliOSC")));
        }
        if (saved.isValid()) s.setValue(QStringLiteral("osc/udpPort"), saved);
        else s.remove(QStringLiteral("osc/udpPort"));
    }
};

QTEST_MAIN(LightingDeskDialogTests)
#include "test_lighting_desk_dialog.moc"
