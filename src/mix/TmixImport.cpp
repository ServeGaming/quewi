#include "mix/TmixImport.h"

#include "core/CueList.h"
#include "core/UndoCommands.h"
#include "core/Workspace.h"
#include "mix/MixCue.h"
#include "mix/MixShow.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QUndoStack>

#include <algorithm>
#include <atomic>

namespace quewi::mix {

namespace {

QString tr(const char *s) { return QCoreApplication::translate("TmixImport", s); }

QSet<int> stripsOf(const QString &text)
{
    QSet<int> out;
    static const QRegularExpression sep(QStringLiteral("[,;\\s]+"));
    for (const QString &t : text.split(sep, Qt::SkipEmptyParts)) {
        bool ok = false;
        const int v = t.toInt(&ok);
        if (ok && v > 0) out.insert(v);
    }
    return out;
}

// TheatreMix names a channel it hasn't been told about "Channel 7"; that's
// no name at all.
bool isPlaceholder(const QString &name, int strip)
{
    return name.isEmpty()
        || name.compare(QStringLiteral("Channel %1").arg(strip), Qt::CaseInsensitive) == 0;
}

bool hasTable(const QSqlDatabase &db, const QString &table)
{
    return db.tables().contains(table, Qt::CaseInsensitive);
}

} // namespace

bool readTmix(const QString &path, TmixShow *out, QString *error)
{
    auto fail = [&](const QString &why) {
        if (error) *error = why;
        return false;
    };
    if (!out) return false;
    *out = {};
    if (!QFileInfo(path).isFile()) return fail(tr("The file isn't there."));

    // SQLite will happily "open" any file; the header says whether it is one.
    {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) return fail(tr("The file can't be read."));
        if (!f.read(16).startsWith("SQLite format 3"))
            return fail(tr("That isn't a TheatreMix show file."));
    }

