// Smoke test for the widget layer under QT_QPA_PLATFORM=offscreen.
// Layout tests TC-UI-005..007 are added in task 1.3.

#include "app/Config.h"
#include "ui/MainWindow.h"

#include <QTest>

class tst_MainWindow : public QObject
{
    Q_OBJECT

private slots:
    void showsWithAppTitle();
};

void tst_MainWindow::showsWithAppTitle()
{
    notes::ui::MainWindow window;
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QCOMPARE(window.windowTitle(), QString::fromLatin1(notes::config::kAppName));
}

QTEST_MAIN(tst_MainWindow)
#include "tst_mainwindow.moc"
