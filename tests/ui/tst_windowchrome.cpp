// WindowChrome (task 1.9): the app draws its own header row, window buttons,
// drop shadow and border instead of the system title bar. Runs under
// QT_QPA_PLATFORM=offscreen, which can't move or resize windows, so dragging
// is covered through the double-click and hit-test paths.

#include "TestHelpers.h"
#include "ui/MainWindow.h"
#include "ui/WindowChrome.h"

#include <QApplication>
#include <QCursor>
#include <QFrame>
#include <QMenuBar>
#include <QTest>
#include <QToolBar>
#include <QToolButton>

#include <algorithm>
#include <cmath>
#include <memory>
#include <numbers>

using notes::ui::MainWindow;
using notes::ui::WindowChrome;

namespace {
// Painted content starts here...
constexpr int kMargin = WindowChrome::kShadowSize + WindowChrome::kBorderWidth;
// ...and the widgets reach a pixel further, trimmed along the rounded edge.
constexpr int kLayoutMargin = kMargin - WindowChrome::kOverhang;
} // namespace

class tst_WindowChrome : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void replacesSystemTitleBar();
    void headerHasMenusAndWindowButtons();
    void maximizeButtonTogglesAndDropsMargins();
    void minimizeAndCloseButtons();
    void doubleClickOnEmptyHeaderMaximizes();
    void doubleClickOnMenuTitleDoesNothing();
    void edgesAt_data();
    void edgesAt();
    void cornerGripsSitInsideCorners();
    void paintsShadowOutsideBorder();
    void cornersAreRounded();
    void headerAndToolbarSpacingIsBalanced();
    void windowButtonIconsAreCentered();
    void shadowFadesWithoutGaps_data();
    void shadowFadesWithoutGaps();

private:
    std::unique_ptr<MainWindow> showWindow();
    static QToolButton* button(const MainWindow& window, const char* name);

    std::unique_ptr<notes::test::TempDb> m_dir;
    std::unique_ptr<notes::app::AppContext> m_context;
};

void tst_WindowChrome::init()
{
    m_dir = std::make_unique<notes::test::TempDb>();
    m_context = notes::test::makeTestContext(m_dir->dirPath());
}

void tst_WindowChrome::cleanup()
{
    m_context = nullptr;
    m_dir = nullptr;
}

std::unique_ptr<MainWindow> tst_WindowChrome::showWindow()
{
    auto window = std::make_unique<MainWindow>(*m_context);
    window->show();
    if (!QTest::qWaitForWindowExposed(window.get())) {
        return nullptr;
    }
    return window;
}

QToolButton* tst_WindowChrome::button(const MainWindow& window, const char* name)
{
    return window.findChild<QToolButton*>(QLatin1StringView(name));
}

// TC-UI-040
void tst_WindowChrome::replacesSystemTitleBar()
{
    const auto window = showWindow();
    QVERIFY(window);
    QVERIFY(window->windowFlags().testFlag(Qt::FramelessWindowHint));
    QVERIFY(window->testAttribute(Qt::WA_TranslucentBackground));
    // The title still names the window for the dock and Alt+Tab; it just isn't drawn.
    QVERIFY(!window->windowTitle().isEmpty());
    QCOMPARE(window->contentsMargins(),
             QMargins(kLayoutMargin, kLayoutMargin, kLayoutMargin, kLayoutMargin));
}

// TC-UI-040
void tst_WindowChrome::headerHasMenusAndWindowButtons()
{
    const auto window = showWindow();
    QVERIFY(window);
    const QMenuBar* header = window->findChild<QMenuBar*>(QStringLiteral("headerBar"));
    QVERIFY(header);
    QStringList menus;
    for (const QAction* menu : header->actions()) {
        menus.append(menu->text().remove(QLatin1Char('&')));
    }
    QCOMPARE(menus,
             (QStringList{QStringLiteral("File"), QStringLiteral("Edit"), QStringLiteral("Format"),
                          QStringLiteral("View"), QStringLiteral("Help")}));

    // Window buttons at the right end of the header row, in GNOME's order.
    const QWidget* buttons = header->cornerWidget(Qt::TopRightCorner);
    QVERIFY(buttons);
    QCOMPARE(buttons->objectName(), QStringLiteral("windowButtons"));
    const QToolButton* minimize = button(*window, "windowMinimizeButton");
    const QToolButton* maximize = button(*window, "windowMaximizeButton");
    const QToolButton* close = button(*window, "windowCloseButton");
    QVERIFY(minimize && maximize && close);
    QVERIFY(minimize->x() < maximize->x() && maximize->x() < close->x());
    QVERIFY(header->width() - buttons->geometry().right() < 16);
    const QRect help = header->actionGeometry(header->actions().constLast());
    QVERIFY(buttons->geometry().left() > help.right());

    // The header row sits above the toolbar, inside the shadow margin.
    const QToolBar* toolBar = window->findChild<QToolBar*>(QStringLiteral("mainToolBar"));
    QVERIFY(toolBar);
    QVERIFY(header->geometry().bottom() < toolBar->geometry().top());
    QCOMPARE(header->geometry().topLeft(), QPoint(kLayoutMargin, kLayoutMargin));
}

