#include "show/ShowFile.h"

#include "core/CueList.h"
#include "core/Workspace.h"
#include "audio/AudioCue.h"
#include "cues/Cue.h"
#include "cues/FadeCue.h"
#include "cues/MemoCue.h"
#include "lighting/LightCue.h"
#include "osc/OscCue.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <QVariant>

namespace quewi::show {

namespace {

QString g_lastError;

constexpr int kSchemaVersion = 1;

constexpr const char *kDdl =
    "CREATE TABLE IF NOT EXISTS meta ("
    "  key TEXT PRIMARY KEY, value TEXT);"

    "CREATE TABLE IF NOT EXISTS cue_lists ("
    "  id   TEXT PRIMARY KEY,"
    "  name TEXT NOT NULL,"
    "  ord  INTEGER NOT NULL);"

    "CREATE TABLE IF NOT EXISTS cues ("
    "  id        TEXT PRIMARY KEY,"
    "  list_id   TEXT NOT NULL REFERENCES cue_lists(id) ON DELETE CASCADE,"
    "  parent_id TEXT,"
    "  ord       INTEGER NOT NULL,"
    "  type      TEXT NOT NULL,"
    "  payload   TEXT NOT NULL);";

QString uniqueConnName()
{
    return QStringLiteral("quewi-show-%1").arg(QUuid::createUuid().toString());
}

void setError(const QString &what)
{
    g_lastError = what;
}

void setError(const QString &what, const QSqlError &e)
{
    g_lastError = QStringLiteral("%1: %2").arg(what, e.text());
}

std::unique_ptr<cues::Cue> makeCue(const QString &type)
{
    if (type == QLatin1String("memo"))       return std::make_unique<cues::MemoCue>();
    if (type == QLatin1String("osc"))        return std::make_unique<osc::OscCue>();
    if (type == QLatin1String("audio"))      return std::make_unique<audio::AudioCue>();
    if (type == QLatin1String("fade"))       return std::make_unique<cues::FadeCue>();
    if (type == QLatin1String("light"))      return std::make_unique<lighting::LightCue>();
    if (type == QLatin1String("light-fade")) return std::make_unique<lighting::LightFadeCue>();
    // Unknown type: load as a Memo so the user doesn't lose data, with a
    // note. Future cue types register here in Phase 6.
    auto memo = std::make_unique<cues::MemoCue>();
    memo->setField(QStringLiteral("notes"),
                   QStringLiteral("(unknown cue type: %1)").arg(type));
    return memo;
}

} // namespace

QString ShowFile::lastError() { return g_lastError; }

bool ShowFile::load(const QString &path, core::Workspace &workspace)
{
    g_lastError.clear();
    if (!QFile::exists(path)) {
        setError(QStringLiteral("File not found: %1").arg(path));
        return false;
    }

    const QString conn = uniqueConnName();
    {
        auto db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
        db.setDatabaseName(path);
        if (!db.open()) {
            setError(QStringLiteral("Open failed"), db.lastError());
            QSqlDatabase::removeDatabase(conn);
            return false;
        }

        QSqlQuery q(db);

        // Cue lists in document order.
        if (!q.exec(QStringLiteral("SELECT id, name FROM cue_lists ORDER BY ord ASC"))) {
            setError(QStringLiteral("Read cue_lists"), q.lastError());
            db.close();
            QSqlDatabase::removeDatabase(conn);
            return false;
        }
        struct ListRow { core::CueListId id; QString name; };
        std::vector<ListRow> lists;
        while (q.next()) {
            lists.push_back({
                QUuid(q.value(0).toString()),
                q.value(1).toString()
            });
        }

        for (const auto &row : lists) {
            auto list = std::make_unique<core::CueList>(row.name);
            list->setId(row.id);

            QSqlQuery cq(db);
            cq.prepare(QStringLiteral(
                "SELECT id, type, payload FROM cues "
                "WHERE list_id = ? ORDER BY ord ASC"));
            cq.addBindValue(row.id.toString());
            if (!cq.exec()) {
                setError(QStringLiteral("Read cues"), cq.lastError());
                db.close();
                QSqlDatabase::removeDatabase(conn);
                return false;
            }
            int ord = 0;
            while (cq.next()) {
                const auto cueId   = QUuid(cq.value(0).toString());
                const auto type    = cq.value(1).toString();
                const auto payload = cq.value(2).toString();

                auto cue = makeCue(type);
                cue->setId(cueId);
                const auto doc = QJsonDocument::fromJson(payload.toUtf8());
                if (doc.isObject()) cue->fromPayload(doc.object());
                list->insertCue(ord++, std::move(cue));
            }

            workspace.addCueList(std::move(list));
        }

        // Workspace name from meta.
        QSqlQuery mq(db);
        if (mq.exec(QStringLiteral("SELECT value FROM meta WHERE key='workspace_name'"))
            && mq.next()) {
            workspace.setName(mq.value(0).toString());
        }

        db.close();
    }
    QSqlDatabase::removeDatabase(conn);
    workspace.markClean();
    return true;
}

bool ShowFile::save(const QString &path, const core::Workspace &workspace)
{
    g_lastError.clear();

    // Atomic write: open <path>.tmp, write, fsync via close, rename.
    const QString tmp = path + QStringLiteral(".tmp");
    QFile::remove(tmp);

    const QString conn = uniqueConnName();
    {
        auto db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
        db.setDatabaseName(tmp);
        if (!db.open()) {
            setError(QStringLiteral("Open failed"), db.lastError());
            QSqlDatabase::removeDatabase(conn);
            return false;
        }

        QSqlQuery pragma(db);
        pragma.exec(QStringLiteral("PRAGMA foreign_keys = ON"));
        pragma.exec(QStringLiteral("PRAGMA journal_mode = WAL"));

        // Schema
        for (const auto &stmt : QString::fromUtf8(kDdl).split(';', Qt::SkipEmptyParts)) {
            const auto trimmed = stmt.trimmed();
            if (trimmed.isEmpty()) continue;
            QSqlQuery q(db);
            if (!q.exec(trimmed)) {
                setError(QStringLiteral("Schema"), q.lastError());
                db.close();
                QSqlDatabase::removeDatabase(conn);
                QFile::remove(tmp);
                return false;
            }
        }

        db.transaction();

        // Meta
        QSqlQuery mq(db);
        mq.prepare(QStringLiteral("INSERT OR REPLACE INTO meta(key, value) VALUES (?, ?)"));
        mq.addBindValue(QStringLiteral("schema_version"));
        mq.addBindValue(QString::number(kSchemaVersion));
        mq.exec();
        mq.bindValue(0, QStringLiteral("workspace_name"));
        mq.bindValue(1, workspace.name());
        mq.exec();

        // Cue lists & cues
        QSqlQuery lq(db);
        lq.prepare(QStringLiteral(
            "INSERT INTO cue_lists(id, name, ord) VALUES (?, ?, ?)"));
        QSqlQuery cq(db);
        cq.prepare(QStringLiteral(
            "INSERT INTO cues(id, list_id, parent_id, ord, type, payload) "
            "VALUES (?, ?, NULL, ?, ?, ?)"));

        int listOrd = 0;
        for (const auto &list : workspace.cueLists()) {
            lq.bindValue(0, list->id().toString());
            lq.bindValue(1, list->name());
            lq.bindValue(2, listOrd++);
            if (!lq.exec()) {
                setError(QStringLiteral("Write cue_list"), lq.lastError());
                db.rollback();
                db.close();
                QSqlDatabase::removeDatabase(conn);
                QFile::remove(tmp);
                return false;
            }

            for (int row = 0; row < list->cueCount(); ++row) {
                auto *cue = list->cueAt(row);
                cq.bindValue(0, cue->id().toString());
                cq.bindValue(1, list->id().toString());
                cq.bindValue(2, row);
                cq.bindValue(3, cue->typeKey());
                cq.bindValue(4, QString::fromUtf8(
                    QJsonDocument(cue->toPayload()).toJson(QJsonDocument::Compact)));
                if (!cq.exec()) {
                    setError(QStringLiteral("Write cue"), cq.lastError());
                    db.rollback();
                    db.close();
                    QSqlDatabase::removeDatabase(conn);
                    QFile::remove(tmp);
                    return false;
                }
            }
        }

        db.commit();
        db.close();
    }
    QSqlDatabase::removeDatabase(conn);

    // Atomic rename
    QFile::remove(path);
    if (!QFile::rename(tmp, path)) {
        setError(QStringLiteral("Rename failed"));
        QFile::remove(tmp);
        return false;
    }
    return true;
}

} // namespace quewi::show
