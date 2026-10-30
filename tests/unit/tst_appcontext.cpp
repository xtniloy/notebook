// TC-UI-002: QSettings uses the configured organization and application name.
// Also guards the stable identifiers that locate user data (see CMakeLists.txt).

#include "TestHelpers.h"
#include "app/AppContext.h"
#include "app/AppIdentity.h"
#include "app/Config.h"
#include "services/NoteService.h"
#include "storage/Database.h"
#include "storage/Migrator.h"

#include <QSettings>
#include <QTest>

class tst_AppContext : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void stableIdentifiersAreUnchanged();
    void settingsUseAppIdentity();
    void defaultContextUsesAppSettingsFile();
    void contextUsesInjectedSettings();
    void newerDatabaseIsRefused();
    void openFailureMessages();
};

void tst_AppContext::initTestCase()
{
    notes::app::applyAppIdentity();
}

void tst_AppContext::stableIdentifiersAreUnchanged()
{
    // These values locate every user's settings, notes database, keyring entries
    // and single-instance lock. If this fails, you changed one: renaming the app
    // only needs NOTES_DISPLAY_NAME. Changing these requires a data migration.
    QCOMPARE(notes::config::kAppId, "io.augmex.NotesApp");
    QCOMPARE(notes::config::kStorageName, "NotesApp");
    QCOMPARE(notes::config::kOrgName, "augmex");
    QCOMPARE(notes::config::kOrgDomain, "augmex.io");
}

void tst_AppContext::settingsUseAppIdentity()
{
    const QSettings settings;
    QCOMPARE(settings.organizationName(), QString::fromLatin1(notes::config::kOrgName));
    // The storage name, not the display name: renaming the app must not move data.
    QCOMPARE(settings.applicationName(), QString::fromLatin1(notes::config::kStorageName));
}

void tst_AppContext::defaultContextUsesAppSettingsFile()
{
    // Read-only: constructing QSettings doesn't write the user's settings file.
    const auto context = notes::app::AppContext::createDefault();
    const QString fileName = context->settings().fileName();
    const QString expectedSuffix = QStringLiteral("/%1/%2.conf")
                                       .arg(QString::fromLatin1(notes::config::kOrgName),
                                            QString::fromLatin1(notes::config::kStorageName));
    QVERIFY2(fileName.endsWith(expectedSuffix), qPrintable(fileName));
}

void tst_AppContext::contextUsesInjectedSettings()
{
    const notes::test::TempDb dir;
    QVERIFY(dir.isValid());
    {
        const auto context = notes::test::makeTestContext(dir.dirPath());
        context->settings().setValue(QStringLiteral("probe"), 42);
    }
    const QSettings reopened(dir.dirPath() + QStringLiteral("/settings.ini"), QSettings::IniFormat);
    QCOMPARE(reopened.value(QStringLiteral("probe")).toInt(), 42);
}

// TC-MIG-004 end to end: a database from a newer version is reported, and the
// startup job writes nothing to it (no default folder, no purge).
void tst_AppContext::newerDatabaseIsRefused()
{
    using notes::storage::Migrator;
    const notes::test::TempDb dir;
    const QString path = dir.dirPath() + QStringLiteral("/notes.db");
    const int latest = Migrator().latestVersion().value();
    {
        auto db = notes::storage::Database::open(path);
        QVERIFY(db);
        QVERIFY(
            db.value()->exec(QStringLiteral("CREATE TABLE meta (key TEXT PRIMARY KEY, value TEXT);"
                                            "INSERT INTO meta VALUES ('schema_version', '%1');")
                                 .arg(latest + 1)));
    }
    auto fresh = std::make_unique<notes::app::AppContext>(
        std::make_unique<QSettings>(dir.dirPath() + QStringLiteral("/settings.ini"),
                                    QSettings::IniFormat),
        std::make_unique<notes::test::FakeClock>());
    const auto result = notes::test::await(fresh->openDatabase(path));
    QVERIFY(result);
    QCOMPARE(result.value().outcome, Migrator::Outcome::TooNew);
    const std::optional<QString> message = notes::app::AppContext::openFailureMessage(result);
    QVERIFY2(message.value_or(QString()).contains(QStringLiteral("newer version")),
             qPrintable(message.value_or(QStringLiteral("(none)"))));
    fresh = nullptr;

    auto db = notes::storage::Database::open(path);
    QVERIFY(db);
    auto tables = db.value()->prepare(QStringLiteral("SELECT count(*) FROM sqlite_master"
                                                     " WHERE name = 'folders'"));
    QVERIFY(tables && tables.value()->step().valueOr(false));
    QCOMPARE(tables.value()->column<int>(0), 0); // nothing created
}

void tst_AppContext::openFailureMessages()
{
    using notes::storage::Migrator;
    const notes::storage::DbResult<Migrator::Result> ok(
        Migrator::Result{.outcome = Migrator::Outcome::Migrated, .fromVersion = 0, .toVersion = 1});
    QVERIFY(!notes::app::AppContext::openFailureMessage(ok));
    const notes::storage::DbResult<Migrator::Result> failed(notes::storage::DbError::make(
        notes::storage::DbError::Kind::Sqlite, QStringLiteral("disk I/O error")));
    const std::optional<QString> message = notes::app::AppContext::openFailureMessage(failed);
    QVERIFY(message && message->contains(QStringLiteral("disk I/O error")));
}

QTEST_GUILESS_MAIN(tst_AppContext)
#include "tst_appcontext.moc"