// TC-UI-041
void tst_WindowChrome::maximizeButtonTogglesAndDropsMargins()
{
    const auto window = showWindow();
    QVERIFY(window);
    QToolButton* maximize = button(*window, "windowMaximizeButton");
    QCOMPARE(maximize->toolTip(), QStringLiteral("Maximize"));

    maximize->click();
    QTRY_VERIFY(window->isMaximized());
    QCOMPARE(window->contentsMargins(), QMargins()); // no shadow when maximised
    QCOMPARE(maximize->toolTip(), QStringLiteral("Restore"));
    QCOMPARE(maximize->accessibleName(), QStringLiteral("Restore"));

    maximize->click();
    QTRY_VERIFY(!window->isMaximized());
    QCOMPARE(window->contentsMargins(),
             QMargins(kLayoutMargin, kLayoutMargin, kLayoutMargin, kLayoutMargin));
    QCOMPARE(maximize->toolTip(), QStringLiteral("Maximize"));
}

// TC-UI-041
void tst_WindowChrome::minimizeAndCloseButtons()
{
    const auto window = showWindow();
    QVERIFY(window);
    button(*window, "windowMinimizeButton")->click();
    QTRY_VERIFY(window->windowState().testFlag(Qt::WindowMinimized));

    window->showNormal();
    QVERIFY(QTest::qWaitForWindowExposed(window.get()));
    button(*window, "windowCloseButton")->click();
    QTRY_VERIFY(!window->isVisible());
}

// TC-UI-042
void tst_WindowChrome::doubleClickOnEmptyHeaderMaximizes()
{
    const auto window = showWindow();
    QVERIFY(window);
    auto* header = window->findChild<QMenuBar*>(QStringLiteral("headerBar"));
    const QWidget* buttons = header->cornerWidget(Qt::TopRightCorner);
    // Between the Help menu and the window buttons.
    const QRect help = header->actionGeometry(header->actions().constLast());
    const QPoint empty((help.right() + buttons->geometry().left()) / 2, header->height() / 2);
    QVERIFY(header->actionAt(empty) == nullptr);

    QTest::mouseDClick(header, Qt::LeftButton, {}, empty);
    QTRY_VERIFY(window->isMaximized());
    QTest::mouseDClick(header, Qt::LeftButton, {}, empty);
    QTRY_VERIFY(!window->isMaximized());
}

// TC-UI-042
void tst_WindowChrome::doubleClickOnMenuTitleDoesNothing()
{
    const auto window = showWindow();
    QVERIFY(window);
    auto* header = window->findChild<QMenuBar*>(QStringLiteral("headerBar"));
    const QRect file = header->actionGeometry(header->actions().constFirst());
    QTest::mouseDClick(header, Qt::LeftButton, {}, file.center());
    QTest::qWait(50);
    QVERIFY(!window->isMaximized());
    if (QWidget* popup = QApplication::activePopupWidget()) {
        popup->close(); // the File menu may have opened; that's fine
    }
}

