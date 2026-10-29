// Main window layout (task 1.3): TC-UI-005 default pane widths, TC-UI-006 layout
// persistence, TC-UI-007 sidebar toggle. Runs under QT_QPA_PLATFORM=offscreen.

#include "TestHelpers.h"
#include "app/Config.h"
#include "ui/MainWindow.h"
#include "ui/dialogs/AboutDialog.h"

#include <QAction>
#include <QPlainTextEdit>
#include <QSettings>
#include <QSplitter>
#include <QTest>

#include <memory>
#include <numeric>

using notes::ui::MainWindow;

class tst_MainWindow : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void showsWithAppTitle();
    void defaultPaneWidths();
    void layoutIsRestored();
    void sidebarToggleKeepsWidth();
    void hiddenSidebarIsRestored();
    void panesCannotCollapse();
    void corruptSettingsFallBackToDefaults_data();
    void corruptSettingsFallBackToDefaults();
    void aboutListsThirdPartyLicenses();

private:
    std::unique_ptr<MainWindow> showWindow();
    static QSplitter* splitterOf(const MainWindow& window);
    static QWidget* paneOf(const MainWindow& window, const char* name);
    static void setPaneWidths(QSplitter* splitter, int sidebar, int noteList);

    std::unique_ptr<notes::test::TempDb> m_dir;
    std::unique_ptr<notes::app::AppContext> m_context;
};

void tst_MainWindow::init()
{
    m_dir = std::make_unique<notes::test::TempDb>();
    QVERIFY(m_dir->isValid());
    m_context = notes::test::makeTestContext(m_dir->dirPath());
}

void tst_MainWindow::cleanup()
{
    m_context.reset();
    m_dir.reset();
}

std::unique_ptr<MainWindow> tst_MainWindow::showWindow()
{
    auto window = std::make_unique<MainWindow>(*m_context);
    window->show();
    if (!QTest::qWaitForWindowExposed(window.get())) {
        return nullptr;
    }
    return window;
}

QSplitter* tst_MainWindow::splitterOf(const MainWindow& window)
{
    return window.findChild<QSplitter*>(QStringLiteral("mainSplitter"));
}

QWidget* tst_MainWindow::paneOf(const MainWindow& window, const char* name)
{
    return window.findChild<QWidget*>(QLatin1StringView(name));
}

// Keeps the total unchanged so QSplitter applies the widths exactly.
void tst_MainWindow::setPaneWidths(QSplitter* splitter, int sidebar, int noteList)
{
    const QList<int> sizes = splitter->sizes();
    const int total = std::accumulate(sizes.begin(), sizes.end(), 0);
    splitter->setSizes({sidebar, noteList, total - sidebar - noteList});
}

void tst_MainWindow::showsWithAppTitle()
{
    const auto window = showWindow();
    QVERIFY(window);
    QCOMPARE(window->windowTitle(), QString::fromLatin1(notes::config::kDisplayName));
}

// TC-UI-005
void tst_MainWindow::defaultPaneWidths()
{
    const auto window = showWindow();
    QVERIFY(window);
    QSplitter* splitter = splitterOf(*window);
    QVERIFY(splitter);

    const QList<int> sizes = splitter->sizes();
    QCOMPARE(sizes.size(), 3);
    QCOMPARE(sizes.at(0), MainWindow::kSidebarDefaultWidth);
    QCOMPARE(sizes.at(1), MainWindow::kNoteListDefaultWidth);
    const int rest = splitter->contentsRect().width() - MainWindow::kSidebarDefaultWidth -
                     MainWindow::kNoteListDefaultWidth - (2 * splitter->handleWidth());
    QCOMPARE(sizes.at(2), rest);

    // Growing the window only grows the editor.
    window->resize(window->width() + 200, window->height());
    QTRY_COMPARE(splitter->sizes().at(2), rest + 200);
    QCOMPARE(splitter->sizes().at(0), MainWindow::kSidebarDefaultWidth);
    QCOMPARE(splitter->sizes().at(1), MainWindow::kNoteListDefaultWidth);
}

// TC-UI-006
void tst_MainWindow::layoutIsRestored()
{
    QList<int> savedSizes;
    {
        const auto window = showWindow();
        QVERIFY(window);
        window->resize(1300, 820);
        QTRY_COMPARE(window->size(), QSize(1300, 820));
        setPaneWidths(splitterOf(*window), 260, 340);
        savedSizes = splitterOf(*window)->sizes();
        QCOMPARE(savedSizes.mid(0, 2), QList<int>({260, 340}));
    } // destroyed: layout saved

    const auto window = showWindow();
    QVERIFY(window);
    QCOMPARE(window->size(), QSize(1300, 820));
    QCOMPARE(splitterOf(*window)->sizes(), savedSizes);
}

