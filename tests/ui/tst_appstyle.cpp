// AppStyle (UI review): rounded menus with a soft shadow, the panel exactly
// where Qt places the menu, right-aligned muted shortcuts; thin scroll bars
// whose track shows on hover; and no QMenu or QScrollBar rules in the theme
// (they would make Qt bypass AppStyle). Runs offscreen.

#include "TestHelpers.h"
#include "ui/AppStyle.h"
#include "ui/Icons.h"
#include "ui/ThemeManager.h"

#include <QApplication>
#include <QHoverEvent>
#include <QLineEdit>
#include <QMenu>
#include <QRegularExpression>
#include <QScrollBar>
#include <QStyleOptionSlider>
#include <QTest>
#include <QTextEdit>

#include <algorithm>
#include <array>
#include <memory>
#include <utility>

using notes::ui::AppStyle;

class tst_AppStyle : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void appUsesAppStyle();
    void themeHasNoMenuOrScrollBarRules();
    void menuIsRoundedWithShadow();
    void panelLandsWhereQtPlacesTheMenu();
    void shortcutsAreRightAlignedAndMuted();
    void textMenusUseLineIcons();
    void scrollBarIsThinWithoutArrows();
    void scrollBarTrackShowsOnHover_data();
    void scrollBarTrackShowsOnHover();
    void scrollHandleWidensOnHover();

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

