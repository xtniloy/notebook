// Migrator (task 2.4): TC-MIG-001..005, plus misnumbered files, foreign key
// checks and a corrupt version value.

#include "TestHelpers.h"
#include "storage/Database.h"
#include "storage/Migrator.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QTemporaryDir>
#include <QTest>

#include <memory>
#include <utility>
#include <vector>

using namespace notes::storage;
using Outcome = Migrator::Outcome;

namespace {

constexpr int kSqliteCorrupt = 11;
constexpr int kSqliteConstraintForeignKey = 787;
constexpr char kInitialScript[] = ":/migrations/001_initial.sql";

QStringList column(Database& db, const QString& sql)
{
    QStringList values;
    DbResult<ScopedStatement> stmt = db.prepare(sql);
    if (!stmt) {
        return {QStringLiteral("ERROR: ") + stmt.error().message};
    }
    while (stmt.value()->step().valueOr(false)) {
        values.append(stmt.value()->column<QString>(0));
    }
    return values;
}

QString scalar(Database& db, const QString& sql)
{
    return column(db, sql).value(0);
}

// The schema as SQLite stores it, to compare before/after.
QStringList schema(Database& db)
{
    return column(
        db,
        QStringLiteral("SELECT type || ' ' || name || ': ' || coalesce(sql, '') FROM sqlite_master"
                       " ORDER BY type, name"));
}

} // namespace

class tst_Migrator : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void embeddedMigrationsAreNumbered();
    void emptyDatabaseReachesLatest();
    void dataSurvivesLaterMigrations();
    void failingMigrationRollsBack();
    void newerDatabaseIsLeftAlone();
    void secondRunIsNoOp();
    void misnumberedMigrationsAreRejected_data();
    void misnumberedMigrationsAreRejected();
    void danglingForeignKeysRollBack();
    void corruptVersionIsAnError();

private:
    // A migration folder with the real 001 plus `extra` files (name → SQL).
    QString migrationDir(const QList<std::pair<QString, QString>>& extra = {});
    std::unique_ptr<Database> openDb();

    std::unique_ptr<notes::test::TempDb> m_dir;
    std::vector<std::unique_ptr<QTemporaryDir>> m_scriptDirs;
    std::unique_ptr<Database> m_db;
};

void tst_Migrator::init()
{
    m_dir = std::make_unique<notes::test::TempDb>();
    QVERIFY(m_dir->isValid());
    m_db = openDb();
    QVERIFY(m_db);
}

void tst_Migrator::cleanup()
{
    m_db = nullptr;
    m_scriptDirs.clear();
    m_dir = nullptr;
}

std::unique_ptr<Database> tst_Migrator::openDb()
{
    DbResult<std::unique_ptr<Database>> db = Database::open(m_dir->path());
    return db ? std::move(db).value() : nullptr;
}

QString tst_Migrator::migrationDir(const QList<std::pair<QString, QString>>& extra)
{
    auto& dir = m_scriptDirs.emplace_back(std::make_unique<QTemporaryDir>());
    const QString initial = dir->filePath(QStringLiteral("001_initial.sql"));
    if (!QFile::copy(QString::fromLatin1(kInitialScript), initial)) {
        qWarning("could not copy the embedded 001_initial.sql");
    }
    for (const auto& [name, sql] : extra) {
        QFile file(dir->filePath(name));
        if (!file.open(QIODevice::WriteOnly) || file.write(sql.toUtf8()) < 0) {
            qWarning("could not write %s", qPrintable(name));
        }
    }
    return dir->path();
}

void tst_Migrator::embeddedMigrationsAreNumbered()
{
    QVERIFY(QFile::exists(QString::fromLatin1(kInitialScript)));
    const DbResult<int> latest = Migrator().latestVersion();
    QVERIFY2(latest, qPrintable(latest.error().message));
    QVERIFY(latest.value() >= 1);
}

