#include "storage/Migrator.h"

#include "storage/Database.h"
#include "storage/Transaction.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

#include <sqlite3.h>

#include <algorithm>
#include <utility>

namespace notes::storage {

namespace {

DbError migrationError(const QString& message)
{
    return {.code = SQLITE_ERROR, .message = message};
}

// Table rebuilds (SQLite's 12-step ALTER) need foreign keys off, and the pragma
// is ignored inside a transaction, so it is switched off around all scripts and
// checked per script with `PRAGMA foreign_key_check` instead.
class ForeignKeysOff
{
public:
    explicit ForeignKeysOff(Database& db)
        : m_db(db)
        , m_status(db.exec(QStringLiteral("PRAGMA foreign_keys = OFF")))
    {}
    ~ForeignKeysOff()
    {
        if (!m_db.exec(QStringLiteral("PRAGMA foreign_keys = ON"))) {
            qWarning("notes.storage: could not turn foreign keys back on");
        }
    }
    ForeignKeysOff(const ForeignKeysOff&) = delete;
    ForeignKeysOff& operator=(const ForeignKeysOff&) = delete;
    ForeignKeysOff(ForeignKeysOff&&) = delete;
    ForeignKeysOff& operator=(ForeignKeysOff&&) = delete;

    [[nodiscard]] const DbStatus& status() const { return m_status; }

private:
    Database& m_db;
    DbStatus m_status;
};

} // namespace

Migrator::Migrator(QString directory)
    : m_directory(std::move(directory))
{}

DbResult<QList<Migrator::Script>> Migrator::scripts() const
{
    static const QRegularExpression kName(QStringLiteral(R"(^(\d{3})_[a-z0-9_]+\.sql$)"));

    const QDir dir(m_directory);
    QList<Script> found;
    for (const QString& name : dir.entryList({QStringLiteral("*.sql")}, QDir::Files)) {
        const QRegularExpressionMatch match = kName.match(name);
        if (!match.hasMatch()) {
            return migrationError(
                QStringLiteral("Migration file %1 is not named NNN_short_name.sql").arg(name));
        }
        found.append({.version = match.captured(1).toInt(), .path = dir.filePath(name)});
    }
    if (found.isEmpty()) {
        return migrationError(QStringLiteral("No migrations found in %1").arg(m_directory));
    }

    std::ranges::sort(found, {}, &Script::version);
    for (qsizetype i = 0; i < found.size(); ++i) {
        const int expected = static_cast<int>(i) + 1;
        if (found.at(i).version != expected) {
            return migrationError(
                QStringLiteral("Migrations must be numbered 001, 002, ... without gaps or "
                               "duplicates; expected %1, found %2")
                    .arg(expected, 3, 10, QLatin1Char('0'))
                    .arg(QFileInfo(found.at(i).path).fileName()));
        }
    }
    return found;
}

DbResult<int> Migrator::latestVersion() const
{
    DbResult<QList<Script>> all = scripts();
    if (!all) {
        return all.error();
    }
    return all.value().last().version;
}

DbResult<int> Migrator::currentVersion(Database& db)
{
    {
        DbResult<ScopedStatement> hasMeta = db.prepare(
            QStringLiteral("SELECT 1 FROM sqlite_master WHERE type = 'table' AND name = 'meta'"));
        if (!hasMeta) {
            return hasMeta.error();
        }
        DbResult<bool> row = hasMeta.value()->step();
        if (!row) {
            return row.error();
        }
        if (!row.value()) {
            return 0; // empty database
        }
    }

    DbResult<ScopedStatement> select =
        db.prepare(QStringLiteral("SELECT value FROM meta WHERE key = 'schema_version'"));
    if (!select) {
        return select.error();
    }
    DbResult<bool> row = select.value()->step();
    if (!row) {
        return row.error();
    }
    if (!row.value()) {
        return 0;
    }
    bool ok = false;
    const int version = select.value()->column<QString>(0).toInt(&ok);
    if (!ok || version < 0) {
        return DbError{.code = SQLITE_CORRUPT,
                       .message = QStringLiteral("meta.schema_version is not a valid version")};
    }
    return version;
}

DbResult<Migrator::Result> Migrator::migrate(Database& db) const
{
    DbResult<QList<Script>> all = scripts();
    if (!all) {
        return all.error();
    }
    const int latest = all.value().last().version;

    DbResult<int> current = currentVersion(db);
    if (!current) {
        return current.error();
    }
    const int from = current.value();

    if (from > latest) {
        // Written by a newer app. Changing it could lose data that version keeps,
        // so leave it alone and let the caller explain (TC-MIG-004).
        return Result{.outcome = Outcome::TooNew, .fromVersion = from, .toVersion = latest};
    }
    if (from == latest) {
        return Result{.outcome = Outcome::UpToDate, .fromVersion = from, .toVersion = latest};
    }

    const ForeignKeysOff foreignKeysOff(db);
    if (!foreignKeysOff.status()) {
        return foreignKeysOff.status().error();
    }
    for (const Script& script : all.value()) {
        if (script.version <= from) {
            continue;
        }
        if (const DbStatus status = apply(db, script); !status) {
            DbError error = status.error();
            error.message = QStringLiteral("Migration %1 failed: %2")
                                .arg(QFileInfo(script.path).fileName(), error.message);
            return error;
        }
    }
    return Result{.outcome = Outcome::Migrated, .fromVersion = from, .toVersion = latest};
}

DbStatus Migrator::apply(Database& db, const Script& script)
{
    QFile file(script.path);
    if (!file.open(QIODevice::ReadOnly)) {
        return DbError{.code = SQLITE_CANTOPEN, .message = file.errorString()};
    }
    const QString sql = QString::fromUtf8(file.readAll());

    DbResult<Transaction> tx = Transaction::begin(db);
    if (!tx) {
        return tx.error();
    }
    if (const DbStatus status = db.exec(sql); !status) {
        return status; // the transaction rolls back
    }
    {
        DbResult<ScopedStatement> setVersion =
            db.prepare(QStringLiteral("INSERT INTO meta (key, value) VALUES ('schema_version', ?)"
                                      " ON CONFLICT (key) DO UPDATE SET value = excluded.value"));
        if (!setVersion) {
            return setVersion.error();
        }
        if (const DbStatus status =
                setVersion.value()->bind(1, QString::number(script.version)).run();
            !status) {
            return status;
        }
    }
    {
        // Foreign keys are off while migrating (see ForeignKeysOff); make sure the
        // script left none dangling.
        DbResult<ScopedStatement> check = db.prepare(QStringLiteral("PRAGMA foreign_key_check"));
        if (!check) {
            return check.error();
        }
        DbResult<bool> violation = check.value()->step();
        if (!violation) {
            return violation.error();
        }
        if (violation.value()) {
            return DbError{.code = SQLITE_CONSTRAINT_FOREIGNKEY,
                           .message = QStringLiteral("foreign key violation in table %1")
                                          .arg(check.value()->column<QString>(0))};
        }
    }
    return tx.value().commit();
}

} // namespace notes::storage
