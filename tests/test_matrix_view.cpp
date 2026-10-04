#include <QTest>
#include <QApplication>
#include <QJsonArray>
#include <QLabel>
#include <QMimeData>
#include <QSignalSpy>
#include <QTableView>

#include "audio/AudioCue.h"
#include "audio/LightTrigger.h"
#include "core/CueList.h"
#include "core/Workspace.h"
#include "cues/MemoCue.h"
#include "midi/MidiCue.h"
#include "osc/EosCueLists.h"
#include "osc/OscCue.h"
#include "ui/MatrixSource.h"
#include "ui/MatrixView.h"
#include "ui/ShowModeView.h"

using namespace quewi;
namespace m = core::matrix;

// The Matrix List on screen (ui/MatrixView) and what feeds it
// (ui/MatrixSource): which desk cues a real show's cues fire, the rows a
// desk list read over EosCueLists makes, drag-placing, the live highlights,
// the remote JSON, and Show Mode's merged COMING UP.
class MatrixViewTests : public QObject {
    Q_OBJECT

    struct Show {
        core::Workspace ws;
        core::CueList *main = nullptr;
        core::CueList *matrix = nullptr;
        QUuid song, sfx, osc, msc, memo;
    };

    static audio::LightTrigger deskCueTrigger(double at, const char *number, const char *name)
    {
        audio::LightTrigger t;
        t.name = QString::fromLatin1(name);
        t.start = at;
        t.enter.kind = audio::TriggerAction::Kind::Desk;
        t.enter.deskDo = audio::TriggerAction::DeskDo::GoToCue;
        t.enter.number = QString::fromLatin1(number);
        t.enter.list = 1;
        return t;
    }

    // Main: 1 song (fires LX 5 at GO, LX 6 at 0:30), 2 memo, 3 OSC → LX 7,
    // 4 MSC GO Q 9. Matrix interleaves Main with desk list 1.
    static void build(Show &s)
    {
        auto main = std::make_unique<core::CueList>(QStringLiteral("Main"));
        auto song = std::make_unique<audio::AudioCue>();
        song->setField(QStringLiteral("number"), 1.0);
        song->setField(QStringLiteral("name"), QStringLiteral("Overture"));
        song->setField(QStringLiteral("lightTriggers"),
                       audio::triggersToJson({deskCueTrigger(0.0, "5", "Top"),
                                              deskCueTrigger(30.0, "6", "Chorus")}));
        s.song = song->id();
        auto memo = std::make_unique<cues::MemoCue>();
        memo->setField(QStringLiteral("number"), 2.0);
        memo->setField(QStringLiteral("name"), QStringLiteral("Scene change"));
        memo->setField(QStringLiteral("notes"), QStringLiteral("Wait for the flats"));
        s.memo = memo->id();
        auto osc = std::make_unique<osc::OscCue>();
        osc->setField(QStringLiteral("number"), 3.0);
        osc->setField(QStringLiteral("address"), QStringLiteral("/eos/cue/1/7/fire"));
        s.osc = osc->id();
        auto msc = std::make_unique<midi::MscCue>();
        msc->setField(QStringLiteral("number"), 4.0);
        msc->setField(QStringLiteral("qNumber"), QStringLiteral("9"));
        s.msc = msc->id();
        main->insertCue(0, std::move(song));
        main->insertCue(1, std::move(memo));
        main->insertCue(2, std::move(osc));
        main->insertCue(3, std::move(msc));
        s.main = s.ws.addCueList(std::move(main));

        auto matrix = std::make_unique<core::CueList>(QStringLiteral("Matrix"));
        matrix->setKind(core::CueList::Kind::Matrix);
        s.matrix = s.ws.addCueList(std::move(matrix));
    }

    // Answer a reader's fetch of list 1 as a desk with these cues would.
    static void feedDesk(osc::EosCueLists &r, const QStringList &numbers)
    {
        r.handle({QStringLiteral("/eos/out/get/cue/1/count"), {osc::Argument::i(int(numbers.size()))}});
        for (int i = 0; i < numbers.size(); ++i) {
            std::vector<osc::Argument> a(31, osc::Argument::i(-1));
            a[0] = osc::Argument::i(i);
            a[1] = osc::Argument::s(QStringLiteral("uid-") + numbers[i]);
            a[2] = osc::Argument::s(QStringLiteral("Look ") + numbers[i]);
            a[3] = osc::Argument::i(4000);
            a[27] = osc::Argument::s(QString());
            a[28] = osc::Argument::s(numbers[i] == QLatin1String("1") ? QStringLiteral("Prologue") : QString());
            r.handle({QStringLiteral("/eos/out/get/cue/1/%1/0/list/%2/%3").arg(numbers[i]).arg(i).arg(numbers.size()), a});
        }
    }

