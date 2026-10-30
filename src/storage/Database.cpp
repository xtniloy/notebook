#include "storage/Database.h"

#include "storage/SqliteError.h"

#include <QCollator>
#include <QFile>

#include <sqlite3.h>

#include <utility>

namespace notes::storage {

// --- ScopedStatement ---

ScopedStatement::ScopedStatement(Statement* cached, bool* inUse)
    : m_statement(cached)
    , m_inUse(inUse)
{
    *m_inUse = true;
}

ScopedStatement::ScopedStatement(std::unique_ptr<Statement> uncached)
    : m_statement(uncached.get())
    , m_uncached(std::move(uncached))
{}

ScopedStatement::ScopedStatement(ScopedStatement&& other) noexcept
    : m_statement(std::exchange(other.m_statement, nullptr))
    , m_inUse(std::exchange(other.m_inUse, nullptr))
    , m_uncached(std::move(other.m_uncached))
{}

ScopedStatement::~ScopedStatement()
{
    if (m_statement == nullptr) {
        return; // moved from
    }
    m_statement->reset();
    if (m_inUse != nullptr) {
        *m_inUse = false;
    }
}

// --- Database ---

namespace {

int compareLocale(void* collator, int lengthA, const void* a, int lengthB, const void* b)
{
    return static_cast<const QCollator*>(collator)->compare(
        QString::fromUtf8(static_cast<const char*>(a), lengthA),
        QString::fromUtf8(static_cast<const char*>(b), lengthB));
}

int traceStatement(unsigned type, void* context, void* /*statement*/, void* sql)
{
    if (type == SQLITE_TRACE_STMT) {
        const auto& hook = *static_cast<const std::function<void(const QString&)>*>(context);
        hook(QString::fromUtf8(static_cast<const char*>(sql)));
    }
    return 0;
}

} // namespace

Database::Database(sqlite3* db, QString path)
    : m_db(db)
    , m_path(std::move(path))
{}

Database::~Database()
{
    // Statements must be finalized before the connection closes.
    m_cache.clear();
    if (sqlite3_close(m_db) != SQLITE_OK) {
        qWarning("notes.storage: closing the database failed: %s", sqlite3_errmsg(m_db));
    }
}

DbResult<std::unique_ptr<Database>> Database::open(const QString& path)
{
    sqlite3* rawDb = nullptr;
    const QByteArray fileName = path == u":memory:" ? path.toUtf8() : QFile::encodeName(path);
    const int rc = sqlite3_open_v2(fileName.constData(), &rawDb,
                                   SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
    // sqlite3_open_v2() may allocate a handle even on failure; the Database owns
    // it from here, so it is always closed.
    std::unique_ptr<Database> db(new Database(rawDb, path));
    if (rc != SQLITE_OK) {
        return sqliteError(rawDb, rc);
    }
    sqlite3_extended_result_codes(rawDb, 1);

    if (const DbStatus status = db->configure(); !status) {
        return status.error();
    }
    return db;
}

DbStatus Database::configure()
{
    // ADR 0002: the app must never run on plain SQLite, which would silently
    // ignore encryption later.
    {
        DbResult<std::unique_ptr<Statement>> cipher =
            prepareUncached(QStringLiteral("PRAGMA cipher_version"));
        if (!cipher) {
            return cipher.error();
        }
        DbResult<bool> row = cipher.value()->step();
        if (!row || !row.value() || cipher.value()->column<QString>(0).isEmpty()) {
            return DbError{.code = SQLITE_ERROR,
                           .message = QStringLiteral("Not linked against SQLCipher")};
        }
    }
    // Title sorting (task 2.6). SQLite's NOCASE only folds ASCII: "Élan" would
    // sort after "Zebra".
    m_collator = std::make_unique<QCollator>(QLocale());
    m_collator->setCaseSensitivity(Qt::CaseInsensitive);
    m_collator->setNumericMode(true);
    if (const int rc = sqlite3_create_collation_v2(m_db, "LOCALE", SQLITE_UTF8, m_collator.get(),
                                                   &compareLocale, nullptr);
        rc != SQLITE_OK) {
        return sqliteError(m_db, rc);
    }

    // An in-memory database reports "memory" and stays that way; fine for tests.
    return exec(QStringLiteral("PRAGMA journal_mode = WAL;"
                               "PRAGMA foreign_keys = ON;"
                               "PRAGMA synchronous = NORMAL;"));
}

DbStatus Database::exec(const QString& sql)
{
    char* message = nullptr;
    const int rc = sqlite3_exec(m_db, sql.toUtf8().constData(), nullptr, nullptr, &message);
    if (rc == SQLITE_OK) {
        return {};
    }
    DbError error{.code = rc,
                  .message = message != nullptr ? QString::fromUtf8(message)
                                                : QString::fromUtf8(sqlite3_errstr(rc))};
    sqlite3_free(message);
    return error;
}

DbResult<std::unique_ptr<Statement>> Database::prepareUncached(const QString& sql)
{
    const QByteArray utf8 = sql.toUtf8();
    sqlite3_stmt* stmt = nullptr;
    const int rc = sqlite3_prepare_v3(m_db, utf8.constData(), static_cast<int>(utf8.size()),
                                      SQLITE_PREPARE_PERSISTENT, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        sqlite3_finalize(stmt);
        return sqliteError(m_db, rc);
    }
    if (stmt == nullptr) {
        // Only whitespace or comments.
        return DbError{.code = SQLITE_MISUSE, .message = QStringLiteral("Empty SQL statement")};
    }
    return std::make_unique<Statement>(m_db, stmt);
}

DbResult<ScopedStatement> Database::prepare(const QString& sql)
{
    const auto found = m_cache.find(sql);
    if (found != m_cache.end()) {
        if (!found->second.inUse) {
            return ScopedStatement(found->second.statement.get(), &found->second.inUse);
        }
        // The cached one is busy (nested use of the same SQL): use a throwaway.
        DbResult<std::unique_ptr<Statement>> fresh = prepareUncached(sql);
        if (!fresh) {
            return fresh.error();
        }
        return ScopedStatement(std::move(fresh).value());
    }

    DbResult<std::unique_ptr<Statement>> prepared = prepareUncached(sql);
    if (!prepared) {
        return prepared.error();
    }
    CacheSlot& slot = m_cache[sql];
    slot.statement = std::move(prepared).value();
    return ScopedStatement(slot.statement.get(), &slot.inUse);
}

void Database::setTraceHook(std::function<void(const QString& sql)> hook)
{
    m_traceHook = std::move(hook);
    if (m_traceHook) {
        sqlite3_trace_v2(m_db, SQLITE_TRACE_STMT, &traceStatement, &m_traceHook);
    } else {
        sqlite3_trace_v2(m_db, 0, nullptr, nullptr);
    }
}

qint64 Database::lastInsertRowId() const
{
    return sqlite3_last_insert_rowid(m_db);
}

int Database::changes() const
{
    return sqlite3_changes(m_db);
}

bool Database::inTransaction() const
{
    return sqlite3_get_autocommit(m_db) == 0;
}

} // namespace notes::storage
