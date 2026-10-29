// Database wrapper (task 2.3): TC-STO-002 connection settings, TC-STO-003
// rollback without commit, TC-STO-004 no SQL injection, TC-STO-005 errors as
// values. Plus typed bind/column, the statement cache and its scope guard.

#include "TestHelpers.h"
#include "models/SyncState.h"
#include "storage/Database.h"
#include "storage/Transaction.h"

#include <QTest>

#include <limits>
#include <memory>

using namespace notes::storage;

namespace {

// SQLite result codes (sqlite3.h is private to the storage library).
constexpr int kSqliteError = 1;
constexpr int kSqliteCantOpen = 14;
constexpr int kSqliteConstraint = 19;
constexpr int kSqliteMisuse = 21;
constexpr int kSqliteRange = 25;
constexpr int kSqliteConstraintForeignKey = 787;
constexpr int kSqliteConstraintUnique = 2067;

QString scalarText(Database& db, const QString& sql)
{
    DbResult<ScopedStatement> stmt = db.prepare(sql);
    if (!stmt || !stmt.value()->step().valueOr(false)) {
        return {};
    }
    return stmt.value()->column<QString>(0);
}

} // namespace

class tst_Database : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void openConfiguresConnection();
    void openFailsForMissingDirectory();
    void transactionWithoutCommitRollsBack();
    void committedTransactionPersists();
    void transactionMisuse();
    void bindingStoresTextLiterally_data();
    void bindingStoresTextLiterally();
    void invalidSqlReturnsError();
    void typedValuesRoundTrip();
    void emptyValuesAreNotNull();
    void namedParameters();
    void badParameterFailsOnStep();
    void constraintErrorsHaveExtendedCodes();
    void statementCacheReusesStatements();
    void sameSqlCanBeNested();
    void scopedStatementResetsOnExit();
    void rowIdAndChanges();

private:
    std::unique_ptr<Database> open();

    std::unique_ptr<notes::test::TempDb> m_dir;
    std::unique_ptr<Database> m_db;
};

void tst_Database::init()
{
    m_dir = std::make_unique<notes::test::TempDb>();
    QVERIFY(m_dir->isValid());
    m_db = open();
    QVERIFY(m_db);
    QVERIFY(m_db->exec(QStringLiteral(
        "CREATE TABLE folders (id INTEGER PRIMARY KEY, name TEXT NOT NULL UNIQUE);"
        "CREATE TABLE notes (id INTEGER PRIMARY KEY,"
        "  folder_id INTEGER REFERENCES folders(id), title TEXT NOT NULL,"
        "  body BLOB, count INTEGER, ratio REAL, pinned INTEGER, trashed_at INTEGER);")));
}

void tst_Database::cleanup()
{
    m_db = nullptr;
    m_dir = nullptr;
}

std::unique_ptr<Database> tst_Database::open()
{
    DbResult<std::unique_ptr<Database>> db = Database::open(m_dir->path());
    if (!db) {
        qWarning() << "open failed:" << db.error().message;
        return nullptr;
    }
    return std::move(db).value();
}

// TC-STO-002
void tst_Database::openConfiguresConnection()
{
    QCOMPARE(scalarText(*m_db, QStringLiteral("PRAGMA journal_mode")), QStringLiteral("wal"));
    QCOMPARE(scalarText(*m_db, QStringLiteral("PRAGMA foreign_keys")), QStringLiteral("1"));
    QCOMPARE(scalarText(*m_db, QStringLiteral("PRAGMA synchronous")),
             QStringLiteral("1")); // NORMAL
    QVERIFY(!scalarText(*m_db, QStringLiteral("PRAGMA cipher_version")).isEmpty());
    QCOMPARE(m_db->path(), m_dir->path());
}

void tst_Database::openFailsForMissingDirectory()
{
    const DbResult<std::unique_ptr<Database>> db =
        Database::open(m_dir->dirPath() + QStringLiteral("/missing/notes.db"));
    QVERIFY(!db);
    QCOMPARE(db.error().primaryCode(), kSqliteCantOpen);
    QVERIFY(!db.error().message.isEmpty());
}

// TC-STO-003
void tst_Database::transactionWithoutCommitRollsBack()
{
    {
        DbResult<Transaction> tx = Transaction::begin(*m_db);
        QVERIFY(tx);
        QVERIFY(m_db->inTransaction());
        QVERIFY(m_db->exec(QStringLiteral("INSERT INTO folders (name) VALUES ('Work')")));
        QCOMPARE(scalarText(*m_db, QStringLiteral("SELECT count(*) FROM folders")),
                 QStringLiteral("1"));
    } // destroyed without commit()
    QVERIFY(!m_db->inTransaction());
    QCOMPARE(scalarText(*m_db, QStringLiteral("SELECT count(*) FROM folders")),
             QStringLiteral("0"));
}