    static QStringList column(const ui::MatrixTableModel *model, int col)
    {
        QStringList out;
        for (int r = 0; r < model->rowCount(); ++r)
            out << model->data(model->index(r, col)).toString();
        return out;
    }

private slots:
    void initTestCase() { QApplication::setStyle(QStringLiteral("Fusion")); }

    void readsWhatCuesFire()
    {
        Show s;
        build(s);
        auto fires = ui::deskFiresOf(s.main->cueAt(0));
        QCOMPARE(fires.size(), size_t(2));
        QCOMPARE(fires[0].number, QStringLiteral("5"));
        QCOMPARE(fires[0].atSeconds, -1.0);                  // top of the song = at GO
        QCOMPARE(fires[1].atSeconds, 30.0);
        QCOMPARE(fires[1].triggerName, QStringLiteral("Chorus"));
        fires = ui::deskFiresOf(s.main->cueAt(2));
        QCOMPARE(fires.size(), size_t(1));
        QCOMPARE(fires[0].list, QStringLiteral("1"));
        QCOMPARE(fires[0].number, QStringLiteral("7"));
        QCOMPARE(ui::deskFiresOf(s.main->cueAt(3)).front().number, QStringLiteral("9"));
        QVERIFY(ui::deskFiresOf(s.main->cueAt(1)).empty());

        // Other ways of writing it: the default list, and a typed command.
        osc::OscCue o;
        o.setField(QStringLiteral("address"), QStringLiteral("/eos/cue/12.5/fire"));
        fires = ui::deskFiresOf(&o);
        QCOMPARE(fires.front().list, QString());
        QCOMPARE(fires.front().number, QStringLiteral("12.5"));
        o.setField(QStringLiteral("address"), QStringLiteral("/eos/newcmd"));
        o.setField(QStringLiteral("rawArgs"), QStringLiteral("Go_To_Cue 2/14 Enter"));
        fires = ui::deskFiresOf(&o);
        QCOMPARE(fires.front().list, QStringLiteral("2"));
        QCOMPARE(fires.front().number, QStringLiteral("14"));
        o.setField(QStringLiteral("address"), QStringLiteral("/eos/key/go_0"));
        QVERIFY(ui::deskFiresOf(&o).empty());
    }

    void showsTheMergedList()
    {
        Show s;
        build(s);
        osc::EosCueLists reader(nullptr);
        reader.setSender([](const osc::Message &) {});

        ui::MatrixView view;
        view.resize(1100, 600);
        view.setWorkspace(&s.ws);
        view.setDesk(&reader, nullptr);
        view.setCueList(s.matrix);
        QVERIFY(reader.watched().contains(QStringLiteral("1")));   // the view asked for its list

        // Before the desk is read: quewi's cues only, and it says why.
        QCOMPARE(column(view.model(), ui::MatrixTableModel::ColNumber),
                 (QStringList{QStringLiteral("1"), QStringLiteral("2"), QStringLiteral("3"),
                              QStringLiteral("4")}));
        QVERIFY(view.statusText().contains(QStringLiteral("Lighting Desk")));

        s.ws.markClean();                    // as if just opened
        feedDesk(reader, {QStringLiteral("1"), QStringLiteral("5"), QStringLiteral("6"),
                          QStringLiteral("6.5"), QStringLiteral("7"), QStringLiteral("8"), QStringLiteral("9")});
        QTRY_VERIFY(view.statusText().startsWith(QStringLiteral("Live from the desk")));
        const QStringList lights = column(view.model(), ui::MatrixTableModel::ColLights);
        QCOMPARE(lights, (QStringList{QStringLiteral("LX 1  Look 1"),       // before anything: top
                                      QStringLiteral("LX 5  Look 5"),       // with the song's GO
                                      QStringLiteral("LX 6  Look 6"),       // hit at 0:30
                                      QStringLiteral("LX 6.5  Look 6.5"),   // follows LX 6
                                      QString(),                            // 2 memo
                                      QStringLiteral("LX 7  Look 7"),       // OSC cue
                                      QStringLiteral("LX 8  Look 8"),       // follows 7
                                      QStringLiteral("LX 9  Look 9")}));    // MSC
        const QStringList cue = column(view.model(), ui::MatrixTableModel::ColCue);
        QVERIFY(cue[2].contains(QStringLiteral("0:30")));
        QVERIFY(cue[2].contains(QStringLiteral("Chorus")));
        QVERIFY(column(view.model(), ui::MatrixTableModel::ColNotes)[0].contains(QStringLiteral("Prologue")));
        QCOMPARE(column(view.model(), ui::MatrixTableModel::ColTime)[1], QStringLiteral("4"));

        // The desk's cues are kept with the show for next time (not an edit).
        QCOMPARE(s.matrix->matrixConfig().deskCache.size(), size_t(7));
        QVERIFY(!s.ws.isDirty());

        // Edits to the source list flow in.
        auto memo2 = std::make_unique<cues::MemoCue>();
        memo2->setField(QStringLiteral("number"), 5.0);
        s.main->insertCue(4, std::move(memo2));
        QTRY_COMPARE(view.model()->rowCount(), 9);
    }

