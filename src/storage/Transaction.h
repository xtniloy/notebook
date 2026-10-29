#pragma once

#include "storage/DbResult.h"

namespace notes::storage {

class Database;

// RAII transaction (task 2.3): rolls back when destroyed without commit(), so an
// early `return error;` can never leave half a change behind.
//
//     DbResult<Transaction> tx = Transaction::begin(db);
//     if (!tx) return tx.error();
//     ... statements ...
//     return tx.value().commit();
//
// Transactions don't nest; begin() fails if one is already open.
class Transaction
{
public:
    // BEGIN IMMEDIATE: takes the write lock up front, so a transaction never
    // fails half-way when it upgrades from reading to writing.
    [[nodiscard]] static DbResult<Transaction> begin(Database& db);

    Transaction(Transaction&& other) noexcept;
    Transaction& operator=(Transaction&&) = delete;
    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;
    ~Transaction();

    DbStatus commit();
    DbStatus rollback();

private:
    explicit Transaction(Database* db);

    Database* m_db; // null once committed, rolled back or moved from
};

} // namespace notes::storage
