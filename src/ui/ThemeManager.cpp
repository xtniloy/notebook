#include "ui/ThemeManager.h"

#include <QApplication>
#include <QFile>
#include <QGraphicsOpacityEffect>
#include <QLabel>
#include <QLoggingCategory>
#include <QPropertyAnimation>
#include <QRegularExpression>
#include <QSettings>
#include <QStyleFactory>
#include <QStyleHints>

namespace notes::ui {

namespace {

Q_LOGGING_CATEGORY(lcTheme, "notes.ui.theme")

constexpr QLatin1StringView kPreferenceKey("appearance/theme");
constexpr QLatin1StringView kFadeOverlayName("themeFadeOverlay");

// Both themes share the accent; everything else is tuned for contrast (WCAG AA:
// tst_thememanager checks text, muted text, links and the selection).
const ThemeColors kLight{
    .window = QColor(0xF5, 0xF5, 0xF4),
    .sidebar = QColor(0xED, 0xED, 0xEB),
    .base = QColor(0xFF, 0xFF, 0xFF),
    .text = QColor(0x1D, 0x1D, 0x1F),
    .mutedText = QColor(0x6A, 0x6A, 0x70),
    .separator = QColor(0xD8, 0xD8, 0xD5),
    .hover = QColor(0, 0, 0, 18),
    .accent = QColor(0xF2, 0xA9, 0x00),
    .accentText = QColor(0x1D, 0x1D, 0x1F),
    .link = QColor(0x8F, 0x63, 0x00),
};

const ThemeColors kDark{
    .window = QColor(0x2A, 0x2A, 0x2C),
    .sidebar = QColor(0x23, 0x23, 0x25),
    .base = QColor(0x1C, 0x1C, 0x1E),
    .text = QColor(0xED, 0xED, 0xF0),
    .mutedText = QColor(0xA1, 0xA1, 0xA8),
    .separator = QColor(0x3A, 0x3A, 0x3D),
    .hover = QColor(255, 255, 255, 22),
    .accent = QColor(0xF2, 0xA9, 0x00),
    .accentText = QColor(0x1C, 0x1C, 0x1E),
    .link = QColor(0xF5, 0xC0, 0x4A),
};

QString preferenceName(ThemePreference preference)
{
    switch (preference) {
    case ThemePreference::System:
        return QStringLiteral("system");
    case ThemePreference::Light:
        return QStringLiteral("light");
    case ThemePreference::Dark:
        return QStringLiteral("dark");
    }
    return QStringLiteral("system");
}

ThemePreference preferenceFromName(const QString& name)
{
    if (name == u"light") {
        return ThemePreference::Light;
    }
    if (name == u"dark") {
        return ThemePreference::Dark;
    }
    return ThemePreference::System; // also for missing or unknown values
}

QString qssColor(const QColor& color)
{
    if (color.alpha() == 255) {
        return color.name(QColor::HexRgb);
    }
    return QStringLiteral("rgba(%1, %2, %3, %4)")
        .arg(color.red())
        .arg(color.green())
        .arg(color.blue())
        .arg(color.alpha());
}

// Qt's built-in menu, combo box and tooltip animations have their own, longer
// durations; turning them off keeps every animation within kMaxAnimationMs.
void disableBuiltInAnimations()
{
    for (const Qt::UIEffect effect :
         {Qt::UI_AnimateMenu, Qt::UI_FadeMenu, Qt::UI_AnimateCombo, Qt::UI_AnimateTooltip,
          Qt::UI_FadeTooltip, Qt::UI_AnimateToolBox}) {
        QApplication::setEffectEnabled(effect, false);
    }
}

// A picture of each visible window in its current look, laid over the window.
QList<QLabel*> snapshotVisibleWindows()
{
    QList<QLabel*> overlays;
    for (QWidget* window : QApplication::topLevelWidgets()) {
        if (!window->isVisible() || window->isMinimized() || window->size().isEmpty()) {
            continue;
        }
        auto* overlay = new QLabel(window);
        overlay->setObjectName(kFadeOverlayName);
        overlay->setAttribute(Qt::WA_TransparentForMouseEvents);
        overlay->setPixmap(window->grab());
        overlay->setGeometry(window->rect());
        overlay->show();
        overlay->raise();
        overlays.append(overlay);
    }
    return overlays;
}

// Fades the snapshots out, revealing the new theme underneath.
void fadeOut(const QList<QLabel*>& overlays)
{
    for (QLabel* overlay : overlays) {
        auto* effect = new QGraphicsOpacityEffect(overlay);
        overlay->setGraphicsEffect(effect);
        auto* animation = new QPropertyAnimation(effect, "opacity", overlay);
        animation->setDuration(ThemeManager::kMaxAnimationMs);
        animation->setStartValue(1.0);
        animation->setEndValue(0.0);
        QObject::connect(animation, &QPropertyAnimation::finished, overlay, &QObject::deleteLater);
        animation->start();
    }
}

} // namespace

ThemeManager::ThemeManager(QSettings& settings, QObject* parent)
    : QObject(parent)
    , m_settings(settings)
    , m_preference(preferenceFromName(settings.value(kPreferenceKey).toString()))
    , m_systemScheme(QGuiApplication::styleHints()->colorScheme())
{
    // The palette and style sheet are written for Fusion; native styles (e.g.
    // Breeze) ignore parts of the palette. Set unconditionally: once a style sheet
    // is active, style() is Qt's style sheet wrapper and its name says nothing.
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    disableBuiltInAnimations();

    // The scheme comes from the signal's argument, not a fresh colorScheme() query,
    // so a test can emit the signal on platforms without a real color scheme.
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this,
            &ThemeManager::onSystemColorSchemeChanged);