    void dragPlacesADeskCue()
    {
        Show s;
        build(s);
        osc::EosCueLists reader(nullptr);
        reader.setSender([](const osc::Message &) {});
        ui::MatrixView view;
        view.setWorkspace(&s.ws);
        view.setDesk(&reader, nullptr);
        view.setCueList(s.matrix);
        feedDesk(reader, {QStringLiteral("5"), QStringLiteral("8")});
        view.rebuildNow();
        auto *model = view.model();
        // Rows: Q1[LX5]  ~LX6 (missing)  LX8  Q2  Q3[LX7 missing]  Q4[LX9 missing]
        int lx8 = -1, memoRow = -1;
        for (int r = 0; r < model->rowCount(); ++r) {
            if (model->data(model->index(r, ui::MatrixTableModel::ColLights)).toString().startsWith(QStringLiteral("LX 8")))
                lx8 = r;
            if (model->data(model->index(r, ui::MatrixTableModel::ColNumber)).toString() == QLatin1String("2"))
                memoRow = r;
        }
        QVERIFY(lx8 >= 0 && memoRow >= 0);
        QVERIFY(model->flags(model->index(lx8, 0)) & Qt::ItemIsDragEnabled);

        QSignalSpy modified(&view, &ui::MatrixView::modified);
        std::unique_ptr<QMimeData> md(model->mimeData({model->index(lx8, 0)}));
        QVERIFY(md);
        // Dropped onto the memo's row: LX 8 goes with cue 2.
        model->dropMimeData(md.get(), Qt::MoveAction, -1, -1, model->index(memoRow, 1));
        QCOMPARE(modified.count(), 1);
        QCOMPARE(s.matrix->matrixConfig().placements.size(), size_t(1));
        QCOMPARE(s.matrix->matrixConfig().placements[0].anchor, s.memo);
        QCOMPARE(s.matrix->matrixConfig().placements[0].uid, QStringLiteral("uid-8"));
        view.rebuildNow();
        for (int r = 0; r < model->rowCount(); ++r)
            if (model->data(model->index(r, ui::MatrixTableModel::ColNumber)).toString() == QLatin1String("2"))
                QVERIFY(model->data(model->index(r, ui::MatrixTableModel::ColLights)).toString().startsWith(QStringLiteral("LX 8")));

        // Dropped in the gap at the very top: Start.
        m::ManualPlacement::Mode mode;
        QUuid anchor;
        QVERIFY(model->dropTarget(0, false, &mode, &anchor));
        QCOMPARE(mode, m::ManualPlacement::Mode::Start);
        // In the gap below the song's row: After the song.
        QVERIFY(model->dropTarget(1, false, &mode, &anchor));
        QCOMPARE(mode, m::ManualPlacement::Mode::After);
        QCOMPARE(anchor, s.song);

        // Show Mode: nothing can be moved.
        view.setLocked(true);
        QVERIFY(!(model->flags(model->index(lx8, 0)) & Qt::ItemIsDragEnabled));
        QVERIFY(!model->canDropMimeData(md.get(), Qt::MoveAction, -1, -1, model->index(0, 0)));
    }

