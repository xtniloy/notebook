#include "storage/DbThread.h"

#include <QThread>

#include <sqlite3.h>

#include <utility>

namespace notes::storage {

DbThread::DbThread(QString path)
    : m_path(std::move(path))
{
    m_opened.start();
    m_thread.reset(QThread::create([this] { run(); }));
    m_thread->setObjectName(QStringLiteral("notes-db"));
    m_thread->start();
}

DbThread::~DbThread()
{
    stop();
}

void DbThread::stop()
{
    {
        const std::lock_guard lock(m_mutex);
        m_stopping = true;
    }
    m_wake.notify_all();
    if (m_thread) {
        m_thread->wait();
    }
}

bool DbThread::enqueue(Task task)
{
    {
        const std::lock_guard lock(m_mutex);
        if (m_stopping) {
            return false;
        }
        m_queue.push_back(std::move(task));
    }
    m_wake.notify_one();
    return true;
}

void DbThread::run()
{
    // Everything below runs on the DB thread; `db` never leaves it.
    std::unique_ptr<Database> db;
    DbError openError;
    if (DbResult<std::unique_ptr<Database>> opened = Database::open(m_path)) {
        db = std::move(opened).value();
        m_opened.addResult(DbStatus{});
    } else {
        openError = opened.error();
        openError.message = QStringLiteral("Database is not open: %1").arg(openError.message);
        m_opened.addResult(DbStatus(opened.error()));
    }
    m_opened.finish();

    while (true) {
        Task task;
        {
            std::unique_lock lock(m_mutex);
            m_wake.wait(lock, [this] { return !m_queue.empty() || m_stopping; });
            if (m_queue.empty()) {
                break; // stopping, and every posted job has run
            }
            task = std::move(m_queue.front());
            m_queue.pop_front();
        }
        task(db.get(), openError);
    }
    db.reset(); // closed on the thread that used it
}

DbError DbThread::stoppedError()
{
    return {.code = SQLITE_MISUSE, .message = QStringLiteral("Database thread has stopped")};
}

} // namespace notes::storage
