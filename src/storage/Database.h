#pragma once

#include "storage/DbResult.h"
#include "storage/Statement.h"

#include <QHashFunctions> // std::hash<QString>
#include <QString>

#include <memory>
#include <unordered_map>

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
    // and configures it: WAL journal, foreign keys on, synchronous=NORMAL.
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
    [[nodiscard]] QString path() const { return m_path; }

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
};

} // namespace notes::storage