void tst_Database::committedTransactionPersists()
{
    {
        DbResult<Transaction> tx = Transaction::begin(*m_db);
        QVERIFY(tx);
        QVERIFY(m_db->exec(QStringLiteral("INSERT INTO folders (name) VALUES ('Work')")));
        QVERIFY(tx.value().commit());
    }
    m_db = open(); // reopen from disk
    QVERIFY(m_db);
    QCOMPARE(scalarText(*m_db, QStringLiteral("SELECT name FROM folders")), QStringLiteral("Work"));
}

void tst_Database::transactionMisuse()
{
    DbResult<Transaction> tx = Transaction::begin(*m_db);
    QVERIFY(tx);
    // Transactions don't nest.
    const DbResult<Transaction> nested = Transaction::begin(*m_db);
    QVERIFY(!nested);
    QCOMPARE(nested.error().code, kSqliteError);

    QVERIFY(tx.value().commit());
    const DbStatus again = tx.value().commit();
    QVERIFY(!again);
    QCOMPARE(again.error().code, kSqliteMisuse);

    DbResult<Transaction> second = Transaction::begin(*m_db);
    QVERIFY(second);
    QVERIFY(second.value().rollback());
    QVERIFY(!m_db->inTransaction());
}

// TC-STO-004
void tst_Database::bindingStoresTextLiterally_data()
{
    QTest::addColumn<QString>("text");
    QTest::newRow("quotes") << QStringLiteral("O'Brien said \"hi\"");
    QTest::newRow("injection") << QStringLiteral("'; DROP TABLE notes; --");
    QTest::newRow("injection 2") << QStringLiteral("x'); DELETE FROM folders; SELECT ('");
    QTest::newRow("unicode") << QStringLiteral("Ünïcödé 日本語 🗒️ ✓");
    QTest::newRow("control chars") << QStringLiteral("line 1\nline 2\ttab\r\n");
    QTest::newRow("embedded NUL") << QString(QStringLiteral("before") + QChar(0) +
                                             QStringLiteral("after"));
    QTest::newRow("marker") << QString::fromLatin1(notes::test::kSecretMarker);
    QTest::newRow("empty") << QString(QLatin1StringView(""));
}

void tst_Database::bindingStoresTextLiterally()
{
    QFETCH(QString, text);
    QVERIFY(m_db->exec(QStringLiteral("INSERT INTO folders (name) VALUES ('Work')")));
    {
        DbResult<ScopedStatement> insert =
            m_db->prepare(QStringLiteral("INSERT INTO notes (title) VALUES (?)"));
        QVERIFY(insert);
        QVERIFY(insert.value()->bind(1, text).run());
    }
    DbResult<ScopedStatement> select = m_db->prepare(QStringLiteral("SELECT title FROM notes"));
    QVERIFY(select);
    QVERIFY(select.value()->step().valueOr(false));
    QCOMPARE(select.value()->column<QString>(0), text);
    QVERIFY(!select.value()->step().valueOr(false)); // exactly one row

    // Nothing was executed: both tables and their rows are still there.
    QCOMPARE(scalarText(*m_db, QStringLiteral("SELECT count(*) FROM folders")),
             QStringLiteral("1"));
    QCOMPARE(scalarText(*m_db, QStringLiteral("SELECT count(*) FROM notes")), QStringLiteral("1"));
}

// TC-STO-005
void tst_Database::invalidSqlReturnsError()
{
    const DbStatus syntax = m_db->exec(QStringLiteral("SELEC 1"));
    QVERIFY(!syntax);
    QCOMPARE(syntax.error().code, kSqliteError);
    QVERIFY2(syntax.error().message.contains(QStringLiteral("syntax error")),
             qPrintable(syntax.error().message));

    const DbResult<ScopedStatement> missing =
        m_db->prepare(QStringLiteral("SELECT * FROM no_such_table"));
    QVERIFY(!missing);
    QVERIFY2(missing.error().message.contains(QStringLiteral("no such table")),
             qPrintable(missing.error().message));

    const DbResult<ScopedStatement> empty = m_db->prepare(QStringLiteral("  -- nothing"));
    QVERIFY(!empty);

    // The connection is still usable, and failed SQL isn't cached.
    QCOMPARE(scalarText(*m_db, QStringLiteral("SELECT 40 + 2")), QStringLiteral("42"));
    QCOMPARE(m_db->cachedStatementCount(), std::size_t{1});
}

