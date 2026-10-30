#pragma once

#include "FakeClock.h"
#include "app/AppContext.h"

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QFuture>
#include <QSettings>
#include <QString>
#include <QTemporaryDir>

#include <memory>

namespace notes::test {

// Put this in note bodies so later tests can prove it never appears on disk,
// in logs, or on the network.
inline constexpr char kSecretMarker[] = "SECRET-MARKER-7f3a";

// A fresh temp directory with a database path inside it, removed on destruction.
// Tests must never touch ~/.local/share or the real keyring.
class TempDb
{
public:
    [[nodiscard]] bool isValid() const { return m_dir.isValid(); }
    [[nodiscard]] QString dirPath() const { return m_dir.path(); }
    [[nodiscard]] QString path() const { return m_dir.filePath(QStringLiteral("notes.db")); }

private:
    QTemporaryDir m_dir;
};

// An AppContext whose settings live in `directory`/settings.ini instead of the
// user's ~/.config, and whose database is `directory`/notes.db, with a FakeClock
// at FakeClock::kDefaultStartMs so results don't depend on the real date.
inline std::unique_ptr<app::AppContext> makeTestContext(const QString& directory)
{
    auto context = std::make_unique<app::AppContext>(
        std::make_unique<QSettings>(directory + QStringLiteral("/settings.ini"),
                                    QSettings::IniFormat),
        std::make_unique<FakeClock>());
    (void)context->openDatabase(directory + QStringLiteral("/notes.db"));
    return context;
}

// The FakeClock inside a makeTestContext() context, to move time forward.
inline FakeClock& fakeClockOf(const app::AppContext& context)
{
    // The context hands out its clock as const; tests own both, so this is safe.
    return const_cast<FakeClock&>( // NOLINT(*-const-cast)
        dynamic_cast<const FakeClock&>(context.clock()));
}

// The result of a service call, waiting with the event loop running. Services
// finish their futures in continuations on the calling thread (ADR 0003), so a
// plain QFuture::waitForFinished() there would never return. Aborts the test
// run if the future doesn't finish in time, rather than hanging.
template <typename T>
T await(const QFuture<T>& future, int timeoutMs = 10'000)
{
    const QDeadlineTimer deadline(timeoutMs);
    while (!future.isFinished() && !deadline.hasExpired()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
    if (!future.isFinished()) {
        qFatal("notes::test::await: future did not finish within %d ms", timeoutMs);
    }
    return future.result();
}

} // namespace notes::test
