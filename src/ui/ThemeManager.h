#pragma once

#include <QColor>
#include <QObject>
#include <QPalette>

#include <cstdint>

class QSettings;

namespace notes::ui {

enum class Theme : std::uint8_t {
    Light,
    Dark,
};

// The user's choice in View → Appearance, saved in QSettings.
enum class ThemePreference : std::uint8_t {
    System, // follow the desktop's light/dark setting, live
    Light,
    Dark,
};

// The color tokens both themes define. The palette and the shared style sheet
// (resources/themes/theme.qss, with `${name}` placeholders) are built from these, so
// custom-painted widgets (delegates) and styled widgets always agree.
struct ThemeColors
{
    QColor window;     // toolbar, menus, splitter
    QColor sidebar;    // folder sidebar
    QColor base;       // note list, editor, text fields
    QColor text;       // primary text and icons
    QColor mutedText;  // dates, counts, placeholders, disabled
    QColor separator;  // hairlines and borders
    QColor hover;      // translucent overlay for hovered buttons
    QColor accent;     // selection highlight, focus ring (Plan §3.6)
    QColor accentText; // text and icons on the accent
    QColor link;
};

// Applies the light or dark theme to the whole application: Fusion style, a
// palette and a style sheet. Follows the system color scheme while the
// preference is System, and switches live without a restart (Plan §3.6).
class ThemeManager : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(ThemeManager)

public:
    // Longest animation the app allows (Plan §3.6: "150 ms fades").
    static constexpr int kMaxAnimationMs = 150;

    // Reads the preference from `settings` and applies the theme right away.
    explicit ThemeManager(QSettings& settings, QObject* parent = nullptr);
    ~ThemeManager() override;

    [[nodiscard]] ThemePreference preference() const { return m_preference; }
    // Saves the preference and applies the resulting theme.
    void setPreference(ThemePreference preference);

    [[nodiscard]] Theme theme() const { return m_theme; }

    [[nodiscard]] static Theme resolve(ThemePreference preference, Qt::ColorScheme system);
    [[nodiscard]] static const ThemeColors& colors(Theme theme);
    [[nodiscard]] static QPalette palette(Theme theme);
    [[nodiscard]] static QString styleSheet(Theme theme);

signals:
    void themeChanged(notes::ui::Theme theme);

private:
    void onSystemColorSchemeChanged(Qt::ColorScheme scheme);
    // Applies `theme` if it differs from the current one; `fade` cross-fades
    // visible windows from a snapshot of the old look.
    void apply(Theme theme, bool fade);

    QSettings& m_settings;
    ThemePreference m_preference = ThemePreference::System;
    Qt::ColorScheme m_systemScheme = Qt::ColorScheme::Unknown;
    Theme m_theme = Theme::Light;
    bool m_applied = false;
};

} // namespace notes::ui
