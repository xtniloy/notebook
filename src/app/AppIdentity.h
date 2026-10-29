#pragma once

namespace notes::app {

// Sets organization, domain, application name and version on QCoreApplication.
// Call before creating QSettings or using QStandardPaths so both resolve to
// NotesApp's own paths (e.g. ~/.config/augmex/NotesApp.conf).
void applyAppIdentity();

} // namespace notes::app
