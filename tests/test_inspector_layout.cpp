#include <QTest>
#include <QApplication>
#include <QComboBox>
#include <QScrollArea>

#include "audio/AudioCue.h"
#include "core/CueList.h"
#include "core/Workspace.h"
#include "cues/FadeCue.h"
#include "ui/Inspector.h"

using namespace quewi;

// A long file name (and so a long cue name) mustn't make the Inspector wider:
// the cue pickers list every cue by name, and a combo box's natural width is
// its longest entry. On a Mac that pinned the panel open and squashed it.
class InspectorLayoutTests : public QObject {
    Q_OBJECT

    static int widthWith(const QString &cueName, bool fade)
    {
        core::Workspace ws;
        auto *list = ws.cueLists().empty()
                   ? ws.addCueList(std::make_unique<core::CueList>(QStringLiteral("Main")))
                   : ws.cueLists().front().get();
        auto audio = std::make_unique<audio::AudioCue>();
        audio->setField(QStringLiteral("name"), cueName);
        auto *audioPtr = audio.get();
        list->insertCue(list->cueCount(), std::move(audio));
        auto fadeCue = std::make_unique<cues::FadeCue>();
        auto *fadePtr = fadeCue.get();
        list->insertCue(list->cueCount(), std::move(fadeCue));

        ui::Inspector inspector;
        inspector.setWorkspace(&ws);
        inspector.setCue(fade ? static_cast<cues::Cue *>(fadePtr) : audioPtr);
        inspector.show();
        QCoreApplication::processEvents();
        // The panel is a scroll area with no sideways scrolling: what counts is
        // how narrow its contents can go, or the right-hand side is cut off.
        auto *scroll = inspector.findChild<QScrollArea *>(QStringLiteral("inspectorScroll"));
        // Both how narrow it can go and how wide it asks to be (the dock
        // follows the second).
        QWidget *body = scroll && scroll->widget() ? scroll->widget() : static_cast<QWidget *>(&inspector);
        const int w = std::max(body->minimumSizeHint().width(), body->sizeHint().width());
        if (qEnvironmentVariableIsSet("QUEWI_DEBUG_WIDTH"))
            for (auto *c : inspector.findChildren<QComboBox *>())
                if (c->isVisibleTo(&inspector) && c->count() > 1)
                    qInfo("combo %s items=%d min=%d hint=%d", qPrintable(c->objectName()), c->count(),
                          c->minimumSizeHint().width(), c->sizeHint().width());
        inspector.setCue(nullptr);
        return w;
    }

private slots:
    void aLongCueNameDoesntWidenTheInspector_data()
    {
        QTest::addColumn<bool>("fade");
        QTest::newRow("fade cue (target picker)") << true;
        QTest::newRow("audio cue (link picker)") << false;
    }
    void aLongCueNameDoesntWidenTheInspector()
    {
        QFETCH(bool, fade);
        const int shortW = widthWith(QStringLiteral("Song"), fade);
        const QString longName = QStringLiteral("01 - Overture (Full Orchestra, Final Mix, Revised for the "
                                                "Tuesday Matinee, Do Not Use the Old One) v12 FINAL FINAL.wav");
        const int longW = widthWith(longName, fade);
        QVERIFY2(longW <= shortW + 8, qPrintable(QStringLiteral("short %1 px, long %2 px").arg(shortW).arg(longW)));
    }
};

QTEST_MAIN(InspectorLayoutTests)
#include "test_inspector_layout.moc"
