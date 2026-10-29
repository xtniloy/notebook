// ThemeManager (task 1.8): TC-UI-018 forcing dark applies the dark theme,
// TC-UI-019 a system color-scheme change switches theme without a restart.
// Also: the System/Light/Dark preference, the 150 ms animation limit, themed
// icons, and WCAG contrast of the color tokens (the automatable part of the
// manual check TC-UI-020). Runs under QT_QPA_PLATFORM=offscreen.

#include "TestHelpers.h"
#include "ui/EditorView.h"
#include "ui/Icons.h"
#include "ui/MainWindow.h"
#include "ui/NoteTextEdit.h"
#include "ui/Sidebar.h"
#include "ui/ThemeManager.h"

#include <QAction>
#include <QApplication>
#include <QGraphicsOpacityEffect>
#include <QLabel>
#include <QPropertyAnimation>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QStyle>
#include <QStyleHints>
#include <QTest>

#include <cmath>
#include <memory>

using notes::ui::Theme;
using notes::ui::ThemeManager;
using notes::ui::ThemePreference;

Q_DECLARE_METATYPE(ThemePreference)
Q_DECLARE_METATYPE(Theme)

namespace {

// WCAG 2.x relative luminance and contrast ratio.
double luminance(const QColor& color)
{
    const auto channel = [](double c) {
        return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
    };
    return (0.2126 * channel(color.redF())) + (0.7152 * channel(color.greenF())) +
           (0.0722 * channel(color.blueF()));
}

double contrast(const QColor& a, const QColor& b)
{
    const double la = luminance(a);
    const double lb = luminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

void emitSystemScheme(Qt::ColorScheme scheme)
{
    // QStyleHints signals are public; offscreen has no real desktop setting.
    emit QGuiApplication::styleHints() -> colorSchemeChanged(scheme);
}

// The color most of an icon's opaque pixels are drawn in.
QColor dominantColor(const QImage& image)
{
    QHash<QRgb, int> counts;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QColor pixel = image.pixelColor(x, y);
            if (pixel.alpha() == 255) {
                ++counts[pixel.rgb()];
            }
        }
    }
    QRgb best = 0;
    int bestCount = 0;
    for (auto it = counts.cbegin(); it != counts.cend(); ++it) {
        if (it.value() > bestCount) {
            best = it.key();
            bestCount = it.value();
        }
    }
    return QColor::fromRgb(best);
}

} // namespace

class tst_ThemeManager : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void resolve_data();
    void resolve();
    void usesFusionAndNoLongAnimations();
    void forcingDarkAppliesDarkTheme();
    void systemSchemeChangeSwitchesLive();
    void manualOverrideIgnoresSystem();
    void preferenceIsSaved();
    void unknownPreferenceMeansSystem();
    void styleSheetHasNoUnresolvedTokens_data();
    void styleSheetHasNoUnresolvedTokens();
    void colorsAreReadable_data();
    void colorsAreReadable();
    void iconsFollowTheTheme();
    void switchFadesWithinLimit();
    void appearanceMenuSwitchesTheme();

private:
    std::unique_ptr<ThemeManager> makeManager();

    std::unique_ptr<notes::test::TempDb> m_dir;
    std::unique_ptr<notes::app::AppContext> m_context;
};

void tst_ThemeManager::init()
{
    m_dir = std::make_unique<notes::test::TempDb>();
    QVERIFY(m_dir->isValid());
    m_context = notes::test::makeTestContext(m_dir->dirPath());
}

void tst_ThemeManager::cleanup()
{
    emitSystemScheme(Qt::ColorScheme::Unknown);
    m_context = nullptr;
    m_dir = nullptr;
}

std::unique_ptr<ThemeManager> tst_ThemeManager::makeManager()
{
    return std::make_unique<ThemeManager>(m_context->settings());
}

void tst_ThemeManager::resolve_data()
{
    QTest::addColumn<ThemePreference>("preference");
    QTest::addColumn<Qt::ColorScheme>("system");
    QTest::addColumn<Theme>("expected");

    QTest::newRow("system dark") << ThemePreference::System << Qt::ColorScheme::Dark << Theme::Dark;
    QTest::newRow("system light") << ThemePreference::System << Qt::ColorScheme::Light
                                  << Theme::Light;
    QTest::newRow("system unknown")
        << ThemePreference::System << Qt::ColorScheme::Unknown << Theme::Light;
    QTest::newRow("light on dark desktop")
        << ThemePreference::Light << Qt::ColorScheme::Dark << Theme::Light;
    QTest::newRow("dark on light desktop")
        << ThemePreference::Dark << Qt::ColorScheme::Light << Theme::Dark;
}

void tst_ThemeManager::resolve()
{
    QFETCH(ThemePreference, preference);
    QFETCH(Qt::ColorScheme, system);
    QFETCH(Theme, expected);
    QCOMPARE(ThemeManager::resolve(preference, system), expected);
}

