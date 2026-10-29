// Domain value types (task 2.2): defaults, equality, the sync_state values from
// Plan §7, and the UUID rules of ADR 0005.

#include "models/Attachment.h"
#include "models/Folder.h"
#include "models/Note.h"
#include "models/Tag.h"
#include "models/Uuid.h"

#include <QRegularExpression>
#include <QSet>
#include <QTest>

using namespace notes::models;

class tst_Models : public QObject
{
    Q_OBJECT

private slots:
    void defaultsMeanNotStoredYet();
    void equalityComparesEveryField();
    void syncStateMatchesSchema();
    void newUuidIsCanonicalVersion4();
    void parseUuid_data();
    void parseUuid();
};

void tst_Models::defaultsMeanNotStoredYet()
{
    const Note note;
    QCOMPARE(note.id, qint64{0});
    QCOMPARE(note.version, 1);
    QCOMPARE(note.syncState, SyncState::Dirty);
    QVERIFY(!note.isTrashed());
    QVERIFY(!note.deletedAt.has_value());
    QVERIFY(!note.pinned && !note.locked);

    const Folder folder;
    QCOMPARE(folder.id, qint64{0});
    QVERIFY(!folder.parentId.has_value());
    QCOMPARE(folder.syncState, SyncState::Dirty);

    QCOMPARE(Attachment().id, qint64{0});
    QCOMPARE(Tag().id, qint64{0});
}

void tst_Models::equalityComparesEveryField()
{
    Note a;
    a.uuid = newUuid();
    a.title = QStringLiteral("Shopping List");
    a.trashedAt = 1'800'000'000'000;
    Note b = a;
    QCOMPARE(a, b);
    b.trashedAt.reset();
    QVERIFY(a != b);
    QVERIFY(b != Note{});

    Folder f;
    f.parentId = 3;
    Folder g = f;
    QCOMPARE(f, g);
    g.parentId = 4;
    QVERIFY(f != g);

    Attachment x;
    x.data = QByteArray("\x89PNG", 4);
    Attachment y = x;
    QCOMPARE(x, y);
    y.data.append('!');
    QVERIFY(x != y);
}

// The integer values are stored in the `sync_state` column.
void tst_Models::syncStateMatchesSchema()
{
    QCOMPARE(static_cast<int>(SyncState::Dirty), 0);
    QCOMPARE(static_cast<int>(SyncState::Synced), 1);
}

void tst_Models::newUuidIsCanonicalVersion4()
{
    // 8-4-4-4-12 lowercase hex, version 4, RFC 4122 variant (8, 9, a or b).
    static const QRegularExpression kCanonical(
        QStringLiteral("^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$"));
    QSet<QString> seen;
    for (int i = 0; i < 1000; ++i) {
        const QString uuid = newUuid();
        QVERIFY2(kCanonical.match(uuid).hasMatch(), qPrintable(uuid));
        seen.insert(uuid);
    }
    QCOMPARE(seen.size(), 1000);
}

void tst_Models::parseUuid_data()
{
    QTest::addColumn<QString>("input");
    QTest::addColumn<QString>("expected"); // empty: rejected

    const QString canonical = QStringLiteral("3f2504e0-4f89-41d3-9a0c-0305e82c3301");
    QTest::newRow("canonical") << canonical << canonical;
    QTest::newRow("braces") << QStringLiteral("{3f2504e0-4f89-41d3-9a0c-0305e82c3301}")
                            << canonical;
    QTest::newRow("upper case") << QStringLiteral("3F2504E0-4F89-41D3-9A0C-0305E82C3301")
                                << canonical;
    QTest::newRow("nil") << QStringLiteral("00000000-0000-0000-0000-000000000000") << QString();
    QTest::newRow("empty") << QString() << QString();
    QTest::newRow("too short") << QStringLiteral("3f2504e0-4f89-41d3-9a0c") << QString();
    QTest::newRow("not hex") << QStringLiteral("3f2504e0-4f89-41d3-9a0c-0305e82c33zz") << QString();
    QTest::newRow("sql") << QStringLiteral("'; DROP TABLE notes; --") << QString();
}

void tst_Models::parseUuid()
{
    QFETCH(QString, input);
    QFETCH(QString, expected);
    const std::optional<QString> parsed = notes::models::parseUuid(input);
    if (expected.isEmpty()) {
        QVERIFY2(!parsed.has_value(), qPrintable(parsed.value_or(QString())));
    } else {
        QCOMPARE(parsed.value_or(QString()), expected);
    }
}

QTEST_APPLESS_MAIN(tst_Models)
#include "tst_models.moc"
