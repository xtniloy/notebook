#pragma once

namespace notes::app {

// Sets organization, domain, application name and version on QCoreApplication.
// Call before creating QSettings or using QStandardPaths so both resolve to the
// app's own paths (e.g. ~/.config/augmex/NotesApp.conf).
//
// QCoreApplication::applicationName() is the stable storage name, not the name
// users see; that is QGuiApplication::applicationDisplayName() (set in Application).
void applyAppIdentity();

} // namespace notes::app
