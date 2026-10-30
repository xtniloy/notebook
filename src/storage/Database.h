#pragma once

#include "storage/DbResult.h"
#include "storage/Statement.h"

#include <QHashFunctions> // std::hash<QString>
#include <QString>

#include <functional>
#include <memory>
#include <unordered_map>

class QCollator;
struct sqlite3;

namespace notes::storage {

class Database;

// A statement from Database::prepare(), reset and unbound again when this guard
// goes out of scope. That also ends its read snapshot: a statement left
// mid-result would otherwise keep the WAL from being checkpointed.
class ScopedStatement
{
public:
    ScopedStatement(ScopedStatement&& other) noexcept;
    ScopedStatement& operator=(ScopedStatement&&) = delete;
    ScopedStatement(const ScopedStatement&) = delete;
    ScopedStatement& operator=(const ScopedStatement&) = delete;
    ~ScopedStatement();

    Statement* operator->() const { return m_statement; }
    Statement& operator*() const { return *m_statement; }

private:
    friend class Database;
    ScopedStatement(Statement* cached, bool* inUse);
    explicit ScopedStatement(std::unique_ptr<Statement> uncached);

    Statement* m_statement;
    bool* m_inUse = nullptr;               // the cache slot's flag, for cached statements
    std::unique_ptr<Statement> m_uncached; // owned, for statements outside the cache
};

// One SQLite (SQLCipher) connection (ADR 0002). Not thread-safe: use it from one
// thread only, the DB thread (ADR 0003).
class Database
{
public:
    // Opens or creates the database at `path` (":memory:" for an in-memory one)
    // and configures it: WAL journal, foreign keys on, synchronous=NORMAL, and
    // the `LOCALE` collation (`ORDER BY title COLLATE LOCALE`): the user's
    // locale, case-insensitive, numbers by value ("Note 2" before "Note 10").
    // Fails unless the linked library is SQLCipher.
    [[nodiscard]] static DbResult<std::unique_ptr<Database>> open(const QString& path);

    ~Database();
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;
    Database(Database&&) = delete;
    Database& operator=(Database&&) = delete;

    // Runs SQL without parameters or results; may contain several statements
    // (schema scripts, pragmas). Never put user data in `sql`: use prepare().
    DbStatus exec(const QString& sql);

    // A prepared statement for `sql`, from the cache when possible. If the cached
    // one is still in use (nested use of the same SQL), a fresh one is prepared.
    [[nodiscard]] DbResult<ScopedStatement> prepare(const QString& sql);

    [[nodiscard]] qint64 lastInsertRowId() const;
    // Rows changed by the last INSERT, UPDATE or DELETE.
    [[nodiscard]] int changes() const;
    [[nodiscard]] bool inTransaction() const;
    [[nodiscard]] std::size_t cachedStatementCount() const { return m_cache.size(); }

    // For tests: called with the SQL text of every statement as it starts
    // running. The text has `?`/`:name` placeholders, never bound values, so no
    // note content passes through. An empty function removes the hook.
    void setTraceHook(std::function<void(const QString& sql)> hook);
    [[nodiscard]] const QString& path() const { return m_path; }

private:
    struct CacheSlot
    {
        std::unique_ptr<Statement> statement;
        bool inUse = false;
    };

    Database(sqlite3* db, QString path);
    DbStatus configure();
    [[nodiscard]] DbResult<std::unique_ptr<Statement>> prepareUncached(const QString& sql);

    sqlite3* m_db;
    QString m_path;
    // Node-based, so a slot's address (ScopedStatement::m_inUse) never moves.
    std::unordered_map<QString, CacheSlot> m_cache;
    // The LOCALE collation's comparer. Members outlive the destructor body, which
    // closes the connection, so SQLite never calls into a freed collator.
    std::unique_ptr<QCollator> m_collator;
    std::function<void(const QString&)> m_traceHook;
};

} // namespace notes::storage
