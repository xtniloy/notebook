// Toolbar and actions (task 1.4): TC-UI-008 toolbar order, TC-UI-009 shortcuts
// trigger their actions. Runs under QT_QPA_PLATFORM=offscreen.

#include "TestHelpers.h"
#include "ui/ActionRegistry.h"
#include "ui/MainWindow.h"

#include <QAction>
#include <QApplication>
#include <QLineEdit>
#include <QSignalSpy>
#include <QTest>
#include <QToolBar>
#include <QToolButton>

#include <memory>

using notes::ui::ActionId;
using notes::ui::MainWindow;

class tst_Toolbar : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void actionOrderMatchesPlan();
    void searchFieldIsOnTheRight();
    void buttonsShowToolTipWithShortcut();
    void shortcutTriggersAction_data();
    void shortcutTriggersAction();
    void disabledActionIgnoresShortcut();
    void searchAllFocusesSearchField();
    void deleteKeyInSearchFieldEditsText();

private:
    std::unique_ptr<MainWindow> showActiveWindow();
    static QToolBar* toolBarOf(const MainWindow& window);
    static QAction* actionOf(const MainWindow& window, const QString& name);

    std::unique_ptr<notes::test::TempDb> m_dir;
    std::unique_ptr<notes::app::AppContext> m_context;
};

void tst_Toolbar::init()
{
    m_dir = std::make_unique<notes::test::TempDb>();
    QVERIFY(m_dir->isValid());
    m_context = notes::test::makeTestContext(m_dir->dirPath());
}

void tst_Toolbar::cleanup()
{
    m_context.reset();
    m_dir.reset();
}

std::unique_ptr<MainWindow> tst_Toolbar::showActiveWindow()
{
    auto window = std::make_unique<MainWindow>(*m_context);
    window->show();
    window->activateWindow();
    if (!QTest::qWaitForWindowActive(window.get())) {
        return nullptr;
    }
    return window;
}

QToolBar* tst_Toolbar::toolBarOf(const MainWindow& window)
{
    return window.findChild<QToolBar*>(QStringLiteral("mainToolBar"));
}

QAction* tst_Toolbar::actionOf(const MainWindow& window, const QString& name)
{
    return window.findChild<QAction*>(name);
}

// TC-UI-008
void tst_Toolbar::actionOrderMatchesPlan()
{
    const auto window = showActiveWindow();
    QVERIFY(window);
    const QToolBar* toolBar = toolBarOf(*window);
    QVERIFY(toolBar);

    // Plan §3.2, left to right. The search box follows (searchFieldIsOnTheRight).
    const QStringList expected{
        QStringLiteral("toggleSidebarAction"), QStringLiteral("listViewAction"),
        QStringLiteral("galleryViewAction"),   QStringLiteral("deleteNoteAction"),
        QStringLiteral("newNoteAction"),       QStringLiteral("formatAction"),
        QStringLiteral("checklistAction"),     QStringLiteral("insertTableAction"),
        QStringLiteral("attachmentAction"),    QStringLiteral("lockNoteAction"),
        QStringLiteral("shareExportAction"),
    };

    QStringList buttons;
    int previousX = -1;
    for (QAction* action : toolBar->actions()) {
        if (action->isSeparator() || toolBar->widgetForAction(action) == nullptr) {
            continue;
        }
        const auto* button = qobject_cast<QToolButton*>(toolBar->widgetForAction(action));
        if (button == nullptr) {
            continue; // spacer and search field
        }
        QVERIFY2(button->isVisible(), qPrintable(action->objectName()));
        QVERIFY2(button->x() > previousX, qPrintable(action->objectName()));
        previousX = button->x();
        buttons.append(action->objectName());
    }
    QCOMPARE(buttons, expected);
}

void tst_Toolbar::searchFieldIsOnTheRight()
{
    const auto window = showActiveWindow();
    QVERIFY(window);
    const QToolBar* toolBar = toolBarOf(*window);
    auto* search = window->findChild<QLineEdit*>(QStringLiteral("searchField"));
    QVERIFY(toolBar && search);
    QVERIFY(search->isVisible());
    QCOMPARE(search->parentWidget(), toolBar);

    const QWidget* lastButton =
        toolBar->widgetForAction(actionOf(*window, QStringLiteral("shareExportAction")));
    QVERIFY(lastButton);
    QVERIFY(search->geometry().left() > lastButton->geometry().right());
    // Pushed to the right edge by the spacer, not just placed after the buttons.
    QVERIFY(toolBar->contentsRect().right() - search->geometry().right() < 16);
}