    static std::atomic<int> counter{0};
    const QString conn = QStringLiteral("quewi_tmix_%1").arg(++counter);
    bool ok = true;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
        db.setDatabaseName(path);
        db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
        if (!db.open()) {
            ok = fail(tr("The file can't be opened: %1").arg(db.lastError().text()));
        } else if (!hasTable(db, QStringLiteral("cues"))) {
            ok = fail(tr("That isn't a TheatreMix show file (it has no cues)."));
        } else {
            QSqlQuery q(db);

            if (hasTable(db, QStringLiteral("config"))
                && q.exec(QStringLiteral("SELECT param, value FROM config"))) {
                while (q.next()) {
                    const QString key = q.value(0).toString();
                    if (key == QLatin1String("targetConsole")) out->console = q.value(1).toString();
                    else if (key == QLatin1String("channels")) out->strips.unite(stripsOf(q.value(1).toString()));
                }
            }

            // Which dcaNN columns this file has.
            const QSqlRecord layout = db.record(QStringLiteral("cues"));
            static const QRegularExpression dcaCol(QStringLiteral("^dca(\\d+)Channels$"),
                                                   QRegularExpression::CaseInsensitiveOption);
            QVector<int> dcaCols;
            for (int i = 0; i < layout.count(); ++i) {
                const auto m = dcaCol.match(layout.fieldName(i));
                if (m.hasMatch()) dcaCols.push_back(m.captured(1).toInt());
            }
            std::sort(dcaCols.begin(), dcaCols.end());

            if (q.exec(QStringLiteral("SELECT * FROM cues"))) {
                while (q.next()) {
                    const QSqlRecord r = q.record();
                    TmixCue c;
                    const int number = r.value(QStringLiteral("number")).toInt();
                    const int point = r.indexOf(QStringLiteral("point")) >= 0
                                    ? r.value(QStringLiteral("point")).toInt() : 0;
                    // 2 and 5 is cue 2.5; 2 and 15 is 2.15.
                    c.number = point > 0
                             ? QStringLiteral("%1.%2").arg(number).arg(point).toDouble()
                             : double(number);
                    c.name = r.value(QStringLiteral("name")).toString();
                    for (int dca : dcaCols) {
                        const QString key = QStringLiteral("dca%1").arg(dca, 2, 10, QLatin1Char('0'));
                        const QSet<int> s = stripsOf(r.value(key + QStringLiteral("Channels")).toString());
                        if (!s.isEmpty()) {
                            c.dcas.insert(dca, s);
                            out->strips.unite(s);
                            out->highestDca = std::max(out->highestDca, dca);
                        }
                        const QString label = r.value(key + QStringLiteral("Label")).toString().trimmed();
                        if (!label.isEmpty()) c.labels.insert(dca, label);
                    }
                    out->cues.push_back(std::move(c));
                }
            }
            std::stable_sort(out->cues.begin(), out->cues.end(),
                             [](const TmixCue &a, const TmixCue &b) { return a.number < b.number; });

            // A channel's default profile carries its name.
            if (hasTable(db, QStringLiteral("profiles"))) {
                const QSqlRecord p = db.record(QStringLiteral("profiles"));
                if (p.indexOf(QStringLiteral("channel")) >= 0
                    && q.exec(QStringLiteral("SELECT * FROM profiles"))) {
                    const bool hasDefault = p.indexOf(QStringLiteral("default")) >= 0;
                    while (q.next()) {
                        const QSqlRecord r = q.record();
                        const int strip = r.value(QStringLiteral("channel")).toInt();
                        if (strip <= 0 || out->channelNames.contains(strip)) continue;
                        if (hasDefault && r.value(QStringLiteral("default")).toInt() == 0) continue;
                        QString name = r.value(QStringLiteral("label")).toString().trimmed();
                        if (name.isEmpty()) name = r.value(QStringLiteral("name")).toString().trimmed();
                        if (!isPlaceholder(name, strip)) out->channelNames.insert(strip, name);
                    }
                }
            }

            if (hasTable(db, QStringLiteral("actors"))
                && q.exec(QStringLiteral("SELECT channel, name FROM actors"))) {
                while (q.next()) {
                    const int strip = q.value(0).toInt();
                    const QString name = q.value(1).toString().trimmed();
                    if (strip > 0 && !name.isEmpty() && !out->actors.contains(strip))
                        out->actors.insert(strip, name);
                }
            }

            if (hasTable(db, QStringLiteral("ensembles"))
                && q.exec(QStringLiteral("SELECT name, channels FROM ensembles"))) {
                while (q.next()) {
                    const QString name = q.value(0).toString().trimmed();
                    const QSet<int> s = stripsOf(q.value(1).toString());
                    if (!name.isEmpty() && !s.isEmpty()) out->ensembles.insert(name, s);
                }
            }
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(conn);
    if (!ok) return false;
    if (out->cues.isEmpty()) return fail(tr("That TheatreMix file has no cues in it."));
    return true;
}

TmixApplied applyTmix(const TmixShow &show, core::Workspace *ws, core::CueList *list)
{
    TmixApplied done;
    if (!ws || !list || !ws->mixShow()) return done;
    MixShow *mix = ws->mixShow();

    // The channels first: a cue only drives strips the show knows about.
    QList<int> strips(show.strips.cbegin(), show.strips.cend());
    std::sort(strips.begin(), strips.end());
    for (int strip : strips) {
        MixChannel ch = mix->channel(strip);
        const QString name = show.channelNames.value(strip);
        const QString actor = show.actors.value(strip);
        if (!ch.isValid()) {
            ch.strip = strip;
            ch.name = name;
            ch.actor = actor;
            mix->setChannel(ch);
            ++done.channelsAdded;
        } else if ((ch.name.isEmpty() && !name.isEmpty()) || (ch.actor.isEmpty() && !actor.isEmpty())) {
            if (ch.name.isEmpty() && !name.isEmpty()) { ch.name = name; ++done.channelsNamed; }
            if (ch.actor.isEmpty()) ch.actor = actor;
            mix->setChannel(ch);
        }
    }
    const QStringList have = mix->ensembleNames();
    for (auto it = show.ensembles.cbegin(); it != show.ensembles.cend(); ++it) {
        const bool exists = std::any_of(have.cbegin(), have.cend(), [&](const QString &n) {
            return n.compare(it.key(), Qt::CaseInsensitive) == 0;
        });
        if (exists) continue;
        mix->setEnsemble(it.key(), it.value());
        ++done.ensembles;
    }
    const int wanted = std::clamp(show.highestDca, MixShow::kMinDcaCount, MixShow::kMaxDcaCount);
    if (wanted > mix->dcaCount()) mix->setDcaCount(wanted);
    done.dcaCount = mix->dcaCount();

    auto *stack = ws->undoStack();
    if (stack) stack->beginMacro(tr("Import TheatreMix cues"));
    for (const TmixCue &c : show.cues) {
        auto cue = std::make_unique<MixCue>();
        cue->setField(QStringLiteral("number"), c.number);
        cue->setField(QStringLiteral("name"), c.name);
        for (auto it = c.dcas.cbegin(); it != c.dcas.cend(); ++it) {
            if (it.key() > MixShow::kMaxDcaCount) { ++done.dcasDropped; continue; }
            cue->setDcaStrips(it.key(), it.value());
        }
        const int row = list->cueCount();
        if (stack) stack->push(new core::InsertCueCommand(list, row, std::move(cue)));
        else list->insertCue(row, std::move(cue));
        ++done.cues;
    }
    if (stack) stack->endMacro();
    return done;
}

} // namespace quewi::mix
