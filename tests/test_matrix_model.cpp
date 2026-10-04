#include <QTest>

#include "core/MatrixModel.h"

#include <QElapsedTimer>
#include <QJsonArray>

using namespace quewi::core::matrix;

namespace {

QuewiCue q(const char *number, const char *name, std::vector<DeskFire> fires = {})
{
    QuewiCue c;
    c.id = QUuid::createUuid();
    c.number = QString::fromLatin1(number);
    c.name = QString::fromLatin1(name);
    c.type = QStringLiteral("Audio");
    c.fires = std::move(fires);
    return c;
}

DeskCue d(const char *number, const char *label, int part = 0, const char *list = "1")
{
    DeskCue c;
    c.list = QString::fromLatin1(list);
    c.number = QString::fromLatin1(number);
    c.part = part;
    c.uid = QStringLiteral("uid-%1-%2-%3").arg(QString::fromLatin1(list), QString::fromLatin1(number)).arg(part);
    c.label = QString::fromLatin1(label);
    return c;
}

DeskFire fire(const char *number, double at = -1.0, const char *list = "1")
{
    return {QString::fromLatin1(list), QString::fromLatin1(number), at, at >= 0 ? QStringLiteral("Hit") : QString()};
}

// "Q1[LX1,LX2] Q2 ~LX3@12 LX4" — a compact picture of the rows.
QString picture(const Result &r, const std::vector<QuewiCue> &quewi)
{
    QStringList out;
    for (const auto &row : r.rows) {
        QStringList lx;
        for (const auto &c : row.desk) {
            QString t = QStringLiteral("LX") + c.cue.number;
            if (c.cue.part > 0) t += QStringLiteral("p%1").arg(c.cue.part);
            if (!c.parts.empty()) t += QStringLiteral("+%1").arg(c.parts.size());
            if (c.missing) t += QLatin1Char('!');
            lx << t;
        }
        switch (row.kind) {
        case Row::Kind::Quewi:
            out << QStringLiteral("Q") + quewi[size_t(row.quewiIndex)].number
                       + (lx.isEmpty() ? QString() : QStringLiteral("[%1]").arg(lx.join(QLatin1Char(','))));
            break;
        case Row::Kind::Hit:
            out << QStringLiteral("~%1@%2").arg(lx.join(QLatin1Char(','))).arg(row.desk.front().atSeconds);
            break;
        case Row::Kind::Desk:
            out << lx.join(QLatin1Char(','));
            break;
        }
    }
    return out.join(QLatin1Char(' '));
}

} // namespace

// The Matrix List's intermeshing rules (core/MatrixModel), with no show,
// desk or widget: what fires what, hand placements, desk order, parts,
// renumbered and missing cues, and the saved settings.
class MatrixModelTests : public QObject {
    Q_OBJECT

private slots:
    void quewiOnlyAndDeskOnly()
    {
        const std::vector<QuewiCue> quewi{q("1", "Preshow"), q("2", "Overture")};
        Config cfg;
        auto r = intermesh(quewi, {}, cfg, false);
        QCOMPARE(picture(r, quewi), QStringLiteral("Q1 Q2"));
        // Desk only (no quewi cues at all): every desk cue at the top, in order.
        r = intermesh({}, {d("1", "A"), d("2", "B")}, cfg);
        QCOMPARE(picture(r, {}), QStringLiteral("LX1 LX2"));
    }

    // Fired at GO → same row; hit in a song → nested row by time; anything
    // else follows the desk cue before it.
    void automaticPlacement()
    {
        const std::vector<QuewiCue> quewi{
            q("1", "Preshow music", {fire("1")}),
            q("2", "Overture", {fire("3", 42.0), fire("2.5", 12.0)}),
            q("3", "Scene 1 SFX"),
            q("4", "Thunder", {fire("10")}),
        };
        const std::vector<DeskCue> desk{d("0.5", "Preset"), d("1", "House half"), d("2", "House out"),
                                        d("2.5", "Lights up"), d("3", "Chorus"), d("4", "Button"),
                                        d("10", "Storm"), d("11", "After storm")};
        const auto r = intermesh(quewi, desk, Config{});
        QCOMPARE(picture(r, quewi),
                 QStringLiteral("LX0.5 Q1[LX1] LX2 Q2 ~LX2.5@12 ~LX3@42 LX4 Q3 Q4[LX10] LX11"));
        // How each got there.
        QCOMPARE(r.rows[1].desk[0].how, How::Fired);
        QCOMPARE(r.rows[4].desk[0].how, How::Trigger);
        QCOMPARE(r.rows[4].desk[0].triggerName, QStringLiteral("Hit"));
        QCOMPARE(r.rows[2].desk[0].how, How::Order);
        QCOMPARE(r.rows[6].quewiIndex, 1);   // LX4 follows the song LX3 was hit in
    }

