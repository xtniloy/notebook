#pragma once

#include <memory>

class QSettings;

namespace notes::app {

// Owns the app's long-lived services and hands them to the UI by reference
// (dependency injection: no globals or singletons). Services are added as they
// are built: NoteService/FolderService in task 2.7, SearchService in 5.3, ...
class AppContext
{
public:
    explicit AppContext(std::unique_ptr<QSettings> settings);
    ~AppContext();

    AppContext(const AppContext&) = delete;
    AppContext& operator=(const AppContext&) = delete;
    AppContext(AppContext&&) = delete;
    AppContext& operator=(AppContext&&) = delete;

    // Uses the user's settings file. applyAppIdentity() must have run first.
    static std::unique_ptr<AppContext> createDefault();

    [[nodiscard]] QSettings& settings() { return *m_settings; }

private:
    std::unique_ptr<QSettings> m_settings;
};

} // namespace notes::app
