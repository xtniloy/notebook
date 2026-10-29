#pragma once

#include <QStringList>

namespace notes::ui {

// Bundled font families (resources/fonts/, Plan §3.6).
inline constexpr char kUiFontFamily[] = "Inter";
inline constexpr char kMonospaceFontFamily[] = "JetBrains Mono";

// Registers the bundled fonts with QFontDatabase. Call once, after the
// QGuiApplication exists. Returns the resource paths that failed to load (empty
// on success); Qt then falls back to system fonts for those.
QStringList loadBundledFonts();

} // namespace notes::ui
