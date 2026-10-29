#include "storage/Transaction.h"

#include "storage/Database.h"

#include <sqlite3.h>

#include <utility>

namespace notes::storage {

Transaction::Transaction(Database* db)
    : m_db(db)
{}

Transaction::Transaction(Transaction&& other) noexcept
    : m_db(std::exchange(other.m_db, nullptr))
{}

Transaction::~Transaction()
{
    if (m_db != nullptr) {
        if (const DbStatus status = rollback(); !status) {
            qWarning("notes.storage: rollback failed: %s", qPrintable(status.error().message));
        }
    }
}

DbResult<Transaction> Transaction::begin(Database& db)
{
    if (db.inTransaction()) {
        return DbError{.code = SQLITE_ERROR,
                       .message = QStringLiteral("A transaction is already open")};
    }
    if (const DbStatus status = db.exec(QStringLiteral("BEGIN IMMEDIATE")); !status) {
        return status.error();
    }
    return Transaction(&db);
}

DbStatus Transaction::commit()
{
    if (m_db == nullptr) {
        return DbError{.code = SQLITE_MISUSE,
                       .message = QStringLiteral("Transaction already finished")};
    }
    DbStatus status = m_db->exec(QStringLiteral("COMMIT"));
    if (status) {
        m_db = nullptr;
    }
    // On failure (e.g. a deferred foreign key violation) the transaction is still
    // open; the destructor rolls it back.
    return status;
}

DbStatus Transaction::rollback()
{
    Database* db = std::exchange(m_db, nullptr);
    if (db == nullptr) {
        return DbError{.code = SQLITE_MISUSE,
                       .message = QStringLiteral("Transaction already finished")};
    }
    // SQLite may already have rolled back after some errors (e.g. SQLITE_FULL).
    if (!db->inTransaction()) {
        return {};
    }
    return db->exec(QStringLiteral("ROLLBACK"));
}

} // namespace notes::storage
