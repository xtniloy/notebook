// DbThread (task 2.3, ADR 0003): TC-STO-006 jobs from several threads all run,
// in order per caller. Plus: jobs run on the DB thread, continuations come back
// to the caller's thread, stop() drains the queue, and failures arrive as values.

#include "TestHelpers.h"
#include "storage/DbThread.h"
#include "storage/Statement.h"

#include <QFutureSynchronizer>
#include <QTest>
#include <QThread>

#include <memory>
#include <numeric>
#include <vector>

using namespace notes::storage;

namespace {

DbStatus insertLog(Database& db, int caller, int sequence)
{
    DbResult<ScopedStatement> insert =
        db.prepare(QStringLiteral("INSERT INTO log (caller, seq) VALUES (?, ?)"));
    if (!insert) {
        return insert.error();
    }
    return insert.value()->bind(1, caller).bind(2, sequence).run();
}

DbResult<qint64> countRows(Database& db, const QString& table)
{
    DbResult<ScopedStatement> count = db.prepare(QStringLiteral("SELECT count(*) FROM ") + table);
    if (!count) {
        return count.error();
    }
    if (DbResult<bool> row = count.value()->step(); !row) {
        return row.error();
    }
    return count.value()->column<qint64>(0);
}

} // namespace

class tst_DbThread : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void opensAndRunsJobsOnItsOwnThread();
    void jobsFromFourThreadsRunInOrderPerCaller();
    void continuationRunsOnCallerThread();
    void stopRunsPendingJobsFirst();
    void postAfterStopFailsAtOnce();
    void openFailureFailsJobs();

private:
    std::unique_ptr<notes::test::TempDb> m_dir;
};

void tst_DbThread::init()
{
    m_dir = std::make_unique<notes::test::TempDb>();
    QVERIFY(m_dir->isValid());
}

void tst_DbThread::cleanup()
{
    m_dir = nullptr;
}

void tst_DbThread::opensAndRunsJobsOnItsOwnThread()
{
    DbThread db(m_dir->path());
    QFuture<DbStatus> opened = db.opened();
    opened.waitForFinished();
    QVERIFY(opened.result());

    const auto threadOf = [](Database&) -> DbResult<quintptr> {
        return reinterpret_cast<quintptr>(QThread::currentThread());
    };
    QFuture<DbResult<quintptr>> first = db.post(threadOf);
    QFuture<DbResult<quintptr>> second = db.post(threadOf);
    const quintptr dbThread = first.result().value();
    QVERIFY(dbThread != reinterpret_cast<quintptr>(QThread::currentThread()));
    QCOMPARE(second.result().value(), dbThread);

    QFuture<DbResult<QString>> name = db.post(
        [](Database&) -> DbResult<QString> { return QThread::currentThread()->objectName(); });
    QCOMPARE(name.result().value(), QStringLiteral("notes-db"));
}

