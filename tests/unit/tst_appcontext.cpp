// TC-UI-002: QSettings uses the configured organization and application name.
// Also guards the stable identifiers that locate user data (see CMakeLists.txt).

#include "TestHelpers.h"
#include "app/AppContext.h"
#include "app/AppIdentity.h"
#include "app/Config.h"

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

QTEST_GUILESS_MAIN(tst_AppContext)
#include "tst_appcontext.moc"