// A 400 x 300 window with a 21 px shadow margin: the visible window spans
// (21, 21) to (378, 278). Resizing happens within 10 px outside it (UI review:
// the whole shadow was a handle, so the cursor changed far from the window),
// diagonally within 20 px of a corner, and in 6 px grips inside the corners.
void tst_WindowChrome::edgesAt_data()
{
    QTest::addColumn<QPoint>("pos");
    QTest::addColumn<int>("margin");
    QTest::addColumn<int>("edges");

    constexpr int m = 21;
    QTest::newRow("interior") << QPoint(200, 150) << m << 0;
    QTest::newRow("outer shadow, left") << QPoint(3, 150) << m << 0;
    QTest::newRow("outer shadow, top") << QPoint(200, 10) << m << 0;
    QTest::newRow("left") << QPoint(15, 150) << m << int(Qt::LeftEdge);
    QTest::newRow("left, at the edge") << QPoint(20, 150) << m << int(Qt::LeftEdge);
    QTest::newRow("right") << QPoint(385, 150) << m << int(Qt::RightEdge);
    QTest::newRow("top") << QPoint(200, 15) << m << int(Qt::TopEdge);
    QTest::newRow("bottom") << QPoint(200, 285) << m << int(Qt::BottomEdge);
    QTest::newRow("left band near the top")
        << QPoint(15, 35) << m << int(Qt::TopEdge | Qt::LeftEdge);
    QTest::newRow("top band near the left")
        << QPoint(35, 15) << m << int(Qt::TopEdge | Qt::LeftEdge);
    QTest::newRow("top band, past the corner zone") << QPoint(50, 15) << m << int(Qt::TopEdge);
    QTest::newRow("bottom right") << QPoint(385, 285) << m << int(Qt::BottomEdge | Qt::RightEdge);
    QTest::newRow("grip inside top left") << QPoint(23, 23) << m << int(Qt::TopEdge | Qt::LeftEdge);
    QTest::newRow("grip inside bottom right")
        << QPoint(375, 275) << m << int(Qt::BottomEdge | Qt::RightEdge);
    QTest::newRow("inside, past the grip") << QPoint(27, 27) << m << 0;
    QTest::newRow("inside, along the edge") << QPoint(23, 150) << m << 0;
    QTest::newRow("maximised") << QPoint(0, 0) << 0 << 0;
}

void tst_WindowChrome::edgesAt()
{
    QFETCH(QPoint, pos);
    QFETCH(int, margin);
    QFETCH(int, edges);
    QCOMPARE(int(WindowChrome::edgesAt(pos, QSize(400, 300), margin)), edges);
}

// The rounded corners: small grips inside them resize diagonally, above the
// content that would otherwise take the mouse there; gone when maximised.
void tst_WindowChrome::cornerGripsSitInsideCorners()
{
    const auto window = showWindow();
    QVERIFY(window);
    const QList<QWidget*> grips =
        window->findChildren<QWidget*>(QStringLiteral("windowCornerGrip"));
    QCOMPARE(grips.size(), 4);
    const QRect visible =
        window->rect().marginsRemoved(QMargins(kMargin, kMargin, kMargin, kMargin));
    QList<QPoint> corners;
    for (const QWidget* grip : grips) {
        QVERIFY(grip->isVisible());
        QCOMPARE(grip->size(), QSize(WindowChrome::kCornerGrip, WindowChrome::kCornerGrip));
        QVERIFY(visible.contains(grip->geometry()));
        // Under the mouse there, nothing else: the grip is on top.
        QCOMPARE(window->childAt(grip->geometry().center()), grip);
        QVERIFY(grip->cursor().shape() == Qt::SizeFDiagCursor ||
                grip->cursor().shape() == Qt::SizeBDiagCursor);
        corners.append(grip->geometry().topLeft());
    }
    QVERIFY(corners.contains(visible.topLeft()));
    QVERIFY(corners.contains(visible.bottomRight() -
                             QPoint(WindowChrome::kCornerGrip - 1, WindowChrome::kCornerGrip - 1)));

    window->showMaximized();
    QTRY_VERIFY(window->isMaximized());
    for (const QWidget* grip : grips) {
        QVERIFY(!grip->isVisible());
    }
}

void tst_WindowChrome::paintsShadowOutsideBorder()
{
    const auto window = showWindow();
    QVERIFY(window);
    const QImage image = window->grab().toImage();
    const int midY = image.height() / 2;

    // Outermost pixel: (almost) fully transparent; nearer the window: shadow.
    QVERIFY(qAlpha(image.pixel(0, midY)) < 10);
    QVERIFY(qAlpha(image.pixel(kMargin - 3, midY)) > qAlpha(image.pixel(2, midY)));
    QVERIFY(qAlpha(image.pixel(kMargin - 3, midY)) > 0);
    // Inside the border the window is opaque.
    QCOMPARE(qAlpha(image.pixel(kMargin + 2, midY)), 255);

    window->showMaximized();
    QTRY_VERIFY(window->isMaximized());
    QTRY_COMPARE(qAlpha(window->grab().toImage().pixel(0, midY)), 255);
}

