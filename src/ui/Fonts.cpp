#include "ui/Fonts.h"

#include <QFontDatabase>

#include <array>

namespace notes::ui {

namespace {

// Keep in sync with the "fonts" resource in src/CMakeLists.txt.
constexpr std::array kFontFiles = {
    ":/fonts/Inter-Regular.ttf",      ":/fonts/Inter-Italic.ttf",
    ":/fonts/Inter-SemiBold.ttf",     ":/fonts/Inter-Bold.ttf",
    ":/fonts/Inter-BoldItalic.ttf",   ":/fonts/JetBrainsMono-Regular.ttf",
    ":/fonts/JetBrainsMono-Bold.ttf",
};

} // namespace

QStringList loadBundledFonts()
{
    QStringList failed;
    for (const char* file : kFontFiles) {
        const QString path = QString::fromLatin1(file);
        if (QFontDatabase::addApplicationFont(path) < 0) {
            failed.append(path);
        }
    }
    return failed;
}

} // namespace notes::ui