// TC-MIG-001
void tst_Migrator::emptyDatabaseReachesLatest()
{
    const Migrator migrator;
    const int latest = migrator.latestVersion().value();
    QCOMPARE(Migrator::currentVersion(*m_db).value(), 0);

    const DbResult<Migrator::Result> result = migrator.migrate(*m_db);
    QVERIFY2(result, qPrintable(result.error().message));
    QCOMPARE(result.value().outcome, Outcome::Migrated);
    QCOMPARE(result.value().fromVersion, 0);
    QCOMPARE(result.value().toVersion, latest);
    QCOMPARE(Migrator::currentVersion(*m_db).value(), latest);

    // Every table and index of Plan §7 (notes_fts comes in task 5.1).
    const QStringList tables =
        column(*m_db, QStringLiteral("SELECT name FROM sqlite_master WHERE type = 'table'"));
    for (const char* table : {"folders", "notes", "attachments", "tags", "note_tags", "meta"}) {
        QVERIFY2(tables.contains(QLatin1StringView(table)), table);
    }
    const QStringList indexes =
        column(*m_db, QStringLiteral("SELECT name FROM sqlite_master WHERE type = 'index'"));
    for (const char* index : {"idx_notes_folder", "idx_notes_pinned", "idx_notes_sync"}) {
        QVERIFY2(indexes.contains(QLatin1StringView(index)), index);
    }

    const QSet<QString> noteColumns = [&] {
        const QStringList list =
            column(*m_db, QStringLiteral("SELECT name FROM pragma_table_info('notes')"));
        return QSet<QString>(list.begin(), list.end());
    }();
    for (const char* name :
         {"id", "uuid", "folder_id", "title", "snippet", "body_html", "body_text", "is_pinned",
          "is_locked", "locked_blob", "locked_nonce", "wrapped_key", "created_at", "updated_at",
          "trashed_at", "deleted_at", "version", "sync_state"}) {
        QVERIFY2(noteColumns.contains(QLatin1StringView(name)), name);
    }

    // Foreign keys are back on after migrating.
    QCOMPARE(scalar(*m_db, QStringLiteral("PRAGMA foreign_keys")), QStringLiteral("1"));
}

// TC-MIG-002
void tst_Migrator::dataSurvivesLaterMigrations()
{
    QVERIFY(Migrator(migrationDir()).migrate(*m_db));
    QVERIFY(m_db->exec(QStringLiteral(
        "INSERT INTO folders (id, uuid, name, created_at, updated_at)"
        "  VALUES (1, '3f2504e0-4f89-41d3-9a0c-0305e82c3301', 'Work', 1, 1);"
        "INSERT INTO notes (uuid, folder_id, title, body_text, created_at, updated_at)"
        "  VALUES ('5b1c4e2a-0d7e-4c55-9a1e-8f3e2d1c0b9a', 1, 'Plan', 'SECRET-MARKER-7f3a', 2, "
        "3);")));
    QCOMPARE(Migrator::currentVersion(*m_db).value(), 1);

    const Migrator v2(migrationDir(
        {{QStringLiteral("002_note_color.sql"),
          QStringLiteral("ALTER TABLE notes ADD COLUMN color TEXT;"
                         "CREATE TABLE smart_folders (id INTEGER PRIMARY KEY, name TEXT);")}}));
    const DbResult<Migrator::Result> result = v2.migrate(*m_db);
    QVERIFY2(result, qPrintable(result.error().message));
    QCOMPARE(result.value().outcome, Outcome::Migrated);
    QCOMPARE(result.value().fromVersion, 1);
    QCOMPARE(result.value().toVersion, 2);

    QCOMPARE(scalar(*m_db, QStringLiteral(
                               "SELECT title || '|' || body_text || '|' || folder_id FROM notes")),
             QStringLiteral("Plan|SECRET-MARKER-7f3a|1"));
    QCOMPARE(scalar(*m_db, QStringLiteral("SELECT name FROM folders")), QStringLiteral("Work"));
    QCOMPARE(scalar(*m_db, QStringLiteral("SELECT typeof(color) FROM notes")),
             QStringLiteral("null"));
    QCOMPARE(Migrator::currentVersion(*m_db).value(), 2);
}

// TC-MIG-003
void tst_Migrator::failingMigrationRollsBack()
{
    QVERIFY(Migrator(migrationDir()).migrate(*m_db));
    QVERIFY(m_db->exec(QStringLiteral(
        "INSERT INTO folders (uuid, name, created_at, updated_at) VALUES ('u', 'Work', 1, 1)")));
    const QStringList before = schema(*m_db);

    const Migrator broken(migrationDir({{QStringLiteral("002_broken.sql"),
                                         QStringLiteral("ALTER TABLE notes ADD COLUMN color TEXT;"
                                                        "CREATE TABLE half_done (x INTEGER);"
                                                        "SELEC oops;")}}));
    const DbResult<Migrator::Result> result = broken.migrate(*m_db);
    QVERIFY(!result);
    QVERIFY2(result.error().message.contains(QStringLiteral("002_broken.sql")),
             qPrintable(result.error().message));
    QVERIFY2(result.error().message.contains(QStringLiteral("syntax error")),
             qPrintable(result.error().message));

    QCOMPARE(Migrator::currentVersion(*m_db).value(), 1);
    QCOMPARE(schema(*m_db), before); // no color column, no half_done table
    QCOMPARE(scalar(*m_db, QStringLiteral("SELECT name FROM folders")), QStringLiteral("Work"));
    QVERIFY(!m_db->inTransaction());
    QCOMPARE(scalar(*m_db, QStringLiteral("PRAGMA foreign_keys")), QStringLiteral("1"));
}