// TC-UI-040: rounded corners; square again when maximised.
void tst_WindowChrome::cornersAreRounded()
{
    const auto window = showWindow();
    QVERIFY(window);
    const QImage image = window->grab().toImage();
    const int radius = WindowChrome::kCornerRadius;
    // The content's own corner pixel lies outside the curve: erased (only a faint
    // shadow remains). On the curve's diagonal well inside, the window is opaque.
    for (const QPoint corner :
         {QPoint(kMargin, kMargin), QPoint(image.width() - kMargin - 1, kMargin),
          QPoint(kMargin, image.height() - kMargin - 1),
          QPoint(image.width() - kMargin - 1, image.height() - kMargin - 1)}) {
        QVERIFY2(qAlpha(image.pixel(corner)) < 128,
                 qPrintable(QStringLiteral("corner %1,%2 alpha %3")
                                .arg(corner.x())
                                .arg(corner.y())
                                .arg(qAlpha(image.pixel(corner)))));
    }
    QCOMPARE(qAlpha(image.pixel(kMargin + radius, kMargin + radius)), 255);

    window->showMaximized();
    QTRY_VERIFY(window->isMaximized());
    QTRY_COMPARE(qAlpha(window->grab().toImage().pixel(0, 0)), 255);
}

// UI review: the gap between the menu row and the toolbar buttons matches the
// gap below the buttons, and the window buttons are a regular size, clear of
// the top edge.
void tst_WindowChrome::headerAndToolbarSpacingIsBalanced()
{
    const auto window = showWindow();
    QVERIFY(window);
    const auto* header = window->findChild<QMenuBar*>(QStringLiteral("headerBar"));
    const auto* toolBar = window->findChild<QToolBar*>(QStringLiteral("mainToolBar"));
    const auto* group = toolBar->findChild<QFrame*>(QStringLiteral("toolGroup"));
    QVERIFY(header && toolBar && group);

    // From the bottom of the menu titles' text to the top of the buttons...
    const QRect item = header->actionGeometry(header->actions().constFirst());
    const int textBottom =
        header->mapTo(window.get(), item.center()).y() + (header->fontMetrics().height() / 2);
    const QRect groupRect(group->mapTo(window.get(), QPoint(0, 0)), group->size());
    const int above = groupRect.top() - textBottom;
    // ...and from the bottom of the buttons to the toolbar's bottom line.
    const int below = toolBar->geometry().bottom() - groupRect.bottom();
    QVERIFY2(qAbs(above - below) <= 3,
             qPrintable(QStringLiteral("above %1 px, below %2 px").arg(above).arg(below)));
    QVERIFY(below >= 6);

    for (const char* name : {"windowMinimizeButton", "windowMaximizeButton", "windowCloseButton"}) {
        const QToolButton* windowButton = button(*window, name);
        QCOMPARE(windowButton->size(), QSize(22, 22));
        QVERIFY2(windowButton->mapTo(window.get(), QPoint(0, 0)).y() - kMargin >= 6, name);
    }
}

// UI bug (seen at 125 % display scale): the window buttons' icons looked
// off-center, resampled onto half pixels. They are drawn on whole pixels now,
// centered on the circle, at the original 22 px / 14 px size.
void tst_WindowChrome::windowButtonIconsAreCentered()
{
    const auto window = showWindow();
    QVERIFY(window);
    const QImage image = window->grab().toImage();
    const qreal ratio = image.devicePixelRatio();
    for (const char* name : {"windowMinimizeButton", "windowMaximizeButton", "windowCloseButton"}) {
        const QToolButton* windowButton = button(*window, name);
        QCOMPARE(windowButton->size(), QSize(22, 22));
        QCOMPARE(windowButton->iconSize(), QSize(14, 14));

        // The icon's ink, weighted by darkness against the circle, centers on
        // the button.
        const QPointF center =
            (QPointF(windowButton->mapTo(window.get(), QPoint(0, 0))) + QPointF(11, 11)) * ratio;
        const int reach = static_cast<int>(9 * ratio);
        const int circle =
            image.pixelColor(static_cast<int>(center.x()) - reach, static_cast<int>(center.y()))
                .lightness();
        double sumX = 0;
        double sumY = 0;
        double total = 0;
        for (int y = static_cast<int>(center.y()) - reach;
             y <= static_cast<int>(center.y()) + reach; ++y) {
            for (int x = static_cast<int>(center.x()) - reach;
                 x <= static_cast<int>(center.x()) + reach; ++x) {
                const double ink = std::max(0, circle - image.pixelColor(x, y).lightness());
                sumX += ink * (x + 0.5);
                sumY += ink * (y + 0.5);
                total += ink;
            }
        }
        QVERIFY2(total > 0, name); // the icon is drawn
        QVERIFY2(std::abs((sumX / total) - center.x()) <= 0.5, name);
        QVERIFY2(std::abs((sumY / total) - center.y()) <= 0.5, name);
    }
}