    // "12.50" in quewi meets "12.5" on the desk; a fire with no list means
    // the desk's default; fires for another list are ignored.
    void numbersAndListsMatch()
    {
        const std::vector<QuewiCue> quewi{
            q("1", "A", {{QString(), QStringLiteral("12.50"), -1.0, {}}}),
            q("2", "B", {fire("5", -1.0, "2")}),
        };
        const auto r = intermesh(quewi, {d("12.5", "X"), d("5", "Y")}, Config{});
        QCOMPARE(picture(r, quewi), QStringLiteral("Q1[LX12.5] LX5 Q2"));
    }

    void partsRideWithTheirCue()
    {
        const std::vector<QuewiCue> quewi{q("1", "A", {fire("10")})};
        const std::vector<DeskCue> desk{d("10", "Storm"), d("10", "lightning", 1), d("10", "rain", 2),
                                        d("11", "Calm")};
        const auto r = intermesh(quewi, desk, Config{});
        QCOMPARE(picture(r, quewi), QStringLiteral("Q1[LX10+2] LX11"));
        QCOMPARE(r.rows[0].desk[0].parts[1].label, QStringLiteral("rain"));
        // A part reported before its cue still folds in under the cue.
        const auto r2 = intermesh(quewi, {d("10", "lightning", 1), d("10", "Storm")}, Config{});
        QCOMPARE(picture(r2, quewi), QStringLiteral("Q1[LX10+1]"));
        QCOMPARE(r2.rows[0].desk[0].cue.label, QStringLiteral("Storm"));
    }

    // Placed by hand beats automatic; With / After / Start.
    void manualPlacement()
    {
        std::vector<QuewiCue> quewi{q("1", "A", {fire("1")}), q("2", "B"), q("3", "C")};
        const std::vector<DeskCue> desk{d("1", "One"), d("2", "Two"), d("3", "Three"), d("4", "Four")};
        Config cfg;
        cfg.place(desk[0], ManualPlacement::Mode::With, quewi[2].id);    // moves off Q1 (fired)
        cfg.place(desk[1], ManualPlacement::Mode::After, quewi[1].id);
        cfg.place(desk[3], ManualPlacement::Mode::Start, {});
        auto r = intermesh(quewi, desk, cfg);
        // LX3 follows LX2 (desk order) into the slot after Q2.
        QCOMPARE(picture(r, quewi), QStringLiteral("LX4 Q1 Q2 LX2 LX3 Q3[LX1]"));
        QCOMPARE(r.rows[5].desk[0].how, How::Manual);

        // Placing again replaces; unplace goes back to automatic.
        cfg.place(desk[0], ManualPlacement::Mode::After, quewi[0].id);
        QCOMPARE(cfg.placements.size(), size_t(3));
        QVERIFY(cfg.unplace(QStringLiteral("1"), QStringLiteral("4"), 0));
        QVERIFY(!cfg.unplace(QStringLiteral("1"), QStringLiteral("99"), 0));
        r = intermesh(quewi, desk, cfg);
        QCOMPARE(picture(r, quewi), QStringLiteral("Q1 LX1 Q2 LX2 LX3 LX4 Q3"));
    }

    // The desk renumbers a placed cue (same UID): the placement follows it
    // and says so. The desk deletes one: kept, marked missing, where it was.
    void renumberedAndMissing()
    {
        std::vector<QuewiCue> quewi{q("1", "A"), q("2", "B", {fire("40")})};
        Config cfg;
        cfg.place(d("5", "Sunrise"), ManualPlacement::Mode::With, quewi[0].id);
        cfg.place(d("6", "Noon"), ManualPlacement::Mode::After, quewi[0].id);

        DeskCue renumbered = d("5", "Sunrise");
        renumbered.number = QStringLiteral("5.5");          // same uid, new number
        auto r = intermesh(quewi, {renumbered}, cfg);
        QCOMPARE(picture(r, quewi), QStringLiteral("Q1[LX5.5] LX6! Q2[LX40!]"));
        QCOMPARE(r.renumbered.size(), size_t(1));
        QCOMPARE(r.renumbered[0].placement, 0);
        QCOMPARE(r.renumbered[0].number, QStringLiteral("5.5"));
        QCOMPARE(r.rows[1].desk[0].cue.label, QStringLiteral("Noon"));   // what was last known

        // Desk not read yet (nothing cached): placements still show, but
        // nothing can be called missing.
        r = intermesh(quewi, {}, cfg, false);
        QCOMPARE(picture(r, quewi), QStringLiteral("Q1[LX5] LX6 Q2"));
    }

