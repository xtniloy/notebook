// WindowChrome (task 1.9): the app draws its own header row, window buttons,
// drop shadow and border instead of the system title bar. Runs under
// QT_QPA_PLATFORM=offscreen, which can't move or resize windows, so dragging
// is covered through the double-click and hit-test paths.

#include "TestHelpers.h"
#include "ui/MainWindow.h"
#include "ui/WindowChrome.h"

#include <QApplication>
#include <QFrame>
#include <QMenuBar>
#include <QTest>
#include <QToolBar>
#include <QToolButton>

#include <memory>

using notes::ui::MainWindow;
using notes::ui::WindowChrome;

namespace {
constexpr int kMargin = WindowChrome::kShadowSize + WindowChrome::kBorderWidth;
}

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
    void paintsShadowOutsideBorder();
    void cornersAreRounded();
    void headerAndToolbarSpacingIsBalanced();

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
    QCOMPARE(window->contentsMargins(), QMargins(kMargin, kMargin, kMargin, kMargin));
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
    QCOMPARE(header->geometry().topLeft(), QPoint(kMargin, kMargin));
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
    QCOMPARE(window->contentsMargins(), QMargins(kMargin, kMargin, kMargin, kMargin));
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

void tst_WindowChrome::edgesAt_data()
{
    QTest::addColumn<QPoint>("pos");
    QTest::addColumn<int>("margin");
    QTest::addColumn<int>("edges");

    QTest::newRow("interior") << QPoint(200, 150) << 17 << 0;
    QTest::newRow("left") << QPoint(3, 150) << 17 << int(Qt::LeftEdge);
    QTest::newRow("right") << QPoint(390, 150) << 17 << int(Qt::RightEdge);
    QTest::newRow("top") << QPoint(200, 0) << 17 << int(Qt::TopEdge);
    QTest::newRow("bottom") << QPoint(200, 299) << 17 << int(Qt::BottomEdge);
    QTest::newRow("top left") << QPoint(2, 2) << 17 << int(Qt::TopEdge | Qt::LeftEdge);
    QTest::newRow("bottom right") << QPoint(399, 299) << 17 << int(Qt::BottomEdge | Qt::RightEdge);
    QTest::newRow("just inside") << QPoint(17, 17) << 17 << 0;
    QTest::newRow("maximised") << QPoint(0, 0) << 0 << 0;
}

void tst_WindowChrome::edgesAt()
{
    QFETCH(QPoint, pos);
    QFETCH(int, margin);
    QFETCH(int, edges);
    QCOMPARE(int(WindowChrome::edgesAt(pos, QSize(400, 300), margin)), edges);
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

QTEST_MAIN(tst_WindowChrome)
#include "tst_windowchrome.moc"