// UI bug (seen at 125 % display scale): a thin light line under the window and
// at its corners. The shadow used to be pieced together around the shadow's
// offset rectangle, leaving an unpainted strip just under the window and seams
// at the corners. Moving outward from the window, the shadow must only fade.
// At fractional scales the window's last device row must be anti-aliased
// against the outside, not an opaque strip of window background.
void tst_WindowChrome::shadowFadesWithoutGaps_data()
{
    QTest::addColumn<qreal>("scale");
    QTest::newRow("100 %") << 1.0;
    QTest::newRow("125 %") << 1.25;
}

void tst_WindowChrome::shadowFadesWithoutGaps()
{
    QFETCH(qreal, scale);
    const auto window = showWindow();
    QVERIFY(window);
    QImage image((QSizeF(window->size()) * scale).toSize(), QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(scale);
    image.fill(Qt::transparent);
    window->render(&image);
    const auto alpha = [&](int x, int y) { return qAlpha(image.pixel(x, y)); };

    // Walks outward from `start` in steps of (dx, dy) to the image edge: alpha
    // may only decrease (1 level of rounding allowed).
    const auto fadesOutward = [&](QPoint start, int dx, int dy, const char* where) {
        int previous = alpha(start.x(), start.y());
        for (QPoint p = start + QPoint(dx, dy); image.rect().contains(p); p += QPoint(dx, dy)) {
            const int current = alpha(p.x(), p.y());
            QVERIFY2(current <= previous + 1,
                     qPrintable(QStringLiteral("%1: alpha rises from %2 to %3 at %4,%5")
                                    .arg(QLatin1StringView(where))
                                    .arg(previous)
                                    .arg(current)
                                    .arg(p.x())
                                    .arg(p.y())));
            previous = current;
        }
    };
    const int edge = static_cast<int>(std::ceil(kMargin * scale)); // first device px of content
    const int bottomOutside = static_cast<int>(std::ceil((window->height() - kMargin) * scale)) + 1;
    fadesOutward(QPoint(image.width() * 3 / 4, bottomOutside), 0, 1, "below the window");
    fadesOutward(QPoint(edge - 2, image.height() / 2), -1, 0, "left of the window");
    // Diagonally out of the bottom-left corner, past the rounding.
    const int corner = static_cast<int>(
        (kMargin + (WindowChrome::kCornerRadius * (1 - (1 / std::numbers::sqrt2)))) * scale);
    fadesOutward(QPoint(corner - 2, image.height() - corner + 1), -1, 1, "out of the corner");

    // Radially out of both bottom corners' curves at several angles, as seen over
    // a white desktop: nothing at the edge may be darker than the shadow just
    // outside it (UI bug: the shadow was painted over the half-erased edge
    // pixels, a dark ring ~26 levels darker than the shadow: the "thin line").
    const qreal r = WindowChrome::kCornerRadius;
    const auto overWhite = [&](QPoint p) {
        const QColor c = image.pixelColor(p);
        const qreal a = c.alphaF();
        return (a * static_cast<qreal>(qGray(c.rgb()))) + ((1 - a) * 255);
    };
    const QPointF centres[] = {
        QPointF(kMargin + r, window->height() - kMargin - r),                  // bottom-left
        QPointF(window->width() - kMargin - r, window->height() - kMargin - r) // bottom-right
    };
    for (const QPointF centre : centres) {
        const qreal sideX = centre.x() < window->width() / 2.0 ? -1 : 1;
        for (const int degrees : {0, 30, 45, 60, 90}) {
            const qreal angle = degrees * std::numbers::pi / 180;
            const QPointF dir(sideX * std::cos(angle), std::sin(angle));
            const auto at = [&](qreal d) { return ((centre + (dir * d)) * scale).toPoint(); };
            const qreal shadow = overWhite(at(r + 3));
            // From 2 px inside the edge to 1 px outside, in half-device-pixel steps.
            const int steps = static_cast<int>(std::ceil(3 * 2 * scale));
            for (int step = 0; step <= steps; ++step) {
                const qreal d = r - 2 + (step * 0.5 / scale);
                QVERIFY2(overWhite(at(d)) >= shadow - 8,
                         qPrintable(QStringLiteral("dark ring at the corner (%1°): %2 vs shadow %3")
                                        .arg(degrees)
                                        .arg(overWhite(at(d)))
                                        .arg(shadow)));
            }
        }
    }

    // The last (partial) device row of the content blends with the outside.
    const qreal bottom = (window->height() - kMargin) * scale;
    if (bottom != std::floor(bottom)) {
        const int lastRow = static_cast<int>(std::floor(bottom));
        QVERIFY2(alpha(image.width() * 3 / 4, lastRow) < 255,
                 "window background shows as an opaque strip at the bottom edge");
    }
}

QTEST_MAIN(tst_WindowChrome)
#include "tst_windowchrome.moc"