    apply(resolve(m_preference, m_systemScheme), /*fade=*/false);
}

ThemeManager::~ThemeManager() = default;

void ThemeManager::setPreference(ThemePreference preference)
{
    m_preference = preference;
    m_settings.setValue(kPreferenceKey, preferenceName(preference));
    apply(resolve(preference, m_systemScheme), /*fade=*/true);
}

void ThemeManager::onSystemColorSchemeChanged(Qt::ColorScheme scheme)
{
    m_systemScheme = scheme;
    apply(resolve(m_preference, scheme), /*fade=*/true);
}

Theme ThemeManager::resolve(ThemePreference preference, Qt::ColorScheme system)
{
    switch (preference) {
    case ThemePreference::Light:
        return Theme::Light;
    case ThemePreference::Dark:
        return Theme::Dark;
    case ThemePreference::System:
        break;
    }
    // Unknown (no desktop setting, e.g. headless) falls back to light.
    return system == Qt::ColorScheme::Dark ? Theme::Dark : Theme::Light;
}

const ThemeColors& ThemeManager::colors(Theme theme)
{
    return theme == Theme::Dark ? kDark : kLight;
}

QPalette ThemeManager::palette(Theme theme)
{
    const ThemeColors& c = colors(theme);
    QPalette palette;
    palette.setColor(QPalette::Window, c.window);
    palette.setColor(QPalette::WindowText, c.text);
    palette.setColor(QPalette::Base, c.base);
    palette.setColor(QPalette::AlternateBase, c.window);
    palette.setColor(QPalette::Text, c.text);
    palette.setColor(QPalette::PlaceholderText, c.mutedText);
    palette.setColor(QPalette::Button, c.base);
    palette.setColor(QPalette::ButtonText, c.text);
    palette.setColor(QPalette::BrightText, theme == Theme::Dark ? Qt::white : Qt::black);
    palette.setColor(QPalette::ToolTipBase, c.base);
    palette.setColor(QPalette::ToolTipText, c.text);
    palette.setColor(QPalette::Highlight, c.accent);
    palette.setColor(QPalette::HighlightedText, c.accentText);
    palette.setColor(QPalette::Accent, c.accent);
    palette.setColor(QPalette::Link, c.link);
    palette.setColor(QPalette::LinkVisited, c.link);
    // Fusion draws bevels and frames from these.
    palette.setColor(QPalette::Light, theme == Theme::Dark ? c.separator.lighter(130) : c.base);
    palette.setColor(QPalette::Midlight, c.window);
    palette.setColor(QPalette::Mid, c.separator);
    palette.setColor(QPalette::Dark, c.separator.darker(130));
    palette.setColor(QPalette::Shadow, QColor(0, 0, 0, theme == Theme::Dark ? 160 : 60));

    for (const QPalette::ColorRole role :
         {QPalette::WindowText, QPalette::Text, QPalette::ButtonText}) {
        palette.setColor(QPalette::Disabled, role, c.mutedText);
    }
    return palette;
}

QString ThemeManager::styleSheet(Theme theme)
{
    QFile file(QStringLiteral(":/themes/theme.qss"));
    if (!file.open(QIODevice::ReadOnly)) {
        qCWarning(lcTheme) << "Missing style sheet" << file.fileName();
        return {};
    }
    const QString source = QString::fromUtf8(file.readAll());

    const ThemeColors& c = colors(theme);
    const QHash<QString, QColor> tokens{
        {QStringLiteral("window"), c.window},
        {QStringLiteral("sidebar"), c.sidebar},
        {QStringLiteral("base"), c.base},
        {QStringLiteral("text"), c.text},
        {QStringLiteral("mutedText"), c.mutedText},
        {QStringLiteral("separator"), c.separator},
        {QStringLiteral("hover"), c.hover},
        {QStringLiteral("accent"), c.accent},
        {QStringLiteral("accentText"), c.accentText},
        {QStringLiteral("link"), c.link},
    };

    static const QRegularExpression kToken(QStringLiteral(R"(\$\{(\w+)\})"));
    QString result;
    result.reserve(source.size());
    qsizetype last = 0;
    for (const QRegularExpressionMatch& match : kToken.globalMatch(source)) {
        result += QStringView(source).sliced(last, match.capturedStart() - last);
        const auto color = tokens.constFind(match.captured(1));
        if (color == tokens.constEnd()) {
            qCWarning(lcTheme) << "Unknown color token" << match.captured(0);
        } else {
            result += qssColor(*color);
        }
        last = match.capturedEnd();
    }
    result += QStringView(source).sliced(last);
    return result;
}

void ThemeManager::apply(Theme theme, bool fade)
{
    if (m_applied && theme == m_theme) {
        return;
    }
    const QList<QLabel*> snapshots =
        fade && m_applied ? snapshotVisibleWindows() : QList<QLabel*>{};
    m_theme = theme;
    m_applied = true;
    QApplication::setPalette(palette(theme));
    qApp->setStyleSheet(styleSheet(theme));
    fadeOut(snapshots);
    emit themeChanged(theme);
}

} // namespace notes::ui
