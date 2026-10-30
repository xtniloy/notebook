// Toolbar and actions (task 1.4): TC-UI-008 toolbar order, TC-UI-009 shortcuts
// trigger their actions. Runs under QT_QPA_PLATFORM=offscreen.

#include "TestHelpers.h"
#include "ui/ActionRegistry.h"
#include "ui/MainWindow.h"
#include "ui/SearchField.h"
#include "ui/ThemeManager.h"

#include <QAction>
#include <QApplication>
#include <QFrame>
#include <QLineEdit>
#include <QSignalSpy>
#include <QTest>
#include <QToolBar>
#include <QToolButton>

#include <algorithm>
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
    void buttonsAreGrouped();
    void searchFieldIsOnTheRight();
    void searchPlaceholderCentredUntilUsed();
    void searchFieldMatchesGroupHeight();
    void layoutSameAfterThemeChange();
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
    static QList<QToolButton*> actionButtons(const QToolBar& toolBar);
    static QToolButton* buttonFor(const QToolBar& toolBar, const QString& actionName);

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

// The toolbar's action buttons (inside the toolGroup frames), left to right.
QList<QToolButton*> tst_Toolbar::actionButtons(const QToolBar& toolBar)
{
    QList<QToolButton*> buttons;
    for (QToolButton* button : toolBar.findChildren<QToolButton*>()) {
        if (button->defaultAction() != nullptr && button->parentWidget() != nullptr &&
            button->parentWidget()->objectName() == u"toolGroup") {
            buttons.append(button);
        }
    }
    std::ranges::sort(buttons, {}, [&toolBar](const QToolButton* button) {
        return button->mapTo(&toolBar, QPoint(0, 0)).x();
    });
    return buttons;
}

QToolButton* tst_Toolbar::buttonFor(const QToolBar& toolBar, const QString& actionName)
{
    for (QToolButton* button : actionButtons(toolBar)) {
        if (button->defaultAction()->objectName() == actionName) {
            return button;
        }
    }
    return nullptr;
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
    for (const QToolButton* button : actionButtons(*toolBar)) {
        const QString name = button->defaultAction()->objectName();
        QVERIFY2(button->isVisible(), qPrintable(name));
        const int x = button->mapTo(toolBar, QPoint(0, 0)).x();
        QVERIFY2(x > previousX, qPrintable(name));
        previousX = x;
        buttons.append(name);
    }
    QCOMPARE(buttons, expected);
}

// TC-UI-043: joined button groups as in macOS Notes (task 1.9).
void tst_Toolbar::buttonsAreGrouped()
{
    const auto window = showActiveWindow();
    QVERIFY(window);
    const QToolBar* toolBar = toolBarOf(*window);
    QStringList groups;
    for (const QFrame* group : toolBar->findChildren<QFrame*>(QStringLiteral("toolGroup"))) {
        QStringList names;
        for (const QToolButton* button : group->findChildren<QToolButton*>()) {
            names.append(button->defaultAction()->objectName().remove(QStringLiteral("Action")));
        }
        groups.append(names.join(QLatin1Char('|')));
    }
    QCOMPARE(groups,
             (QStringList{QStringLiteral("toggleSidebar"), QStringLiteral("listView|galleryView"),
                          QStringLiteral("deleteNote|newNote"),
                          QStringLiteral("format|checklist|insertTable|attachment|lockNote"),
                          QStringLiteral("shareExport")}));
    // The style sheet rounds the outer ends and divides the inner buttons.
    QCOMPARE(buttonFor(*toolBar, QStringLiteral("listViewAction"))->property("position").toString(),
             QStringLiteral("first"));
    QCOMPARE(
        buttonFor(*toolBar, QStringLiteral("checklistAction"))->property("position").toString(),
        QStringLiteral("middle"));
    QCOMPARE(buttonFor(*toolBar, QStringLiteral("lockNoteAction"))->property("position").toString(),
             QStringLiteral("last"));
    QCOMPARE(
        buttonFor(*toolBar, QStringLiteral("shareExportAction"))->property("position").toString(),
        QStringLiteral("only"));
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

    const QToolButton* lastButton = buttonFor(*toolBar, QStringLiteral("shareExportAction"));
    QVERIFY(lastButton);
    QVERIFY(search->geometry().left() >
            lastButton->mapTo(toolBar, QPoint(lastButton->width(), 0)).x());
    // Pushed to the right edge by the spacer, not just placed after the buttons.
    QVERIFY(toolBar->contentsRect().right() - search->geometry().right() < 16);
}

// TC-UI-043: magnifier and placeholder centred until the box is used (task 1.9).
void tst_Toolbar::searchPlaceholderCentredUntilUsed()
{
    const auto window = showActiveWindow();
    QVERIFY(window);
    auto* search = window->findChild<notes::ui::SearchField*>(QStringLiteral("searchField"));
    QVERIFY(search);
    search->clearFocus();
    QVERIFY(search->isPlaceholderCentred());
    QVERIFY(search->placeholderText().isEmpty()); // drawn centred by the field itself

    search->setFocus();
    QTRY_VERIFY(search->hasFocus());
    QVERIFY(!search->isPlaceholderCentred());
    QCOMPARE(search->placeholderText(), QStringLiteral("Search"));

    QTest::keyClicks(search, QStringLiteral("milk"));
    search->clearFocus();
    QVERIFY(!search->isPlaceholderCentred()); // text stays left-aligned
    search->clear();
    QVERIFY(search->isPlaceholderCentred());
}

// TC-UI-043: the search box lines up with the button groups.
void tst_Toolbar::searchFieldMatchesGroupHeight()
{
    const auto window = showActiveWindow();
    QVERIFY(window);
    const QToolBar* toolBar = toolBarOf(*window);
    const auto* group = toolBar->findChild<QFrame*>(QStringLiteral("toolGroup"));
    const auto* search = window->findChild<QLineEdit*>(QStringLiteral("searchField"));
    QVERIFY(group && search);
    QCOMPARE(search->height(), group->height());
    QCOMPARE(search->geometry().center().y(), group->geometry().center().y());
}

// The first theme lays the toolbar out exactly as a later theme switch does
// (QToolBar re-reads padding and spacing only on a style change).
void tst_Toolbar::layoutSameAfterThemeChange()
{
    const auto window = showActiveWindow();
    QVERIFY(window);
    const QToolBar* toolBar = toolBarOf(*window);
    const auto snapshot = [&] {
        QList<QRect> rects{toolBar->geometry()};
        for (const QToolButton* button : actionButtons(*toolBar)) {
            rects.append(QRect(button->mapTo(toolBar, QPoint(0, 0)), button->size()));
        }
        return rects;
    };
    const QList<QRect> before = snapshot();
    window->themes().setPreference(notes::ui::ThemePreference::Dark);
    window->themes().setPreference(notes::ui::ThemePreference::Light);
    QTRY_COMPARE(snapshot(), before);
}

void tst_Toolbar::buttonsShowToolTipWithShortcut()
{
    const auto window = showActiveWindow();
    QVERIFY(window);
    const QToolButton* button = buttonFor(*toolBarOf(*window), QStringLiteral("newNoteAction"));
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