// TC-STO-006
void tst_DbThread::jobsFromFourThreadsRunInOrderPerCaller()
{
    constexpr int kCallers = 4;
    constexpr int kJobsPerCaller = 250;

    DbThread db(m_dir->path());
    QVERIFY(db.post([](Database& d) {
                  return d.exec(QStringLiteral(
                      "CREATE TABLE log (id INTEGER PRIMARY KEY, caller INTEGER, seq INTEGER)"));
              }).result());

    // Each caller thread posts its jobs and keeps the futures.
    std::vector<QList<QFuture<DbStatus>>> futures(kCallers);
    std::vector<std::unique_ptr<QThread>> callers;
    for (int caller = 0; caller < kCallers; ++caller) {
        callers.emplace_back(QThread::create([&db, &futures, caller] {
            for (int seq = 0; seq < kJobsPerCaller; ++seq) {
                futures.at(static_cast<std::size_t>(caller))
                    .append(
                        db.post([caller, seq](Database& d) { return insertLog(d, caller, seq); }));
            }
        }));
        callers.back()->start();
    }
    for (const auto& thread : callers) {
        QVERIFY(thread->wait(30'000));
    }

    for (const QList<QFuture<DbStatus>>& list : futures) {
        QCOMPARE(list.size(), kJobsPerCaller);
        for (QFuture<DbStatus> future : list) {
            future.waitForFinished();
            QVERIFY2(future.result(), qPrintable(future.result().error().message));
        }
    }

    // Insertion order (rowid) is execution order: each caller's jobs ran in the
    // order that caller posted them.
    QFuture<DbResult<QList<QList<int>>>> order =
        db.post([](Database& d) -> DbResult<QList<QList<int>>> {
            DbResult<ScopedStatement> select =
                d.prepare(QStringLiteral("SELECT caller, seq FROM log ORDER BY id"));
            if (!select) {
                return select.error();
            }
            QList<QList<int>> seen(kCallers);
            while (true) {
                DbResult<bool> row = select.value()->step();
                if (!row) {
                    return row.error();
                }
                if (!row.value()) {
                    break;
                }
                seen[select.value()->column<int>(0)].append(select.value()->column<int>(1));
            }
            return seen;
        });
    const QList<QList<int>> seen = order.result().value();
    QList<int> expected(kJobsPerCaller);
    std::iota(expected.begin(), expected.end(), 0);
    for (int caller = 0; caller < kCallers; ++caller) {
        QCOMPARE(seen.at(caller), expected);
    }
}

void tst_DbThread::continuationRunsOnCallerThread()
{
    DbThread db(m_dir->path());
    QObject context; // lives on this (the test's main) thread
    QThread* ranOn = nullptr;
    qint64 value = 0;
    db.post([](Database&) -> DbResult<qint64> {
          return 42;
      }).then(&context, [&](const DbResult<qint64>& result) {
        ranOn = QThread::currentThread();
        value = result.valueOr(-1);
    });
    QTRY_COMPARE(value, qint64{42});
    QCOMPARE(ranOn, QThread::currentThread());
}

void tst_DbThread::stopRunsPendingJobsFirst()
{
    constexpr int kJobs = 200;
    QList<QFuture<DbStatus>> pending;
    {
        DbThread db(m_dir->path());
        pending.append(db.post([](Database& d) {
            return d.exec(QStringLiteral("CREATE TABLE log (caller INTEGER, seq INTEGER)"));
        }));
        for (int seq = 0; seq < kJobs; ++seq) {
            pending.append(db.post([seq](Database& d) { return insertLog(d, 0, seq); }));
        }
        db.stop(); // returns only after the queue is empty
        for (const QFuture<DbStatus>& future : std::as_const(pending)) {
            QVERIFY(future.isFinished());
            QVERIFY(future.result());
        }
    }

    DbResult<std::unique_ptr<Database>> reopened = Database::open(m_dir->path());
    QVERIFY(reopened);
    QCOMPARE(countRows(*reopened.value(), QStringLiteral("log")).value(), qint64{kJobs});
}

void tst_DbThread::postAfterStopFailsAtOnce()
{
    DbThread db(m_dir->path());
    db.stop();
    db.stop(); // idempotent
    bool ran = false;
    QFuture<DbStatus> late = db.post([&ran](Database&) {
        ran = true;
        return DbStatus{};
    });
    QVERIFY(late.isFinished());
    QVERIFY(!late.result());
    QVERIFY(late.result().error().message.contains(QStringLiteral("stopped")));
    QVERIFY(!ran);
}

void tst_DbThread::openFailureFailsJobs()
{
    DbThread db(m_dir->dirPath() + QStringLiteral("/missing/notes.db"));
    QFuture<DbStatus> opened = db.opened();
    opened.waitForFinished();
    QVERIFY(!opened.result());

    bool ran = false;
    QFuture<DbStatus> job = db.post([&ran](Database&) {
        ran = true;
        return DbStatus{};
    });
    job.waitForFinished();
    QVERIFY(!job.result());
    QVERIFY2(job.result().error().message.startsWith(QStringLiteral("Database is not open")),
             qPrintable(job.result().error().message));
    QVERIFY(!ran);
}

QTEST_GUILESS_MAIN(tst_DbThread)
#include "tst_dbthread.moc"
