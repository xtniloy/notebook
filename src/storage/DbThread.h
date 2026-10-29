#pragma once

#include "storage/Database.h"
#include "storage/DbResult.h"

#include <QFuture>
#include <QPromise>
#include <QString>

#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <type_traits>

class QThread;

namespace notes::storage {

// The one thread that touches the database (ADR 0003). It opens the Database,
// then runs posted jobs one at a time, in the order they were posted. post()
// returns at once with a QFuture; continue with `.then(context, ...)` instead of
// waiting on it from the UI thread.
//
// Jobs return a DbResult<T> or DbStatus. If the database failed to open, or the
// thread has stopped, jobs don't run and their futures carry a DbError.
class DbThread
{
public:
    // Starts the thread, which opens (or creates) the database at `path`.
    explicit DbThread(QString path);
    // Calls stop().
    ~DbThread();
    DbThread(const DbThread&) = delete;
    DbThread& operator=(const DbThread&) = delete;
    DbThread(DbThread&&) = delete;
    DbThread& operator=(DbThread&&) = delete;

    // Finishes once the database is open, or failed to open.
    [[nodiscard]] QFuture<DbStatus> opened() const { return m_opened.future(); }

    // Queues `job(Database&)` to run on the DB thread.
    template <typename Job>
    [[nodiscard]] auto post(Job&& job) -> QFuture<std::invoke_result_t<Job&, Database&>>
    {
        using Result = std::invoke_result_t<Job&, Database&>;
        static_assert(std::is_constructible_v<Result, DbError>,
                      "jobs return a DbResult<T> or DbStatus");

        auto promise = std::make_shared<QPromise<Result>>();
        // Shared, so move-only jobs fit in a std::function.
        auto work = std::make_shared<std::decay_t<Job>>(std::forward<Job>(job));
        QFuture<Result> future = promise->future();
        promise->start();
        const bool queued = enqueue([promise, work](Database* db, const DbError& error) {
            promise->addResult(db != nullptr ? (*work)(*db) : Result(error));
            promise->finish();
        });
        if (!queued) {
            promise->addResult(Result(stoppedError()));
            promise->finish();
        }
        return future;
    }

    // Runs every job already posted, closes the database and joins the thread.
    // Jobs posted afterwards fail at once. Safe to call more than once.
    void stop();

private:
    using Task = std::function<void(Database*, const DbError&)>;

    bool enqueue(Task task);
    void run();
    [[nodiscard]] static DbError stoppedError();

    QString m_path;
    QPromise<DbStatus> m_opened;

    // std:: rather than QMutex/QWaitCondition: QWaitCondition::wait() releases
    // the mutex inside libQt6Core, which ThreadSanitizer can't see, so TSan runs
    // (TC-STO-006) reported false races.
    std::mutex m_mutex;
    std::condition_variable m_wake;
    std::deque<Task> m_queue; // guarded by m_mutex
    bool m_stopping = false;  // guarded by m_mutex

    std::unique_ptr<QThread> m_thread;
};

} // namespace notes::storage