    // The quewi cue a placement hangs off was deleted: the desk cue goes back
    // to automatic for now, and the placement is reported, not dropped.
    void orphanedPlacement()
    {
        std::vector<QuewiCue> quewi{q("1", "A"), q("2", "B")};
        Config cfg;
        cfg.place(d("3", "C"), ManualPlacement::Mode::With, QUuid::createUuid());
        const auto r = intermesh(quewi, {d("3", "C")}, cfg);
        QCOMPARE(picture(r, quewi), QStringLiteral("LX3 Q1 Q2"));
        QCOMPARE(r.orphaned, std::vector<int>{0});
        QCOMPARE(cfg.placements.size(), size_t(1));
    }

    void configRoundTrip()
    {
        Config cfg;
        cfg.sourceList = QUuid::createUuid();
        cfg.deskList = QStringLiteral("2");
        cfg.place(d("5", "Sunrise", 0, "2"), ManualPlacement::Mode::After, QUuid::createUuid());
        cfg.place(d("1", "Pre", 0, "2"), ManualPlacement::Mode::Start, {});
        DeskCue cached = d("5", "Sunrise", 0, "2");
        cached.notes = QStringLiteral("Slow");
        cached.scene = QStringLiteral("Act 1");
        cached.sceneEnd = true;
        cached.upSeconds = 5.0;
        cached.followSeconds = 0.0;
        cfg.deskCache = {cached, d("5", "flash", 1, "2")};
        cfg.deskCachedAt = QDateTime(QDate(2026, 10, 4), QTime(21, 14, 0));
        const Config back = Config::fromJson(cfg.toJson());
        QVERIFY(back == cfg);
        QCOMPARE(back.deskCache[0].followSeconds, 0.0);
        QCOMPARE(back.deskCache[0].downSeconds, -1.0);
        QCOMPARE(cfg.toJson().value(QStringLiteral("version")).toInt(), Config::kVersion);

        // Old / foreign JSON: unknown keys ignored, missing ones defaulted.
        QJsonObject odd{{QStringLiteral("version"), 99}, {QStringLiteral("lanes"), QJsonArray{}},
                        {QStringLiteral("placements"), QJsonArray{QJsonObject{
                            {QStringLiteral("list"), QStringLiteral("1")},
                            {QStringLiteral("number"), QStringLiteral("3")},
                            {QStringLiteral("mode"), QStringLiteral("sideways")}}}}};
        const Config c2 = Config::fromJson(odd);
        QCOMPARE(c2.deskList, QStringLiteral("1"));
        QCOMPARE(c2.placements.size(), size_t(1));
        QCOMPARE(c2.placements[0].mode, ManualPlacement::Mode::With);
        QVERIFY(Config::fromJson({}).placements.empty());
    }

    // A 300-row show stays quick (the view rebuilds on every edit).
    void bigShowIsQuick()
    {
        std::vector<QuewiCue> quewi;
        std::vector<DeskCue> desk;
        for (int i = 1; i <= 150; ++i) {
            const QString n = QString::number(i);
            QuewiCue c = q("0", "S");
            c.number = n;
            c.fires.push_back({QStringLiteral("1"), QString::number(i * 2), -1.0, {}});
            quewi.push_back(c);
        }
        for (int i = 1; i <= 300; ++i) {
            DeskCue c = d("0", "L");
            c.number = QString::number(i);
            c.uid = QStringLiteral("u%1").arg(i);
            desk.push_back(c);
        }
        Config cfg;
        for (int i = 0; i < 50; ++i) cfg.place(desk[size_t(i * 6)], ManualPlacement::Mode::After, quewi[size_t(i)].id);
        QElapsedTimer t;
        t.start();
        const auto r = intermesh(quewi, desk, cfg);
        QVERIFY2(t.elapsed() < 250, qPrintable(QString::number(t.elapsed())));
        int deskSeen = 0;
        for (const auto &row : r.rows) deskSeen += int(row.desk.size());
        QCOMPARE(deskSeen, 300);
    }
};

QTEST_MAIN(MatrixModelTests)
#include "test_matrix_model.moc"
