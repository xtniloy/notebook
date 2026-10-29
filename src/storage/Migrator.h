#pragma once

#include "storage/DbResult.h"

#include <QList>
#include <QString>

#include <cstdint>

namespace notes::storage {

class Database;

// Brings a database up to the newest schema (task 2.4). Schema changes are
// numbered SQL files, `NNN_short_name.sql`, starting at 001 with no gaps,
// embedded under :/migrations (src/storage/migrations/). The version reached is
// stored as `meta.schema_version`; an empty database is version 0.
//
// Each file runs in its own transaction together with the version update, so a
// failing file leaves the database exactly as it was before that file.
class Migrator
{
public:
    enum class Outcome : std::uint8_t {
        UpToDate, // nothing to do
        Migrated, // one or more files applied
        TooNew,   // written by a newer app: don't touch it (TC-MIG-004)
    };

    struct Result
    {
        Outcome outcome = Outcome::UpToDate;
        int fromVersion = 0;
        int toVersion = 0; // for TooNew: the newest version this app knows
    };

    // `directory` holds the migration files; tests point it at their own.
    explicit Migrator(QString directory = QStringLiteral(":/migrations"));

    // The highest file number, or an error if the files are misnumbered.
    [[nodiscard]] DbResult<int> latestVersion() const;

    // The database's `meta.schema_version`; 0 for an empty database.
    [[nodiscard]] static DbResult<int> currentVersion(Database& db);

    [[nodiscard]] DbResult<Result> migrate(Database& db) const;

private:
    struct Script
    {
        int version = 0;
        QString path;
    };

    [[nodiscard]] DbResult<QList<Script>> scripts() const;
    [[nodiscard]] static DbStatus apply(Database& db, const Script& script);

    QString m_directory;
};

} // namespace notes::storage