void tst_AppStyle::themeHasNoMenuOrScrollBarRules()
{
    const QRegularExpression menuRule(QStringLiteral(R"(\b(QMenu|QScrollBar)\s*(\{|::|:|#|\.|,))"));
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

void tst_AppStyle::scrollBarIsThinWithoutArrows()
{
    QScrollBar bar(Qt::Vertical);
    bar.setRange(0, 100);
    bar.setPageStep(20);
    bar.resize(bar.sizeHint().width(), 300);
    QCOMPARE(bar.sizeHint().width(), AppStyle::kScrollBarExtent);

    QStyleOptionSlider option;
    option.initFrom(&bar);
    option.orientation = Qt::Vertical;
    option.minimum = 0;
    option.maximum = 100;
    option.pageStep = 20;
    option.sliderPosition = 0;
    const QStyle* style = QApplication::style();
    for (const QStyle::SubControl arrow :
         {QStyle::SC_ScrollBarAddLine, QStyle::SC_ScrollBarSubLine}) {
        QVERIFY(style->subControlRect(QStyle::CC_ScrollBar, &option, arrow, &bar).isEmpty());
    }
    const QRect groove =
        style->subControlRect(QStyle::CC_ScrollBar, &option, QStyle::SC_ScrollBarGroove, &bar);
    const QRect slider =
        style->subControlRect(QStyle::CC_ScrollBar, &option, QStyle::SC_ScrollBarSlider, &bar);
    QVERIFY(groove.contains(slider));
    QCOMPARE(slider.top(), groove.top());
    // Proportional: a page of 20 in 100 + 20.
    QCOMPARE(slider.height(), groove.height() * 20 / 120);
}

void tst_AppStyle::scrollBarTrackShowsOnHover_data()
{
    QTest::addColumn<notes::ui::Theme>("theme");
    QTest::newRow("light") << notes::ui::Theme::Light;
    QTest::newRow("dark") << notes::ui::Theme::Dark;
}

void tst_AppStyle::scrollBarTrackShowsOnHover()
{
    QFETCH(notes::ui::Theme, theme);
    m_themes->setPreference(theme == notes::ui::Theme::Dark ? notes::ui::ThemePreference::Dark
                                                            : notes::ui::ThemePreference::Light);
    const notes::ui::ThemeColors& colors = notes::ui::ThemeManager::colors(theme);

    QScrollBar bar(Qt::Vertical);
    bar.setRange(0, 100);
    bar.setPageStep(20);
    bar.setValue(0);
    bar.resize(AppStyle::kScrollBarExtent, 300);
    bar.show();
    QVERIFY(QTest::qWaitForWindowExposed(&bar));
    // Below the handle, in the middle of the bar.
    const auto sample = [&] {
        const QImage image = bar.grab().toImage();
        const qreal ratio = image.devicePixelRatio();
        return image.pixelColor(static_cast<int>((AppStyle::kScrollBarExtent / 2.0) * ratio),
                                static_cast<int>(250 * ratio));
    };
    // Offscreen, a new window can start out "under the mouse": make sure it isn't.
    QHoverEvent leave(QEvent::HoverLeave, QPointF(-1, -1), QPointF(-1, -1), QPointF(6, 250));
    QCoreApplication::sendEvent(&bar, &leave);
    bar.setAttribute(Qt::WA_UnderMouse, false);
    bar.repaint();
    QVERIFY(sample().rgb() != colors.scrollTrack.rgb()); // no track until hovered

    QHoverEvent enter(QEvent::HoverEnter, QPointF(6, 250), bar.mapToGlobal(QPointF(6, 250)),
                      QPointF(-1, -1));
    QCoreApplication::sendEvent(&bar, &enter);
    bar.setAttribute(Qt::WA_UnderMouse, true);
    bar.repaint();
    QCOMPARE(sample().rgb(), colors.scrollTrack.rgb());
}

// UI review: on hover the handle widens to the track's full width; at rest it
// is inset from the sides.
void tst_AppStyle::scrollHandleWidensOnHover()
{
    m_themes->setPreference(notes::ui::ThemePreference::Light);
    const notes::ui::ThemeColors& colors = notes::ui::ThemeManager::colors(notes::ui::Theme::Light);
    QScrollBar bar(Qt::Vertical);
    bar.setRange(0, 100);
    bar.setPageStep(20);
    bar.setValue(0);
    bar.resize(AppStyle::kScrollBarExtent, 300);
    bar.show();
    QVERIFY(QTest::qWaitForWindowExposed(&bar));
    // Near the bar's left edge, halfway down the handle.
    const auto edgePixel = [&] {
        const QImage image = bar.grab().toImage();
        const qreal ratio = image.devicePixelRatio();
        return image.pixelColor(static_cast<int>(1 * ratio), static_cast<int>(25 * ratio));
    };
    QHoverEvent leave(QEvent::HoverLeave, QPointF(-1, -1), QPointF(-1, -1), QPointF(6, 25));
    QCoreApplication::sendEvent(&bar, &leave);
    bar.setAttribute(Qt::WA_UnderMouse, false);
    bar.repaint();
    QVERIFY(edgePixel().rgb() != colors.scrollHandle.rgb()); // inset at rest

    QHoverEvent enter(QEvent::HoverEnter, QPointF(6, 25), bar.mapToGlobal(QPointF(6, 25)),
                      QPointF(-1, -1));
    QCoreApplication::sendEvent(&bar, &enter);
    bar.setAttribute(Qt::WA_UnderMouse, true);
    bar.repaint();
    QCOMPARE(edgePixel().rgb(), colors.scrollHandleHover.rgb()); // full width on hover
}

// Qt's text menus (the note editor's right-click menu, the search field's)
// show the app's line icons, not the desktop theme's colorful ones.
void tst_AppStyle::textMenusUseLineIcons()
{
    using notes::ui::Icon;
    const auto sameIcon = [](const QIcon& shown, Icon expected) {
        constexpr QSize kSize(16, 16);
        return shown.pixmap(kSize).toImage() == notes::ui::icon(expected).pixmap(kSize).toImage();
    };

    QTextEdit edit;
    QLineEdit line;
    for (QWidget* widget : {static_cast<QWidget*>(&edit), static_cast<QWidget*>(&line)}) {
        auto* textEdit = qobject_cast<QTextEdit*>(widget);
        const std::unique_ptr<QMenu> menu(
            textEdit != nullptr ? textEdit->createStandardContextMenu()
                                : qobject_cast<QLineEdit*>(widget)->createStandardContextMenu());
        menu->ensurePolished();
        const std::array<std::pair<const char*, Icon>, 7> expected{{
            {"edit-undo", Icon::Undo},
            {"edit-redo", Icon::Redo},
            {"edit-cut", Icon::Cut},
            {"edit-copy", Icon::Copy},
            {"edit-paste", Icon::Paste},
            {"edit-delete", Icon::DeleteText},
            {"select-all", Icon::SelectAll},
        }};
        for (const auto& [name, icon] : expected) {
            const auto* action = menu->findChild<QAction*>(QLatin1StringView(name));
            if (action == nullptr) {
                QFAIL(name);
            }
            QVERIFY2(sameIcon(action->icon(), icon), name);
        }
    }
}

QTEST_MAIN(tst_AppStyle)
#include "tst_appstyle.moc"