void tst_Database::typedValuesRoundTrip()
{
    const QByteArray binary("\x00\x01\xff\x00png", 7);
    {
        DbResult<ScopedStatement> insert = m_db->prepare(
            QStringLiteral("INSERT INTO notes (title, body, count, ratio, pinned, trashed_at)"
                           " VALUES (?, ?, ?, ?, ?, ?)"));
        QVERIFY(insert);
        QVERIFY(insert.value()
                    ->bind(1, QStringLiteral("max"))
                    .bind(2, binary)
                    .bind(3, std::numeric_limits<qint64>::max())
                    .bind(4, 0.25)
                    .bind(5, true)
                    .bind(6, std::optional<qint64>{1'800'000'000'000})
                    .run());
        QVERIFY(insert.value()
                    ->bind(1, QStringLiteral("min"))
                    .bind(2, nullptr)
                    .bind(3, std::numeric_limits<qint64>::min())
                    .bind(4, -1.5)
                    .bind(5, notes::models::SyncState::Synced) // enums bind as integers
                    .bind(6, std::optional<qint64>{})
                    .run());
    }

    DbResult<ScopedStatement> select = m_db->prepare(QStringLiteral(
        "SELECT title, body, count, ratio, pinned, trashed_at FROM notes ORDER BY id"));
    QVERIFY(select);
    Statement& row = *select.value();
    QVERIFY(row.step().valueOr(false));
    QCOMPARE(row.column<QByteArray>(1), binary);
    QCOMPARE(row.column<qint64>(2), std::numeric_limits<qint64>::max());
    QCOMPARE(row.column<double>(3), 0.25);
    QCOMPARE(row.column<bool>(4), true);
    QCOMPARE(row.column<std::optional<qint64>>(5), std::optional<qint64>{1'800'000'000'000});

    QVERIFY(row.step().valueOr(false));
    QVERIFY(row.isNull(1));
    QVERIFY(row.column<QByteArray>(1).isNull());
    QCOMPARE(row.column<qint64>(2), std::numeric_limits<qint64>::min());
    QCOMPARE(row.column<notes::models::SyncState>(4), notes::models::SyncState::Synced);
    QVERIFY(!row.column<std::optional<qint64>>(5).has_value());
    QCOMPARE(row.columnCount(), 6);
}

// QString and QByteArray never bind as NULL; std::optional does.
void tst_Database::emptyValuesAreNotNull()
{
    {
        DbResult<ScopedStatement> insert =
            m_db->prepare(QStringLiteral("INSERT INTO notes (title, body) VALUES (?, ?)"));
        QVERIFY(insert);
        // A null QString satisfies `title TEXT NOT NULL`.
        QVERIFY(insert.value()->bind(1, QString()).bind(2, QByteArray()).run());
    }
    DbResult<ScopedStatement> select =
        m_db->prepare(QStringLiteral("SELECT title, body, typeof(body) FROM notes"));
    QVERIFY(select);
    QVERIFY(select.value()->step().valueOr(false));
    QVERIFY(!select.value()->isNull(0));
    QVERIFY(!select.value()->isNull(1));
    QCOMPARE(select.value()->column<QString>(2), QStringLiteral("blob"));
    QVERIFY(!select.value()->column<QByteArray>(1).isNull());
    QVERIFY(select.value()->column<QByteArray>(1).isEmpty());

    {
        DbResult<ScopedStatement> nullTitle =
            m_db->prepare(QStringLiteral("INSERT INTO notes (title) VALUES (?)"));
        QVERIFY(nullTitle);
        const DbStatus status = nullTitle.value()->bind(1, std::optional<QString>{}).run();
        QVERIFY(!status); // NOT NULL constraint
        QCOMPARE(status.error().primaryCode(), kSqliteConstraint);
    }
}

void tst_Database::namedParameters()
{
    {
        DbResult<ScopedStatement> insert = m_db->prepare(
            QStringLiteral("INSERT INTO notes (title, count) VALUES (:title, :count)"));
        QVERIFY(insert);
        QVERIFY(insert.value()->bind(":count", 7).bind(":title", QStringLiteral("Named")).run());
    }
    QCOMPARE(scalarText(*m_db, QStringLiteral("SELECT title || count FROM notes")),
             QStringLiteral("Named7"));
}

void tst_Database::badParameterFailsOnStep()
{
    DbResult<ScopedStatement> insert =
        m_db->prepare(QStringLiteral("INSERT INTO notes (title) VALUES (:title)"));
    QVERIFY(insert);

    const DbStatus unknownName = insert.value()->bind(":titel", QStringLiteral("x")).run();
    QVERIFY(!unknownName);
    QCOMPARE(unknownName.error().code, kSqliteRange);
    QVERIFY(unknownName.error().message.contains(QStringLiteral(":titel")));

    insert.value()->reset(); // clears the remembered error
    const DbStatus badIndex = insert.value()->bind(1, QStringLiteral("x")).bind(9, 1).run();
    QVERIFY(!badIndex);
    QCOMPARE(badIndex.error().code, kSqliteRange);
    QCOMPARE(scalarText(*m_db, QStringLiteral("SELECT count(*) FROM notes")), QStringLiteral("0"));
}