void tst_Toolbar::buttonsShowToolTipWithShortcut()
{
    const auto window = showActiveWindow();
    QVERIFY(window);
    const QToolBar* toolBar = toolBarOf(*window);
    const auto* button = qobject_cast<QToolButton*>(
        toolBar->widgetForAction(actionOf(*window, QStringLiteral("newNoteAction"))));
    QVERIFY(button);
    QCOMPARE(button->toolTip(), QStringLiteral("New Note (Ctrl+N)"));
}

// TC-UI-009: the shortcut table from Plan §3.2.
void tst_Toolbar::shortcutTriggersAction_data()
{
    QTest::addColumn<QString>("action");
    QTest::addColumn<QKeySequence>("shortcut");

    const auto row = [](const char* name, const QKeySequence& shortcut) {
        QTest::newRow(name) << QString::fromLatin1(name) << shortcut;
    };
    row("toggleSidebarAction", QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_S));
    row("listViewAction", QKeySequence(Qt::CTRL | Qt::Key_1));
    row("galleryViewAction", QKeySequence(Qt::CTRL | Qt::Key_2));
    row("deleteNoteAction", QKeySequence(Qt::Key_Delete));
    row("newNoteAction", QKeySequence(Qt::CTRL | Qt::Key_N));
    row("checklistAction", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_L));
    row("insertTableAction", QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_T));
    row("attachmentAction", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_A));
    row("findInNoteAction", QKeySequence(Qt::CTRL | Qt::Key_F));
    row("searchAllNotesAction", QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_F));
}

void tst_Toolbar::shortcutTriggersAction()
{
    QFETCH(QString, action);
    QFETCH(QKeySequence, shortcut);

    const auto window = showActiveWindow();
    QVERIFY(window);
    QAction* target = actionOf(*window, action);
    QVERIFY(target);
    // Tests the key binding, not the feature: unimplemented actions start disabled.
    target->setEnabled(true);
    // A focused text field would claim editing keys such as Delete.
    if (QWidget* focused = QApplication::focusWidget()) {
        focused->clearFocus();
    }

    const QSignalSpy spy(target, &QAction::triggered);
    QTest::keySequence(window.get(), shortcut);
    QCOMPARE(spy.count(), 1);
}

void tst_Toolbar::disabledActionIgnoresShortcut()
{
    const auto window = showActiveWindow();
    QVERIFY(window);
    QAction* newNote = window->actions().action(ActionId::NewNote);
    QVERIFY(!newNote->isEnabled());

    const QSignalSpy spy(newNote, &QAction::triggered);
    QTest::keySequence(window.get(), QKeySequence(Qt::CTRL | Qt::Key_N));
    QCOMPARE(spy.count(), 0);
}

void tst_Toolbar::searchAllFocusesSearchField()
{
    const auto window = showActiveWindow();
    QVERIFY(window);
    auto* search = window->findChild<QLineEdit*>(QStringLiteral("searchField"));
    QVERIFY(search);
    search->setText(QStringLiteral("old query"));
    search->clearFocus();

    QTest::keySequence(window.get(), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_F));
    QTRY_VERIFY(search->hasFocus());
    QCOMPARE(search->selectedText(), QStringLiteral("old query"));
}

void tst_Toolbar::deleteKeyInSearchFieldEditsText()
{
    const auto window = showActiveWindow();
    QVERIFY(window);
    auto* search = window->findChild<QLineEdit*>(QStringLiteral("searchField"));
    QAction* deleteNote = window->actions().action(ActionId::DeleteNote);
    deleteNote->setEnabled(true);
    search->setText(QStringLiteral("abc"));
    search->setFocus();
    search->setCursorPosition(0);
    QTRY_VERIFY(search->hasFocus());

    const QSignalSpy spy(deleteNote, &QAction::triggered);
    QTest::keyClick(search, Qt::Key_Delete);
    QCOMPARE(spy.count(), 0);
    QCOMPARE(search->text(), QStringLiteral("bc"));
}

QTEST_MAIN(tst_Toolbar)
#include "tst_toolbar.moc"