    void highlightsWhatsLiveAndAnswersRemotes()
    {
        Show s;
        build(s);
        osc::EosCueLists reader(nullptr);
        reader.setSender([](const osc::Message &) {});
        ui::MatrixView view;
        view.setWorkspace(&s.ws);
        view.setDesk(&reader, nullptr);
        view.setCueList(s.matrix);
        feedDesk(reader, {QStringLiteral("5"), QStringLiteral("6"), QStringLiteral("7")});
        view.rebuildNow();

        ui::MatrixLive live;
        live.standby = s.osc;
        live.running = {s.song};
        view.setLiveProvider([&live] { return live; });
        view.pollLive();
        auto *model = view.model();
        const auto b = view.build();
        const int sb = b.rowOfQuewi(s.osc);
        QVERIFY(sb >= 0);
        QVERIFY(model->data(model->index(sb, 0)).toString().contains(QStringLiteral("STANDBY")));
        QVERIFY(model->data(model->index(sb, 0), Qt::BackgroundRole).value<QColor>().isValid());
        QVERIFY(model->data(model->index(b.rowOfQuewi(s.song), 0)).toString().contains(QStringLiteral("PLAYING")));

        // Remotes: the whole list, and a page.
        live.deskActiveList = QStringLiteral("1");
        live.deskActiveCue = QStringLiteral("6");
        auto j = ui::matrixJson(s.matrix, b, live);
        QCOMPARE(j.value(QStringLiteral("total")).toInt(), model->rowCount());
        QCOMPARE(j.value(QStringLiteral("standbyRow")).toInt(), sb);
        QCOMPARE(j.value(QStringLiteral("desk")).toString(), QStringLiteral("live"));
        QCOMPARE(j.value(QStringLiteral("deskActiveRow")).toInt(), b.rowOfDesk(QStringLiteral("1"), QStringLiteral("6")));
        const auto rows = j.value(QStringLiteral("rows")).toArray();
        QCOMPARE(rows[0].toObject().value(QStringLiteral("kind")).toString(), QStringLiteral("quewi"));
        QCOMPARE(rows[0].toObject().value(QStringLiteral("lights")).toArray()[0].toObject()
                     .value(QStringLiteral("how")).toString(), QStringLiteral("fired"));
        QCOMPARE(rows[1].toObject().value(QStringLiteral("kind")).toString(), QStringLiteral("hit"));
        QVERIFY(rows[1].toObject().value(QStringLiteral("lights")).toArray()[0].toObject()
                    .value(QStringLiteral("active")).toBool());
        j = ui::matrixJson(s.matrix, b, live, 2, 2);
        QCOMPARE(j.value(QStringLiteral("rows")).toArray().size(), 2);
        QCOMPARE(j.value(QStringLiteral("from")).toInt(), 2);
        QCOMPARE(j.value(QStringLiteral("rows")).toArray()[0].toObject().value(QStringLiteral("row")).toInt(), 2);
    }

    // With no desk connected the cached cues stand in — and say so.
    void cachedDeskCuesStandIn()
    {
        Show s;
        build(s);
        auto cfg = s.matrix->matrixConfig();
        m::DeskCue c;
        c.list = QStringLiteral("1");
        c.number = QStringLiteral("5");
        c.label = QStringLiteral("From last night");
        cfg.deskCache = {c};
        cfg.deskCachedAt = QDateTime::currentDateTime();
        s.matrix->setMatrixConfig(cfg);
        ui::MatrixView view;
        view.setWorkspace(&s.ws);
        view.setCueList(s.matrix);
        QVERIFY(view.statusText().contains(QStringLiteral("last read")));
        QCOMPARE(view.build().desk, ui::MatrixBuild::Desk::Cached);
        QVERIFY(column(view.model(), ui::MatrixTableModel::ColLights)[0].contains(QStringLiteral("From last night")));
        // LX 6/7/9 are fired by quewi but not in the (cached) desk list: marked.
        QVERIFY(view.statusText().contains(QStringLiteral("⚠")));
    }

    // Show Mode's COMING UP shows desk cues in the merged order.
    void showModeRendersMatrixLines()
    {
        ui::ShowSnapshot snap;
        ui::ShowCueLine sb;
        sb.number = QStringLiteral("3");
        sb.name = QStringLiteral("Door slam");
        sb.deskCues = QStringLiteral("LX 7 Blackout");
        snap.standby = sb;
        ui::ShowCueLine desk;
        desk.deskOnly = true;
        desk.number = QStringLiteral("LX 8");
        desk.name = QStringLiteral("Restore");
        desk.notes = QStringLiteral("Scene: Act 2");
        ui::ShowCueLine next;
        next.number = QStringLiteral("4");
        next.name = QStringLiteral("Phone");
        next.deskCues = QStringLiteral("LX 9 Phone special");
        snap.comingUp = {desk, next};
        ui::ShowModeView v;
        v.resize(1280, 800);
        v.setSnapshot(snap);
        QStringList texts;
        for (auto *l : v.findChildren<QLabel *>()) texts << l->text();
        const QString all = texts.join(QLatin1Char('\n'));
        QVERIFY(all.contains(QStringLiteral("LX 8")));
        QVERIFY(all.contains(QStringLiteral("Restore")));
        QVERIFY(all.contains(QStringLiteral("LX 9 Phone special")));
        QVERIFY(all.contains(QStringLiteral("LX 7 Blackout")));
    }
};

QTEST_MAIN(MatrixViewTests)
#include "test_matrix_view.moc"