void tst_Database::constraintErrorsHaveExtendedCodes()
{
    QVERIFY(m_db->exec(QStringLiteral("INSERT INTO folders (name) VALUES ('Work')")));
    const DbStatus duplicate =
        m_db->exec(QStringLiteral("INSERT INTO folders (name) VALUES ('Work')"));
    QVERIFY(!duplicate);
    QCOMPARE(duplicate.error().code, kSqliteConstraintUnique);
    QCOMPARE(duplicate.error().primaryCode(), kSqliteConstraint);

    // Foreign keys are enforced (PRAGMA foreign_keys = ON).
    DbResult<ScopedStatement> orphan =
        m_db->prepare(QStringLiteral("INSERT INTO notes (title, folder_id) VALUES ('x', ?)"));
    QVERIFY(orphan);
    const DbStatus status = orphan.value()->bind(1, 999).run();
    QVERIFY(!status);
    QCOMPARE(status.error().code, kSqliteConstraintForeignKey);
}

void tst_Database::statementCacheReusesStatements()
{
    const QString sql = QStringLiteral("SELECT count(*) FROM notes");
    const Statement* first = nullptr;
    {
        DbResult<ScopedStatement> a = m_db->prepare(sql);
        QVERIFY(a);
        first = &*a.value();
    }
    {
        DbResult<ScopedStatement> b = m_db->prepare(sql);
        QVERIFY(b);
        QCOMPARE(&*b.value(), first);
    }
    QCOMPARE(m_db->cachedStatementCount(), std::size_t{1});
    {
        DbResult<ScopedStatement> c = m_db->prepare(QStringLiteral("SELECT 1"));
        QVERIFY(c);
    }
    QCOMPARE(m_db->cachedStatementCount(), std::size_t{2});
}

void tst_Database::sameSqlCanBeNested()
{
    QVERIFY(m_db->exec(QStringLiteral("INSERT INTO notes (title) VALUES ('a'), ('b')")));
    const QString sql = QStringLiteral("SELECT title FROM notes ORDER BY id");
    DbResult<ScopedStatement> outer = m_db->prepare(sql);
    QVERIFY(outer);
    QVERIFY(outer.value()->step().valueOr(false));
    {
        DbResult<ScopedStatement> inner = m_db->prepare(sql); // cached one is busy
        QVERIFY(inner);
        QVERIFY(&*inner.value() != &*outer.value());
        QVERIFY(inner.value()->step().valueOr(false));
        QCOMPARE(inner.value()->column<QString>(0), QStringLiteral("a"));
    }
    // The outer statement kept its position.
    QCOMPARE(outer.value()->column<QString>(0), QStringLiteral("a"));
    QVERIFY(outer.value()->step().valueOr(false));
    QCOMPARE(outer.value()->column<QString>(0), QStringLiteral("b"));
    QCOMPARE(m_db->cachedStatementCount(), std::size_t{1});
}

void tst_Database::scopedStatementResetsOnExit()
{
    QVERIFY(m_db->exec(QStringLiteral("INSERT INTO notes (title) VALUES ('a'), ('b')")));
    const QString sql = QStringLiteral("SELECT title FROM notes WHERE title >= ? ORDER BY id");
    {
        DbResult<ScopedStatement> stmt = m_db->prepare(sql);
        QVERIFY(stmt);
        QVERIFY(stmt.value()->bind(1, QStringLiteral("b")).step().valueOr(false));
        QCOMPARE(stmt.value()->column<QString>(0), QStringLiteral("b"));
    } // left mid-result
    DbResult<ScopedStatement> again = m_db->prepare(sql);
    QVERIFY(again);
    // Starts over, and the old binding is gone (NULL matches nothing).
    QVERIFY(!again.value()->step().valueOr(false));
    again.value()->reset();
    QVERIFY(again.value()->bind(1, QStringLiteral("a")).step().valueOr(false));
    QCOMPARE(again.value()->column<QString>(0), QStringLiteral("a"));
}

void tst_Database::rowIdAndChanges()
{
    QVERIFY(m_db->exec(QStringLiteral("INSERT INTO notes (title) VALUES ('a')")));
    QCOMPARE(m_db->lastInsertRowId(), qint64{1});
    QVERIFY(m_db->exec(QStringLiteral("INSERT INTO notes (title) VALUES ('b'), ('c')")));
    QCOMPARE(m_db->lastInsertRowId(), qint64{3});
    QVERIFY(m_db->exec(QStringLiteral("UPDATE notes SET count = 1 WHERE title != 'a'")));
    QCOMPARE(m_db->changes(), 2);
}

QTEST_GUILESS_MAIN(tst_Database)
#include "tst_database.moc"
