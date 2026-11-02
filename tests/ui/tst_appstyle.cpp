// AppStyle (UI review): rounded menus with a soft shadow, the panel exactly
// where Qt places the menu, right-aligned muted shortcuts, and no QMenu rules
// in the theme (they would make Qt bypass AppStyle). Runs offscreen.

#include "TestHelpers.h"
#include "ui/AppStyle.h"
#include "ui/ThemeManager.h"

#include <QApplication>
#include <QMenu>
#include <QRegularExpression>
#include <QTest>

#include <algorithm>
#include <memory>

using notes::ui::AppStyle;

class tst_AppStyle : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void appUsesAppStyle();
    void themeHasNoMenuRules();
    void menuIsRoundedWithShadow();
    void panelLandsWhereQtPlacesTheMenu();
    void shortcutsAreRightAlignedAndMuted();

private:
    // A menu like the note list's, shown at `at` (global).
    std::unique_ptr<QMenu> showMenu(const QPoint& at);

    std::unique_ptr<notes::test::TempDb> m_dir;
    std::unique_ptr<notes::app::AppContext> m_context;
    std::unique_ptr<notes::ui::ThemeManager> m_themes;
};

void tst_AppStyle::init()
{
    m_dir = std::make_unique<notes::test::TempDb>();
    m_context = notes::test::makeTestContext(m_dir->dirPath());
    m_themes = std::make_unique<notes::ui::ThemeManager>(m_context->settings());
}

void tst_AppStyle::cleanup()
{
    m_themes = nullptr;
    m_context = nullptr;
    m_dir = nullptr;
}

std::unique_ptr<QMenu> tst_AppStyle::showMenu(const QPoint& at)
{
    auto menu = std::make_unique<QMenu>();
    menu->addAction(QStringLiteral("Pin Note"));
    menu->addMenu(QStringLiteral("Move To"))->addAction(QStringLiteral("Notes"));
    menu->addSeparator();
    menu->addAction(QStringLiteral("Delete Note"))->setShortcut(QKeySequence(Qt::Key_Delete));
    menu->popup(at);
    if (!QTest::qWaitForWindowExposed(menu.get())) {
        return nullptr;
    }
    return menu;
}

void tst_AppStyle::appUsesAppStyle()
{
    // With a style sheet, style() is Qt's wrapper; AppStyle is the base it uses.
    const QString sheet = qApp->styleSheet();
    qApp->setStyleSheet({});
    QVERIFY(qobject_cast<AppStyle*>(QApplication::style()) != nullptr);
    qApp->setStyleSheet(sheet);
}

void tst_AppStyle::themeHasNoMenuRules()
{
    const QRegularExpression menuRule(QStringLiteral(R"(\bQMenu\s*(\{|::|:|#|\.))"));
    for (const auto theme : {notes::ui::Theme::Light, notes::ui::Theme::Dark}) {
        const QString sheet = notes::ui::ThemeManager::styleSheet(theme);
        QVERIFY2(!menuRule.match(sheet).hasMatch(), qPrintable(menuRule.match(sheet).captured()));
    }
}

void tst_AppStyle::menuIsRoundedWithShadow()
{
    const auto menu = showMenu(QPoint(200, 200));
    QVERIFY(menu);
    QVERIFY(menu->testAttribute(Qt::WA_TranslucentBackground));
    const QImage image = menu->grab().toImage();
    const qreal ratio = image.devicePixelRatio();
    const auto pixel = [&](int x, int y) {
        return image.pixelColor(static_cast<int>(x * ratio), static_cast<int>(y * ratio));
    };
    constexpr int m = AppStyle::kShadowMargin;
    const int midY = menu->height() / 2;

    // The outermost pixels are clear; the shadow darkens toward the panel.
    QVERIFY(pixel(0, midY).alpha() < 10);
    QVERIFY(pixel(m - 2, midY).alpha() > pixel(1, midY).alpha());
    // The panel's very corner is outside the rounding: shadow at most, no panel.
    QVERIFY(pixel(m, m).alpha() < 128);
    // Inside: the base color, opaque.
    const QColor base = menu->palette().color(QPalette::Base);
    const QColor inside = pixel(menu->width() / 2, m + 2);
    QCOMPARE(inside.alpha(), 255);
    QCOMPARE(inside.rgb(), base.rgb());
}

void tst_AppStyle::panelLandsWhereQtPlacesTheMenu()
{
    const QPoint at(300, 250);
    const auto menu = showMenu(at);
    QVERIFY(menu);
    // The window starts a shadow margin earlier, so the panel starts at `at`.
    QCOMPARE(menu->pos(), at - QPoint(AppStyle::kShadowMargin, AppStyle::kShadowMargin));
}

void tst_AppStyle::shortcutsAreRightAlignedAndMuted()
{
    const auto menu = showMenu(QPoint(200, 200));
    QVERIFY(menu);
    QAction* remove = menu->actions().constLast();
    const QRect row = menu->actionGeometry(remove);
    const QImage image = menu->grab(row).toImage();
    const QColor base = menu->palette().color(QPalette::Base);
    const auto isInk = [&](const QColor& c) { return qAbs(c.lightness() - base.lightness()) > 40; };
    // The right-most ink in the row: the shortcut, near the right edge.
    int rightMost = -1;
    for (int x = 0; x < image.width(); ++x) {
        for (int y = 0; y < image.height(); ++y) {
            if (isInk(image.pixelColor(x, y))) {
                rightMost = x;
            }
        }
    }
    const qreal ratio = image.devicePixelRatio();
    // Its darkest pixel, within the shortcut's width from its right end.
    const int shortcutWidth =
        static_cast<int>(menu->fontMetrics().horizontalAdvance(
                             remove->shortcut().toString(QKeySequence::NativeText)) *
                         ratio);
    QColor darkest = base;
    for (int x = std::max(0, rightMost - shortcutWidth + 1); x <= rightMost; ++x) {
        for (int y = 0; y < image.height(); ++y) {
            const QColor c = image.pixelColor(x, y);
            if (isInk(c) && c.lightness() < darkest.lightness()) {
                darkest = c;
            }
        }
    }
    const int right = static_cast<int>(rightMost / ratio);
    QVERIFY2(right >= row.width() - AppStyle::kItemInset - AppStyle::kTextPadding - 3,
             qPrintable(QStringLiteral("ink ends at %1 of %2").arg(right).arg(row.width())));
    // Muted: not as dark as the text.
    QVERIFY(darkest.lightness() > menu->palette().color(QPalette::Text).lightness() + 20);
}

QTEST_MAIN(tst_AppStyle)
#include "tst_appstyle.moc"