void tst_ThemeManager::usesFusionAndNoLongAnimations()
{
    const auto manager = makeManager();
    // With a style sheet set, style() is Qt's style sheet wrapper; look underneath.
    const QString sheet = qApp->styleSheet();
    qApp->setStyleSheet({});
    QCOMPARE(QApplication::style()->name().toLower(), QStringLiteral("fusion"));
    qApp->setStyleSheet(sheet);
    for (const Qt::UIEffect effect :
         {Qt::UI_AnimateMenu, Qt::UI_FadeMenu, Qt::UI_AnimateCombo, Qt::UI_AnimateTooltip,
          Qt::UI_FadeTooltip, Qt::UI_AnimateToolBox}) {
        QVERIFY2(!QApplication::isEffectEnabled(effect), qPrintable(QString::number(effect)));
    }
    QCOMPARE(ThemeManager::kMaxAnimationMs, 150);
}

// TC-UI-018
void tst_ThemeManager::forcingDarkAppliesDarkTheme()
{
    const auto manager = makeManager();
    QCOMPARE(manager->theme(), Theme::Light); // offscreen reports no color scheme

    notes::ui::EditorView editor;
    notes::ui::Sidebar sidebar;
    sidebar.setObjectName(QStringLiteral("sidebarPane"));
    editor.show();
    sidebar.show();
    QVERIFY(QTest::qWaitForWindowExposed(&editor));

    const QSignalSpy spy(manager.get(), &ThemeManager::themeChanged);
    manager->setPreference(ThemePreference::Dark);
    QCOMPARE(manager->theme(), Theme::Dark);
    QCOMPARE(spy.count(), 1);

    const notes::ui::ThemeColors& dark = ThemeManager::colors(Theme::Dark);
    QCOMPARE(qApp->styleSheet(), ThemeManager::styleSheet(Theme::Dark));
    QCOMPARE(QApplication::palette().color(QPalette::Base), dark.base);
    // Known widgets pick it up: the editor's text area and the sidebar pane.
    QCOMPARE(editor.textEdit()->palette().color(QPalette::Base), dark.base);
    QCOMPARE(editor.textEdit()->palette().color(QPalette::Text), dark.text);
    QCOMPARE(editor.textEdit()->palette().color(QPalette::Highlight), dark.accent);
    QCOMPARE(sidebar.palette().color(sidebar.backgroundRole()), dark.sidebar);
    QCOMPARE(editor.palette().color(editor.backgroundRole()), dark.base);
}

// TC-UI-019
void tst_ThemeManager::systemSchemeChangeSwitchesLive()
{
    const auto manager = makeManager();
    QCOMPARE(manager->preference(), ThemePreference::System);
    QSignalSpy spy(manager.get(), &ThemeManager::themeChanged);

    emitSystemScheme(Qt::ColorScheme::Dark);
    QCOMPARE(manager->theme(), Theme::Dark);
    QCOMPARE(QApplication::palette().color(QPalette::Window),
             ThemeManager::colors(Theme::Dark).window);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.takeFirst().at(0).value<Theme>(), Theme::Dark);

    emitSystemScheme(Qt::ColorScheme::Dark); // no change, no signal
    QCOMPARE(spy.count(), 0);

    emitSystemScheme(Qt::ColorScheme::Light);
    QCOMPARE(manager->theme(), Theme::Light);
    QCOMPARE(QApplication::palette().color(QPalette::Window),
             ThemeManager::colors(Theme::Light).window);
    QCOMPARE(spy.count(), 1);
}

void tst_ThemeManager::manualOverrideIgnoresSystem()
{
    const auto manager = makeManager();
    manager->setPreference(ThemePreference::Light);
    emitSystemScheme(Qt::ColorScheme::Dark);
    QCOMPARE(manager->theme(), Theme::Light);

    // Back to System: the desktop's current scheme takes effect right away.
    manager->setPreference(ThemePreference::System);
    QCOMPARE(manager->theme(), Theme::Dark);
}

void tst_ThemeManager::preferenceIsSaved()
{
    {
        const auto manager = makeManager();
        manager->setPreference(ThemePreference::Dark);
    }
    QCOMPARE(m_context->settings().value(QStringLiteral("appearance/theme")).toString(),
             QStringLiteral("dark"));
    const auto manager = makeManager();
    QCOMPARE(manager->preference(), ThemePreference::Dark);
    QCOMPARE(manager->theme(), Theme::Dark);
}

void tst_ThemeManager::unknownPreferenceMeansSystem()
{
    m_context->settings().setValue(QStringLiteral("appearance/theme"), QStringLiteral("purple"));
    const auto manager = makeManager();
    QCOMPARE(manager->preference(), ThemePreference::System);
}

void tst_ThemeManager::styleSheetHasNoUnresolvedTokens_data()
{
    QTest::addColumn<Theme>("theme");
    QTest::newRow("light") << Theme::Light;
    QTest::newRow("dark") << Theme::Dark;
}

