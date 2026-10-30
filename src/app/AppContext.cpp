#include "app/AppContext.h"

#include "app/Config.h"
#include "app/IClock.h"
#include "app/SystemClock.h"
#include "services/FolderService.h"
#include "services/NoteService.h"
#include "services/TrashPurger.h"
#include "storage/DbThread.h"
#include "storage/FolderRepository.h"
#include "storage/Migrator.h"

#include <QCoreApplication>
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

QFuture<storage::DbResult<storage::Migrator::Result>> AppContext::openDatabase(const QString& path)
{
    using storage::Migrator;
    Q_ASSERT(!m_db);
    m_db = std::make_unique<storage::DbThread>(path);
    m_notes = std::make_unique<services::NoteService>(*m_db, *m_clock);
    m_folders = std::make_unique<services::FolderService>(*m_db, *m_clock);
    m_trashPurger = std::make_unique<services::TrashPurger>(*m_notes);

    const IClock* clock = m_clock.get();
    QFuture<storage::DbResult<Migrator::Result>> ready =
        m_db->post([clock](storage::Database& db) -> storage::DbResult<Migrator::Result> {
            storage::DbResult<Migrator::Result> migrated = Migrator().migrate(db);
            if (!migrated || migrated.value().outcome == Migrator::Outcome::TooNew) {
                return migrated; // written nothing (TC-MIG-004)
            }
            if (const auto notes = storage::FolderRepository(db, *clock).ensureDefaultFolder();
                !notes) {
                return notes.error();
            }
            return migrated;
        });
    services::TrashPurger* purger = m_trashPurger.get();
    ready.then(purger, [purger](const storage::DbResult<Migrator::Result>& result) {
        if (const std::optional<QString> failure = openFailureMessage(result)) {
            // Shown to the user by Application; logged without the details.
            qCCritical(lcContext) << "Database not usable:" << (result ? -1 : result.error().code);
            return;
        }
        purger->start();
    });
    return ready;
}

std::optional<QString>
AppContext::openFailureMessage(const storage::DbResult<storage::Migrator::Result>& result)
{
    if (!result) {
        return QCoreApplication::translate("AppContext", "Your notes could not be opened.\n\n%1")
            .arg(result.error().message);
    }
    if (result.value().outcome == storage::Migrator::Outcome::TooNew) {
        //: %1 is the app's name.
        return QCoreApplication::translate(
                   "AppContext",
                   "Your notes were saved by a newer version of %1, so this version can't "
                   "open them safely.\n\nInstall the latest version of %1. Your notes "
                   "have not been changed.")
            .arg(QString::fromLatin1(config::kDisplayName));
    }
    return std::nullopt;
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
