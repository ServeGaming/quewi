#include <QTest>
#include <QApplication>
#include <QJsonArray>
#include <QLabel>
#include <QMimeData>
#include <QSignalSpy>
#include <QPushButton>
#include <QTableView>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUdpSocket>
#include <QUndoStack>
#include <QtEndian>

#include "audio/AudioCue.h"
#include "audio/LightTrigger.h"
#include "core/CueList.h"
#include "core/Workspace.h"
#include "cues/MemoCue.h"
#include "midi/MidiCue.h"
#include "app/GoEngine.h"
#include "core/LightingDesk.h"
#include "osc/EosCueLists.h"
#include "osc/EosFeedback.h"
#include "osc/OscCodec.h"
#include "osc/OscCue.h"
#include "osc/OscEngine.h"
#include "ui/MatrixSource.h"
#include "ui/MatrixView.h"
#include "ui/ShowModeView.h"
#include "ui/Theme.h"

#include <QDir>

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
            a[29] = osc::Argument::F();          // scene end, as the desk sends it
            a[28] = osc::Argument::s(numbers[i] == QLatin1String("1") ? QStringLiteral("Prologue") : QString());
            r.handle({QStringLiteral("/eos/out/get/cue/1/%1/0/list/0/31").arg(numbers[i]), a});
        }
    }

    // Same, with labels / scenes / notes given: {number, label, scene, notes}.
    static void feedDeskLabelled(osc::EosCueLists &r, const QList<QStringList> &cues)
    {
        r.handle({QStringLiteral("/eos/out/get/cue/1/count"), {osc::Argument::i(int(cues.size()))}});
        for (int i = 0; i < cues.size(); ++i) {
            std::vector<osc::Argument> a(31, osc::Argument::i(-1));
            a[0] = osc::Argument::i(i);
            a[1] = osc::Argument::s(QStringLiteral("uid-") + cues[i].value(0));
            a[2] = osc::Argument::s(cues[i].value(1));
            a[3] = osc::Argument::i(cues[i].value(4).isEmpty() ? 3000 : cues[i].value(4).toInt());
            a[5] = osc::Argument::i(cues[i].value(5).isEmpty() ? -1 : cues[i].value(5).toInt());
            a[28] = osc::Argument::s(cues[i].value(2));
            a[27] = osc::Argument::s(cues[i].value(3));
            a[29] = osc::Argument::F();
            r.handle({QStringLiteral("/eos/out/get/cue/1/%1/0/list/0/31").arg(cues[i].value(0)), a});
        }
    }

    static QByteArray framed(const osc::Message &m)
    {
        const QByteArray pkt = osc::Codec::encode(m);
        QByteArray out(4, Qt::Uninitialized);
        qToBigEndian<quint32>(quint32(pkt.size()), out.data());
        return out + pkt;
    }

    // A pretend Eos on localhost that only says what's active and pending
    // (enough for the link to be Live and for GO Lights to know the next cue).
    struct FakeDeskLink {
        QTcpServer server;
        QTcpSocket *peer = nullptr;
        osc::EosFeedback fb;
        bool start()
        {
            if (!server.listen(QHostAddress::LocalHost, 0)) return false;
            fb.setTimings(200, 1000, 60000);
            fb.start(QStringLiteral("127.0.0.1"), server.serverPort());
            return QTest::qWaitFor([this] { return server.hasPendingConnections(); }, 3000)
                && (peer = server.nextPendingConnection()) != nullptr;
        }
        void say(const char *address, const QString &text)
        {
            peer->write(framed({QString::fromLatin1(address), {osc::Argument::s(text)}}));
            peer->flush();
        }
    };

    static QStringList column(const ui::MatrixTableModel *model, int col)
    {
        QStringList out;
        for (int r = 0; r < model->rowCount(); ++r)
            out << model->data(model->index(r, col)).toString();
        return out;
    }

