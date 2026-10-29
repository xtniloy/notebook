// TC-BLD-005: the test harness runs, and every core dependency is linked and usable.

#include "TestHelpers.h"
#include "crypto/Sodium.h"
#include "storage/SqlCipher.h"

#include <QDir>
#include <QFileInfo>
#include <QLibraryInfo>
#include <QTest>
#include <QVersionNumber>

class tst_Smoke : public QObject
{
    Q_OBJECT

private slots:
    void qtMeetsMinimumVersion();
    void sodiumInitializes();
    void sqlcipherIsLinked();
    void tempDbIsIsolated();
};

void tst_Smoke::qtMeetsMinimumVersion()
{
    // docs/adr/0001-minimum-platform.md
    QVERIFY(QLibraryInfo::version() >= QVersionNumber(6, 8));
}

void tst_Smoke::sodiumInitializes()
{
    QVERIFY(notes::crypto::initialize());
    QVERIFY(notes::crypto::initialize()); // second call is a no-op
    QVERIFY(!notes::crypto::sodiumVersion().isEmpty());
}

void tst_Smoke::sqlcipherIsLinked()
{
    // Plain SQLite returns an empty version here.
    QVERIFY(!notes::storage::sqlcipherVersion().isEmpty());
}

void tst_Smoke::tempDbIsIsolated()
{
    const notes::test::TempDb first;
    const notes::test::TempDb second;
    QVERIFY(first.isValid());
    QVERIFY(second.isValid());
    QVERIFY(first.path() != second.path());
    QVERIFY(!QFileInfo::exists(first.path()));
    QVERIFY(!first.dirPath().startsWith(QDir::homePath() + QStringLiteral("/.local/share")));
}

QTEST_GUILESS_MAIN(tst_Smoke)
#include "tst_smoke.moc"