// TC-MIG-004
void tst_Migrator::newerDatabaseIsLeftAlone()
{
    const Migrator migrator;
    QVERIFY(migrator.migrate(*m_db));
    const int latest = migrator.latestVersion().value();
    QVERIFY(m_db->exec(QStringLiteral("UPDATE meta SET value = '%1' WHERE key = 'schema_version'")
                           .arg(latest + 1)));
    QVERIFY(m_db->exec(QStringLiteral("PRAGMA wal_checkpoint(TRUNCATE)")));

    const QStringList schemaBefore = schema(*m_db);
    const qint64 dbSize = QFileInfo(m_dir->path()).size();
    const qint64 walSize = QFileInfo(m_dir->path() + QStringLiteral("-wal")).size();

    const DbResult<Migrator::Result> result = migrator.migrate(*m_db);
    QVERIFY2(result, qPrintable(result.error().message));
    QCOMPARE(result.value().outcome, Outcome::TooNew);
    QCOMPARE(result.value().fromVersion, latest + 1);
    QCOMPARE(result.value().toVersion, latest);

    // Nothing written: same schema, same version, files untouched.
    QCOMPARE(schema(*m_db), schemaBefore);
    QCOMPARE(Migrator::currentVersion(*m_db).value(), latest + 1);
    QCOMPARE(QFileInfo(m_dir->path()).size(), dbSize);
    QCOMPARE(QFileInfo(m_dir->path() + QStringLiteral("-wal")).size(), walSize);
}

// TC-MIG-005
void tst_Migrator::secondRunIsNoOp()
{
    const Migrator migrator;
    QCOMPARE(migrator.migrate(*m_db).value().outcome, Outcome::Migrated);
    const QStringList schemaAfterFirst = schema(*m_db);

    const DbResult<Migrator::Result> second = migrator.migrate(*m_db);
    QVERIFY(second);
    QCOMPARE(second.value().outcome, Outcome::UpToDate);
    QCOMPARE(second.value().fromVersion, second.value().toVersion);
    QCOMPARE(schema(*m_db), schemaAfterFirst);

    // Also after reopening the file.
    m_db = openDb();
    QCOMPARE(migrator.migrate(*m_db).value().outcome, Outcome::UpToDate);
}

void tst_Migrator::misnumberedMigrationsAreRejected_data()
{
    QTest::addColumn<QStringList>("files"); // added next to 001_initial.sql

    QTest::newRow("gap") << QStringList{QStringLiteral("003_skipped_two.sql")};
    QTest::newRow("duplicate number") << QStringList{QStringLiteral("001_again.sql")};
    QTest::newRow("no padding") << QStringList{QStringLiteral("2_short.sql")};
    QTest::newRow("dash") << QStringList{QStringLiteral("002-dash.sql")};
    QTest::newRow("upper case") << QStringList{QStringLiteral("002_Upper.sql")};
}

void tst_Migrator::misnumberedMigrationsAreRejected()
{
    QFETCH(QStringList, files);
    QList<std::pair<QString, QString>> extra;
    for (const QString& name : std::as_const(files)) {
        extra.append({name, QStringLiteral("CREATE TABLE should_not_exist (x);")});
    }
    const Migrator migrator(migrationDir(extra));
    QVERIFY(!migrator.latestVersion());

    const DbResult<Migrator::Result> result = migrator.migrate(*m_db);
    QVERIFY(!result);
    QCOMPARE(Migrator::currentVersion(*m_db).value(), 0); // nothing ran, not even 001
    QVERIFY(schema(*m_db).isEmpty());
}

void tst_Migrator::danglingForeignKeysRollBack()
{
    QVERIFY(Migrator(migrationDir()).migrate(*m_db));
    const Migrator dangling(
        migrationDir({{QStringLiteral("002_orphan.sql"),
                       QStringLiteral("INSERT INTO notes (uuid, folder_id, created_at, updated_at)"
                                      " VALUES ('orphan', 999, 1, 1);")}}));
    const DbResult<Migrator::Result> result = dangling.migrate(*m_db);
    QVERIFY(!result);
    QCOMPARE(result.error().code, kSqliteConstraintForeignKey);
    QVERIFY2(result.error().message.contains(QStringLiteral("notes")),
             qPrintable(result.error().message));
    QCOMPARE(Migrator::currentVersion(*m_db).value(), 1);
    QCOMPARE(scalar(*m_db, QStringLiteral("SELECT count(*) FROM notes")), QStringLiteral("0"));
    QCOMPARE(scalar(*m_db, QStringLiteral("PRAGMA foreign_keys")), QStringLiteral("1"));
}

void tst_Migrator::corruptVersionIsAnError()
{
    QVERIFY(Migrator().migrate(*m_db));
    QVERIFY(
        m_db->exec(QStringLiteral("UPDATE meta SET value = 'abc' WHERE key = 'schema_version'")));
    const DbResult<Migrator::Result> result = Migrator().migrate(*m_db);
    QVERIFY(!result);
    QCOMPARE(result.error().code, kSqliteCorrupt);
}

QTEST_GUILESS_MAIN(tst_Migrator)
#include "tst_migrator.moc"
