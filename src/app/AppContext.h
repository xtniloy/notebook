#pragma once

#include <memory>

class QSettings;

namespace notes::app {

class IClock;

// Owns the app's long-lived services and hands them to the UI by reference
// (dependency injection: no globals or singletons). Services are added as they
// are built: NoteService/FolderService in task 2.7, SearchService in 5.3, ...
class AppContext
{
public:
    AppContext(std::unique_ptr<QSettings> settings, std::unique_ptr<IClock> clock);
    ~AppContext();

    AppContext(const AppContext&) = delete;
    AppContext& operator=(const AppContext&) = delete;
    AppContext(AppContext&&) = delete;
    AppContext& operator=(AppContext&&) = delete;

    // Uses the user's settings file and the system clock. applyAppIdentity()
    // must have run first.
    static std::unique_ptr<AppContext> createDefault();

    [[nodiscard]] QSettings& settings() { return *m_settings; }
    // The current time for all app code (ADR 0004).
    [[nodiscard]] const IClock& clock() const { return *m_clock; }

private:
    std::unique_ptr<QSettings> m_settings;
    std::unique_ptr<IClock> m_clock;
};

} // namespace notes::app