private slots:
    void initTestCase()
    {
        QApplication::setStyle(QStringLiteral("Fusion"));
        ui::Theme::load(QStringLiteral("quewi-dark"));
        qApp->setPalette(ui::Theme::palette());
    }

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

        std::unique_ptr<QMimeData> md(model->mimeData({model->index(lx8, 0)}));
        QVERIFY(md);
        // Dropped onto the memo's row: LX 8 goes with cue 2 — one undo step.
        s.ws.undoStack()->clear();
        model->dropMimeData(md.get(), Qt::MoveAction, -1, -1, model->index(memoRow, 1));
        QCOMPARE(s.ws.undoStack()->count(), 1);
        QCOMPARE(s.ws.undoStack()->undoText(), QStringLiteral("Line up LX 8 with Q2"));
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

    // Lining up from the quewi side: drag a sound cue onto a lighting row and
    // the LIGHTING cue moves to the sound cue's row; quewi's order never
    // changes; Ctrl+Z puts it back; a quewi cue dropped on a quewi cue is
    // refused (that would be reordering GO order).
    void soundCueLinesUpWithALightingCue()
    {
        Show s;
        build(s);
        osc::EosCueLists reader(nullptr);
        reader.setSender([](const osc::Message &) {});
        ui::MatrixView view;
        view.setWorkspace(&s.ws);
        view.setDesk(&reader, nullptr);
        view.setCueList(s.matrix);
        feedDesk(reader, {QStringLiteral("5"), QStringLiteral("6"), QStringLiteral("8"), QStringLiteral("9")});
        view.rebuildNow();
        auto *model = view.model();
        const auto rowOf = [&](const char *lx) {
            for (int r = 0; r < model->rowCount(); ++r)
                if (model->data(model->index(r, ui::MatrixTableModel::ColLights)).toString()
                        .startsWith(QString::fromLatin1(lx)))
                    return r;
            return -1;
        };
        const QStringList orderBefore = column(model, ui::MatrixTableModel::ColNumber);
        const int memoRow = view.build().rowOfQuewi(s.memo);
        const int lx8 = rowOf("LX 8");
        QVERIFY(memoRow >= 0 && lx8 >= 0);
        QVERIFY(model->flags(model->index(memoRow, 0)) & Qt::ItemIsDragEnabled);   // quewi rows drag too

        // Drag Q2 (the memo) onto LX 8's row.
        std::unique_ptr<QMimeData> md(model->mimeData({model->index(memoRow, 0)}));
        QVERIFY(md && md->hasFormat(QString::fromLatin1(ui::MatrixTableModel::kQuewiMime)));
        QVERIFY(model->canDropMimeData(md.get(), Qt::MoveAction, -1, -1, model->index(lx8, 2)));
        s.ws.undoStack()->clear();
        model->dropMimeData(md.get(), Qt::MoveAction, -1, -1, model->index(lx8, 2));
        QCOMPARE(s.matrix->matrixConfig().placements.size(), size_t(1));
        QCOMPARE(s.matrix->matrixConfig().placements[0].anchor, s.memo);
        QCOMPARE(s.matrix->matrixConfig().placements[0].mode, m::ManualPlacement::Mode::With);
        view.rebuildNow();
        const int memoNow = view.build().rowOfQuewi(s.memo);
        QVERIFY(model->data(model->index(memoNow, ui::MatrixTableModel::ColLights)).toString().startsWith(QStringLiteral("LX 8")));
        QVERIFY(model->data(model->index(memoNow, ui::MatrixTableModel::ColPlaced)).toString().contains(QStringLiteral("by hand")));
        QVERIFY(model->data(model->index(memoNow, 0), ui::MatrixTableModel::LinkedRole).toBool());
        // quewi's own order is untouched.
        QStringList orderNow = column(model, ui::MatrixTableModel::ColNumber);
        orderNow.removeAll(QString());
        QStringList orderWas = orderBefore;
        orderWas.removeAll(QString());
        QCOMPARE(orderNow, orderWas);
        QCOMPARE(s.main->cueAt(1)->id(), s.memo);

        // Ctrl+Z: back where quewi put it. Ctrl+Y: lined up again.
        QCOMPARE(s.ws.undoStack()->count(), 1);
        s.ws.undoStack()->undo();
        QVERIFY(s.matrix->matrixConfig().placements.empty());
        s.ws.undoStack()->redo();
        QCOMPARE(s.matrix->matrixConfig().placements.size(), size_t(1));

        // Onto another quewi cue: refused, with the reason.
        view.rebuildNow();
        QSignalSpy refused(model, &ui::MatrixTableModel::refused);
        const int songRow = view.build().rowOfQuewi(s.song);
        std::unique_ptr<QMimeData> md2(model->mimeData({model->index(view.build().rowOfQuewi(s.msc), 0)}));
        // (MSC's row has LX 9 on it, so the drag carries both kinds; dropped on a
        // quewi row the lighting moves — the quewi cue never does.)
        QVERIFY(md2 && md2->hasFormat(QString::fromLatin1(ui::MatrixTableModel::kQuewiMime)));
        auto quewiOnly = std::make_unique<QMimeData>();
        quewiOnly->setData(QString::fromLatin1(ui::MatrixTableModel::kQuewiMime), s.msc.toString().toUtf8());
        QVERIFY(!model->canDropMimeData(quewiOnly.get(), Qt::MoveAction, -1, -1, model->index(songRow, 2)));
        model->dropMimeData(quewiOnly.get(), Qt::MoveAction, -1, -1, model->index(songRow, 2));
        QCOMPARE(refused.count(), 1);
        QVERIFY(refused.first().first().toString().contains(QStringLiteral("GO order")));

        // Line up / Unlink from code (what the context menu does), each undoable.
        const auto before = s.ws.undoStack()->count();
        m::DeskCue lx9;
        lx9.list = QStringLiteral("1");
        lx9.number = QStringLiteral("9");
        lx9.uid = QStringLiteral("uid-9");
        model->lineUp(lx9, s.song);
        QCOMPARE(s.ws.undoStack()->count(), before + 1);
        model->unlink({lx9});
        QCOMPARE(s.ws.undoStack()->count(), before + 2);
        QCOMPARE(s.ws.undoStack()->undoText(), QStringLiteral("Unlink LX 9"));
    }

    // GO Lights' target: the desk's pending cue in this list, the next one
    // after its active cue, this list's first when the desk is elsewhere —
    // and disabled, with a reason, when it can't know.
    void goLightsPicksTheNextDeskCue()
    {
        ui::MatrixBuild b;
        b.deskList = QStringLiteral("2");
        b.desk = ui::MatrixBuild::Desk::Live;
        for (const char *n : {"1", "8.3", "8.4", "10"}) {
            m::DeskCue c;
            c.list = QStringLiteral("2");
            c.number = QString::fromLatin1(n);
            c.label = n == QLatin1String("8.4") ? QStringLiteral("Back") : QString();
            b.deskCues.push_back(c);
        }
        ui::MatrixLive live;
        live.deskActiveList = QStringLiteral("2");
        live.deskActiveCue = QStringLiteral("8.3");
        live.deskPendingList = QStringLiteral("2");
        live.deskPendingCue = QStringLiteral("8.4");
        auto t = ui::goLightsTarget(b, live, true, true);
        QVERIFY(t.ok);
        QCOMPARE(t.number, QStringLiteral("8.4"));
        QCOMPARE(t.buttonText(), QStringLiteral("GO Lights  8.4  Back"));
        live.deskPendingCue.clear();                         // no pending: after the active one
        QCOMPARE(ui::goLightsTarget(b, live, true, true).number, QStringLiteral("8.4"));
        live.deskActiveCue = QStringLiteral("10");           // the last one
        t = ui::goLightsTarget(b, live, true, true);
        QVERIFY(!t.ok);
        QVERIFY(t.reason.contains(QStringLiteral("End of desk cue list 2")));
        live.deskActiveList = QStringLiteral("1");           // the desk is on another list
        QCOMPARE(ui::goLightsTarget(b, live, true, true).number, QStringLiteral("1"));
        QVERIFY(!ui::goLightsTarget(b, live, true, false).ok);   // not live
        QVERIFY(ui::goLightsTarget(b, live, true, false).reason.contains(QStringLiteral("isn't connected")));
        QVERIFY(ui::goLightsTarget(b, live, false, true).reason.contains(QStringLiteral("Lighting Desk")));
        b.deskCues.clear();
        QVERIFY(ui::goLightsTarget(b, live, true, true).reason.contains(QStringLiteral("no cues")));

        const auto a = ui::goLightsAction(ui::goLightsTarget(
            [] { ui::MatrixBuild x; x.deskList = QStringLiteral("2"); return x; }(),
            [] { ui::MatrixLive l; l.deskPendingList = QStringLiteral("2"); l.deskPendingCue = QStringLiteral("8.4"); return l; }(),
            true, true));
        QCOMPARE(a.kind, audio::TriggerAction::Kind::Desk);
        QCOMPARE(a.deskDo, audio::TriggerAction::DeskDo::GoToCue);
        QCOMPARE(a.list, 2);
        QCOMPARE(a.number, QStringLiteral("8.4"));
    }

    // The button, against a fake desk: it shows the desk's next cue, fires
    // exactly /eos/cue/1/6.5/fire through the lighting-desk send path (UDP to
    // the desk's OSC port), and a row click never fires anything.
    void goLightsFiresTheDesksNextCue()
    {
        Show s;
        build(s);
        FakeDeskLink desk;
        QVERIFY(desk.start());
        osc::EosCueLists reader(&desk.fb);
        ui::MatrixView view;
        view.setWorkspace(&s.ws);
        view.setDesk(&reader, &desk.fb);
        view.setCueList(s.matrix);
        auto *button = view.findChild<QPushButton *>(QStringLiteral("matrixGoLights"));
        QVERIFY(button);
        QVERIFY(!button->isEnabled());                       // not live yet
        QVERIFY(button->toolTip().contains(QStringLiteral("isn't connected")));

        desk.say("/eos/out/active/cue/text", QStringLiteral("1/6 Look 6 4.00 100%"));
        desk.say("/eos/out/pending/cue/text", QStringLiteral("1/6.5 Look 6.5 4.00 0%"));
        QTRY_COMPARE(desk.fb.pending().cue, QStringLiteral("6.5"));
        feedDesk(reader, {QStringLiteral("5"), QStringLiteral("6"), QStringLiteral("6.5"), QStringLiteral("7")});
        view.rebuildNow();
        view.pollLive();
        QVERIFY(button->isEnabled());
        QCOMPARE(button->text(), QStringLiteral("GO Lights  6.5  Look 6.5"));
        // The NEXT marker is on the same cue the button fires.
        const int next = view.build().rowOfDesk(QStringLiteral("1"), QStringLiteral("6.5"));
        QVERIFY(view.model()->data(view.model()->index(next, 0)).toString().contains(QStringLiteral("LX NEXT")));
        const int liveRow = view.build().rowOfDesk(QStringLiteral("1"), QStringLiteral("6"));
        QVERIFY(view.model()->data(view.model()->index(liveRow, 0)).toString().contains(QStringLiteral("LX LIVE")));

        // Clicking a row (or double-clicking it) doesn't ask for GO Lights.
        QSignalSpy go(&view, &ui::MatrixView::goLightsRequested);
        view.table()->setCurrentIndex(view.model()->index(next, 1));
        emit view.table()->clicked(view.model()->index(next, 1));
        QCOMPARE(go.count(), 0);
        button->click();
        QCOMPARE(go.count(), 1);

        // What MainWindow then sends, through GoEngine's desk path, to a desk
        // listening on UDP.
        QUdpSocket udpDesk;
        QVERIFY(udpDesk.bind(QHostAddress::LocalHost, 0));
        osc::OscEngine oscEngine;
        GoEngine engine;
        engine.setOscEngine(&oscEngine);
        core::LightingDesk ld;
        ld.type = core::LightingDesk::Type::Eos;
        ld.host = QStringLiteral("127.0.0.1");
        ld.port = udpDesk.localPort();
        QVERIFY(engine.sendDeskAction(ui::goLightsAction(view.goLightsTarget()), ld));
        QTRY_VERIFY(udpDesk.hasPendingDatagrams());
        QByteArray dgram(int(udpDesk.pendingDatagramSize()), Qt::Uninitialized);
        udpDesk.readDatagram(dgram.data(), dgram.size());
        const auto el = osc::Codec::decode(dgram);
        QVERIFY(el);
        const auto *msg = std::get_if<osc::Message>(&*el);
        QVERIFY(msg);
        QCOMPARE(msg->address, QStringLiteral("/eos/cue/1/6.5/fire"));
        QVERIFY(msg->args.empty());
        QTest::qWait(100);
        QVERIFY(!udpDesk.hasPendingDatagrams());             // exactly one message

        // The page says where it's reading from.
        auto *summary = view.findChild<QLabel *>(QStringLiteral("matrixSummary"));
        QVERIFY(summary->text().contains(QStringLiteral("Main")));
        QVERIFY(summary->text().contains(QStringLiteral("Live from")));
        QVERIFY(summary->text().contains(QStringLiteral("127.0.0.1")));
    }

    // Nothing to show: the page says what to do, not an empty grid.
    void emptyStateExplains()
    {
        core::Workspace ws;
        ws.addCueList(std::make_unique<core::CueList>(QStringLiteral("Main")));
        auto mx = std::make_unique<core::CueList>(QStringLiteral("Matrix"));
        mx->setKind(core::CueList::Kind::Matrix);
        auto *matrix = ws.addCueList(std::move(mx));
        ui::MatrixView view;
        view.setWorkspace(&ws);
        view.setCueList(matrix);
        auto *title = view.findChild<QLabel *>(QStringLiteral("matrixEmptyTitle"));
        QVERIFY(title && !title->text().isEmpty());
        QVERIFY(view.findChild<QLabel *>(QStringLiteral("matrixEmptyText"))->text().contains(QStringLiteral("Eos")));
        QSignalSpy desk(&view, &ui::MatrixView::deskSettingsRequested);
        view.findChild<QPushButton *>(QStringLiteral("matrixEmptyDesk"))->click();
        QCOMPARE(desk.count(), 1);
    }

    // QUEWI_RENDER_DIR=<folder>: write a PNG of the view mid-show, to check
    // the look (the app's QSS isn't linked into tests — palette only).
    void renderForLooking()
    {
        const QString dir = qEnvironmentVariable("QUEWI_RENDER_DIR");
        if (dir.isEmpty()) QSKIP("QUEWI_RENDER_DIR not set");
        QDir().mkpath(dir);
        Q_INIT_RESOURCE(resources);
        qApp->setStyleSheet(ui::Theme::load(QStringLiteral("quewi-dark")));

        // A show like Matthew's: songs with lighting, a scene change, an OSC
        // cue to the desk, and a desk list with labels, scenes and notes.
        Show s;
        build(s);
        auto extra = std::make_unique<cues::MemoCue>();
        extra->setField(QStringLiteral("number"), 5.0);
        extra->setField(QStringLiteral("name"), QStringLiteral("Interval"));
        extra->setField(QStringLiteral("notes"), QStringLiteral("House lights to half on the applause"));
        s.main->insertCue(4, std::move(extra));
        s.main->cueAt(0)->setField(QStringLiteral("notes"), QStringLiteral("GO on the house-to-half"));
        FakeDeskLink desk;
        QVERIFY(desk.start());
        desk.say("/eos/out/active/cue/text", QStringLiteral("1/6 Chorus wash 4.00 100%"));
        desk.say("/eos/out/pending/cue/text", QStringLiteral("1/6.5 Bridge 4.00 0%"));
        QTRY_COMPARE(desk.fb.pending().cue, QStringLiteral("6.5"));
        osc::EosCueLists reader(&desk.fb);
        auto cfg = s.matrix->matrixConfig();
        m::DeskCue lx8;
        lx8.list = QStringLiteral("1");
        lx8.number = QStringLiteral("8");
        lx8.uid = QStringLiteral("uid-8");
        cfg.place(lx8, m::ManualPlacement::Mode::With, s.memo);
        m::DeskCue lx12;
        lx12.list = QStringLiteral("1");
        lx12.number = QStringLiteral("12");
        lx12.label = QStringLiteral("Old special");
        cfg.place(lx12, m::ManualPlacement::Mode::After, s.osc);
        s.matrix->setMatrixConfig(cfg);

        ui::MatrixView view;
        view.setAttribute(Qt::WA_DontShowOnScreen);
        view.setWorkspace(&s.ws);
        view.setDesk(&reader, &desk.fb);
        view.setCueList(s.matrix);
        feedDeskLabelled(reader, {
            {QStringLiteral("0.5"), QStringLiteral("Preset"), QStringLiteral("Act 1"), QStringLiteral("Walk-in state"), QStringLiteral("0")},
            {QStringLiteral("1"), QStringLiteral("House to half"), QString(), QString(), QStringLiteral("5000")},
            {QStringLiteral("5"), QStringLiteral("Overture look"), QString(), QStringLiteral("Slow build"), QStringLiteral("8000"), QStringLiteral("5000")},
            {QStringLiteral("6"), QStringLiteral("Chorus wash")},
            {QStringLiteral("6.5"), QStringLiteral("Bridge")},
            {QStringLiteral("7"), QStringLiteral("Storm"), QStringLiteral("The storm"), QStringLiteral("Lightning on the thunder")},
            {QStringLiteral("8"), QStringLiteral("Flats in")},
            {QStringLiteral("9"), QStringLiteral("Calm after")},
            {QStringLiteral("10"), QStringLiteral("Interval state"), QStringLiteral("Interval")},
        });
        ui::MatrixLive live;
        live.standby = s.osc;
        live.running = {s.song};
        view.setLiveProvider([&live] { return live; });
        for (const QSize sz : {QSize(1280, 720), QSize(1920, 1080)}) {
            view.resize(sz);
            view.show();
            view.rebuildNow();
            view.pollLive();
            QTest::qWait(150);
            view.grab().save(QStringLiteral("%1/matrix-live-%2x%3.png").arg(dir).arg(sz.width()).arg(sz.height()));
        }

        // The desk gone: the cached cues, and the banner saying so.
        desk.peer->abort();
        desk.server.close();
        desk.fb.stop();
        view.rebuildNow();
        view.pollLive();
        view.resize(1280, 720);
        QTest::qWait(150);
        view.grab().save(QStringLiteral("%1/matrix-desk-off-1280x720.png").arg(dir));

        // A brand-new Matrix List with nothing to show.
        core::Workspace ws;
        ws.addCueList(std::make_unique<core::CueList>(QStringLiteral("Main")));
        auto mx = std::make_unique<core::CueList>(QStringLiteral("Matrix"));
        mx->setKind(core::CueList::Kind::Matrix);
        auto *matrix = ws.addCueList(std::move(mx));
        ui::MatrixView empty;
        empty.setAttribute(Qt::WA_DontShowOnScreen);
        empty.setWorkspace(&ws);
        empty.setCueList(matrix);
        empty.resize(1280, 720);
        empty.show();
        QTest::qWait(150);
        empty.grab().save(QStringLiteral("%1/matrix-empty-1280x720.png").arg(dir));

        // Show Mode with the Matrix List up: GO Lights under GO.
        ui::ShowSnapshot snap;
        ui::ShowCueLine sb;
        sb.number = QStringLiteral("3");
        sb.name = QStringLiteral("Thunder");
        sb.type = QStringLiteral("OSC");
        sb.deskCues = QStringLiteral("LX 7 Storm");
        snap.standby = sb;
        snap.listName = QStringLiteral("Main");
        ui::ShowCueLine d;
        d.deskOnly = true;
        d.number = QStringLiteral("LX 8");
        d.name = QStringLiteral("Flats in");
        snap.comingUp = {d};
        snap.lightsGo = ui::ShowLightsGo{true, QStringLiteral("GO Lights  6.5\nBridge"), QString()};
        ui::ShowModeView sm;
        sm.setAttribute(Qt::WA_DontShowOnScreen);
        sm.resize(1280, 720);
        sm.setSnapshot(snap);
        sm.show();
        QTest::qWait(150);
        sm.grab().save(QStringLiteral("%1/showmode-golights-1280x720.png").arg(dir));
    }
};

QTEST_MAIN(MatrixViewTests)
#include "test_matrix_view.moc"
