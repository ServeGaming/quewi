#include <QTest>

#include "core/Workspace.h"

class SmokeTests : public QObject {
    Q_OBJECT
private slots:
    void constructsWorkspace() {
        quewi::core::Workspace w;
        Q_UNUSED(w);
        QVERIFY(true);
    }
};

QTEST_MAIN(SmokeTests)
#include "test_smoke.moc"