void tst_ThemeManager::styleSheetHasNoUnresolvedTokens()
{
    QFETCH(Theme, theme);
    QTest::failOnWarning(QRegularExpression(QStringLiteral("Unknown color token")));
    const QString sheet = ThemeManager::styleSheet(theme);
    QVERIFY(sheet.contains(QStringLiteral("#sidebarPane")));
    QVERIFY(!sheet.contains(QStringLiteral("${")));
    QVERIFY(sheet.contains(ThemeManager::colors(theme).sidebar.name()));
    QVERIFY(sheet.contains(ThemeManager::colors(theme).accent.name()));
}

void tst_ThemeManager::colorsAreReadable_data()
{
    QTest::addColumn<Theme>("theme");
    QTest::newRow("light") << Theme::Light;
    QTest::newRow("dark") << Theme::Dark;
}

// WCAG AA: 4.5:1 for text. Part of TC-UI-020; the rest is a manual look.
void tst_ThemeManager::colorsAreReadable()
{
    QFETCH(Theme, theme);
    const notes::ui::ThemeColors& c = ThemeManager::colors(theme);
    const auto check = [](const QColor& fg, const QColor& bg, const char* what) {
        const double ratio = contrast(fg, bg);
        QVERIFY2(ratio >= 4.5,
                 qPrintable(QStringLiteral("%1: %2:1").arg(what).arg(ratio, 0, 'f', 2)));
    };
    check(c.text, c.base, "text on base");
    check(c.text, c.window, "text on window");
    check(c.text, c.sidebar, "text on sidebar");
    check(c.mutedText, c.base, "muted text on base");
    check(c.mutedText, c.sidebar, "muted text on sidebar");
    check(c.accentText, c.accent, "selected text");
    check(c.link, c.base, "link on base");
    QCOMPARE(c.accent, ThemeManager::colors(Theme::Light).accent); // shared accent
}

void tst_ThemeManager::iconsFollowTheTheme()
{
    const auto manager = makeManager();
    const QIcon trash = notes::ui::icon(notes::ui::Icon::Trash);

    manager->setPreference(ThemePreference::Light);
    QCOMPARE(dominantColor(trash.pixmap(48, 48).toImage()),
             ThemeManager::colors(Theme::Light).text);

    manager->setPreference(ThemePreference::Dark);
    // The same QIcon, so cached icons never go stale.
    QCOMPARE(dominantColor(trash.pixmap(48, 48).toImage()), ThemeManager::colors(Theme::Dark).text);
    QCOMPARE(dominantColor(trash.pixmap(QSize(48, 48), QIcon::Selected).toImage()),
             ThemeManager::colors(Theme::Dark).accentText);
    QCOMPARE(dominantColor(trash.pixmap(QSize(48, 48), QIcon::Disabled).toImage()),
             ThemeManager::colors(Theme::Dark).mutedText);
}

void tst_ThemeManager::switchFadesWithinLimit()
{
    const auto manager = makeManager();
    QWidget window;
    window.resize(400, 300);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    manager->setPreference(ThemePreference::Dark);
    auto* overlay = window.findChild<QLabel*>(QStringLiteral("themeFadeOverlay"));
    QVERIFY(overlay);
    QCOMPARE(overlay->geometry(), window.rect());
    const auto* animation = overlay->findChild<QPropertyAnimation*>();
    QVERIFY(animation);
    QVERIFY(animation->duration() <= ThemeManager::kMaxAnimationMs);
    QVERIFY(qobject_cast<QGraphicsOpacityEffect*>(animation->targetObject()));

    QTRY_VERIFY_WITH_TIMEOUT(!window.findChild<QLabel*>(QStringLiteral("themeFadeOverlay")), 2000);

    // Hidden windows and the first theme at startup are not faded.
    window.hide();
    manager->setPreference(ThemePreference::Light);
    QVERIFY(!window.findChild<QLabel*>(QStringLiteral("themeFadeOverlay")));
}

void tst_ThemeManager::appearanceMenuSwitchesTheme()
{
    notes::ui::MainWindow window(*m_context);
    auto* system = window.findChild<QAction*>(QStringLiteral("themeSystemAction"));
    auto* dark = window.findChild<QAction*>(QStringLiteral("themeDarkAction"));
    auto* light = window.findChild<QAction*>(QStringLiteral("themeLightAction"));
    QVERIFY(system && dark && light);
    QVERIFY(system->isChecked());

    dark->trigger();
    QVERIFY(dark->isChecked());
    QVERIFY(!system->isChecked());
    QCOMPARE(window.themes().theme(), Theme::Dark);
    QCOMPARE(m_context->settings().value(QStringLiteral("appearance/theme")).toString(),
             QStringLiteral("dark"));

    // A new window (next launch) shows the saved choice.
    light->trigger();
    const notes::ui::MainWindow next(*m_context);
    QVERIFY(next.findChild<QAction*>(QStringLiteral("themeLightAction"))->isChecked());
}

QTEST_MAIN(tst_ThemeManager)
#include "tst_thememanager.moc"
