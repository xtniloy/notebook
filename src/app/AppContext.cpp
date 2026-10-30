#include "app/AppContext.h"

#include "app/IClock.h"
#include "app/SystemClock.h"
#include "services/FolderService.h"
#include "services/NoteService.h"
#include "storage/DbThread.h"
#include "storage/Migrator.h"

#include <QDir>
#include <QFile>
#include <QLoggingCategory>
#include <QSettings>
#include <QStandardPaths>

#include <utility>

namespace notes::app {

namespace {

Q_LOGGING_CATEGORY(lcContext, "notes.app.context")

} // namespace

AppContext::AppContext(std::unique_ptr<QSettings> settings, std::unique_ptr<IClock> clock)
    : m_settings(std::move(settings))
    , m_clock(std::move(clock))
{
    Q_ASSERT(m_settings);
    Q_ASSERT(m_clock);
}

AppContext::~AppContext() = default;

void AppContext::openDatabase(const QString& path)
{
    Q_ASSERT(!m_db);
    m_db = std::make_unique<storage::DbThread>(path);
    // Queued first, so every later job sees the current schema. Telling the user
    // about a failure (e.g. a database from a newer version) is task 2.11.
    m_db->post([](storage::Database& db) { return storage::Migrator().migrate(db); })
        .then([](const storage::DbResult<storage::Migrator::Result>& result) {
            if (!result) {
                qCCritical(lcContext) << "Database migration failed:" << result.error().code
                                      << result.error().message;
            } else if (result.value().outcome == storage::Migrator::Outcome::TooNew) {
                qCCritical(lcContext) << "The database was written by a newer version, schema"
                                      << result.value().fromVersion;
            }
        });
    m_notes = std::make_unique<services::NoteService>(*m_db, *m_clock);
    m_folders = std::make_unique<services::FolderService>(*m_db, *m_clock);
    (void)m_folders->ensureDefaultFolder();
}

QString AppContext::defaultDatabasePath()
{
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!QDir().mkpath(directory)) {
        qCWarning(lcContext) << "Could not create the data directory";
    }
    // Owner only: other users must not read the notes.
    QFile::setPermissions(directory, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    return QDir(directory).filePath(QStringLiteral("notes.db"));
}

std::unique_ptr<AppContext> AppContext::createDefault()
{
    return std::make_unique<AppContext>(std::make_unique<QSettings>(),
                                        std::make_unique<SystemClock>());
}

} // namespace notes::app
