// Smoke tests for the widget layer under QT_QPA_PLATFORM=offscreen.
// Layout tests TC-UI-005..007 are added in task 1.3.

#include "TestHelpers.h"
#include "app/Config.h"
#include "ui/MainWindow.h"
#include "ui/dialogs/AboutDialog.h"

#include <QAction>
#include <QPlainTextEdit>
#include <QTest>

class tst_MainWindow : public QObject
{
    Q_OBJECT

private slots:
    void showsWithAppTitle();
    void aboutListsThirdPartyLicenses();
};

void tst_MainWindow::showsWithAppTitle()
{
    const notes::test::TempDb dir;
    const auto context = notes::test::makeTestContext(dir.dirPath());
    notes::ui::MainWindow window(*context);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QCOMPARE(window.windowTitle(), QString::fromLatin1(notes::config::kAppName));
}

void tst_MainWindow::aboutListsThirdPartyLicenses()
{
    const notes::test::TempDb dir;
    const auto context = notes::test::makeTestContext(dir.dirPath());
    notes::ui::MainWindow window(*context);

    auto* about = window.findChild<QAction*>(QStringLiteral("aboutAction"));
    QVERIFY(about);
    about->trigger();

    auto* dialog = window.findChild<notes::ui::AboutDialog*>();
    QVERIFY(dialog);
    QVERIFY(dialog->isVisible());
    const auto* licenses = dialog->findChild<QPlainTextEdit*>(QStringLiteral("licenses"));
    QVERIFY(licenses);

    const QString text = licenses->toPlainText();
    for (const char* expected : {"Inter (font)", "JetBrains Mono (font)", "Lucide (icons)",
                                 "SIL Open Font License", "ISC License"}) {
        QVERIFY2(text.contains(QLatin1StringView(expected)), expected);
    }
}

QTEST_MAIN(tst_MainWindow)
#include "tst_mainwindow.moc"