// TC-UI-007
void tst_MainWindow::sidebarToggleKeepsWidth()
{
    const auto window = showWindow();
    QVERIFY(window);
    window->activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(window.get()));
    QSplitter* splitter = splitterOf(*window);
    QWidget* sidebar = paneOf(*window, "sidebarPane");
    QWidget* noteList = paneOf(*window, "noteListPane");
    auto* action = window->findChild<QAction*>(QStringLiteral("toggleSidebarAction"));
    QVERIFY(sidebar && noteList && action);

    setPaneWidths(splitter, 250, 310);
    const QKeySequence toggle(Qt::CTRL | Qt::ALT | Qt::Key_S);

    QTest::keySequence(window.get(), toggle);
    QVERIFY(sidebar->isHidden());
    QVERIFY(!action->isChecked());
    QCOMPARE(noteList->width(), 310); // the editor takes the freed space

    QTest::keySequence(window.get(), toggle);
    QVERIFY(!sidebar->isHidden());
    QVERIFY(action->isChecked());
    QCOMPARE(sidebar->width(), 250);
    QCOMPARE(noteList->width(), 310);
}

void tst_MainWindow::hiddenSidebarIsRestored()
{
    {
        const auto window = showWindow();
        QVERIFY(window);
        setPaneWidths(splitterOf(*window), 240, 300);
        window->findChild<QAction*>(QStringLiteral("toggleSidebarAction"))->trigger();
    }

    const auto window = showWindow();
    QVERIFY(window);
    QWidget* sidebar = paneOf(*window, "sidebarPane");
    auto* action = window->findChild<QAction*>(QStringLiteral("toggleSidebarAction"));
    QVERIFY(sidebar->isHidden());
    QVERIFY(!action->isChecked());

    action->trigger();
    QCOMPARE(sidebar->width(), 240);
}

void tst_MainWindow::panesCannotCollapse()
{
    const auto window = showWindow();
    QVERIFY(window);
    QSplitter* splitter = splitterOf(*window);
    QVERIFY(!splitter->childrenCollapsible());

    setPaneWidths(splitter, 0, 0);
    QVERIFY(splitter->sizes().at(0) >= MainWindow::kSidebarMinimumWidth);
    QVERIFY(splitter->sizes().at(1) >= MainWindow::kNoteListMinimumWidth);
    QVERIFY(splitter->sizes().at(2) >= MainWindow::kEditorMinimumWidth);
}

void tst_MainWindow::corruptSettingsFallBackToDefaults_data()
{
    QTest::addColumn<QVariant>("geometry");
    QTest::addColumn<QVariant>("paneWidths");

    QTest::newRow("garbage") << QVariant(QByteArray("not a geometry"))
                             << QVariant(QVariantList{QStringLiteral("abc"), -5});
    QTest::newRow("out of range") << QVariant(42) << QVariant(QVariantList{99999, 10});
    QTest::newRow("wrong type") << QVariant(QStringLiteral("x")) << QVariant(QStringLiteral("y"));
}

void tst_MainWindow::corruptSettingsFallBackToDefaults()
{
    QFETCH(QVariant, geometry);
    QFETCH(QVariant, paneWidths);

    // Keys as written by MainWindow.
    m_context->settings().setValue(QStringLiteral("mainWindow/geometry"), geometry);
    m_context->settings().setValue(QStringLiteral("mainWindow/paneWidths"), paneWidths);

    const auto window = showWindow();
    QVERIFY(window);
    QCOMPARE(window->size(), QSize(1100, 720));
    const QList<int> sizes = splitterOf(*window)->sizes();
    QCOMPARE(sizes.at(0), MainWindow::kSidebarDefaultWidth);
    QCOMPARE(sizes.at(1), MainWindow::kNoteListDefaultWidth);
}

void tst_MainWindow::aboutListsThirdPartyLicenses()
{
    const auto window = showWindow();
    QVERIFY(window);

    auto* about = window->findChild<QAction*>(QStringLiteral("aboutAction"));
    QVERIFY(about);
    about->trigger();

    auto* dialog = window->findChild<notes::ui::AboutDialog*>();
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
