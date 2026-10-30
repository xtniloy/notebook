#pragma once

#include <QString>

#include <memory>

class QSettings;

namespace notes::services {
class FolderService;
class NoteService;
} // namespace notes::services

namespace notes::storage {
class DbThread;
}

namespace notes::app {

class IClock;

// Owns the app's long-lived services and hands them to the UI by reference
// (dependency injection: no globals or singletons). Services are added as they
// are built: NoteService/FolderService in task 2.7, SearchService in 5.3, ...
//
// The database is opened separately (openDatabase()), so a context can be made
// without touching the user's data, e.g. in tests that only need settings.
class AppContext
{
public:
    AppContext(std::unique_ptr<QSettings> settings, std::unique_ptr<IClock> clock);
    ~AppContext();

    AppContext(const AppContext&) = delete;
    AppContext& operator=(const AppContext&) = delete;
    AppContext(AppContext&&) = delete;
    AppContext& operator=(AppContext&&) = delete;

    // Uses the user's settings file and the system clock. applyAppIdentity()
    // must have run first.
    static std::unique_ptr<AppContext> createDefault();

    [[nodiscard]] QSettings& settings() { return *m_settings; }
    // The current time for all app code (ADR 0004).
    [[nodiscard]] const IClock& clock() const { return *m_clock; }

    // Starts the DB thread on `path` (created if missing), queues the schema
    // migration and the default "Notes" folder, and creates the services. Jobs
    // run in order, so anything posted afterwards sees a migrated database.
    // Call once.
    void openDatabase(const QString& path);
    // The user's database: notes.db in the app's data directory (made 0700).
    [[nodiscard]] static QString defaultDatabasePath();
    [[nodiscard]] bool hasDatabase() const { return m_db != nullptr; }

    // After openDatabase().
    [[nodiscard]] services::NoteService& notes() { return *m_notes; }
    [[nodiscard]] services::FolderService& folders() { return *m_folders; }

private:
    std::unique_ptr<QSettings> m_settings;
    std::unique_ptr<IClock> m_clock;
    // Destroyed in reverse: services first, then the thread (which drains its queue).
    std::unique_ptr<storage::DbThread> m_db;
    std::unique_ptr<services::NoteService> m_notes;
    std::unique_ptr<services::FolderService> m_folders;
};

} // namespace notes::app
