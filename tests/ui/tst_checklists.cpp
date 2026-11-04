// Checklists in the window (task 3.6): Ctrl+Shift+L, clicking the checkbox,
// Enter, undo, grayed checked items, autosave, and "Move checked items to
// bottom".

#include "TestHelpers.h"
#include "editor/NoteFormat.h"
#include "editor/StyleManager.h"
#include "models/Note.h"
#include "models/SidebarRoles.h"
#include "services/FolderService.h"
#include "services/NoteService.h"
#include "ui/ActionRegistry.h"
#include "ui/EditorView.h"
#include "ui/FormatMenu.h"
#include "ui/MainWindow.h"
#include "ui/NoteTextEdit.h"

#include <QAbstractTextDocumentLayout>
#include <QAction>
#include <QMenuBar>
#include <QSettings>
#include <QTest>
#include <QTextBlock>
#include <QTextLayout>
#include <QTextList>

#include <chrono>
#include <memory>

using namespace std::chrono_literals;
using notes::editor::ParagraphStyle;
using notes::editor::StyleManager;
using notes::models::Note;
using notes::test::await;
using notes::ui::EditorView;
using notes::ui::FormatMenu;
using notes::ui::MainWindow;
using notes::ui::NoteTextEdit;
namespace roles = notes::models;

class tst_Checklists : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void shortcutMakesChecklist();
    void shortcutAgainGivesBody();
    void clickOnCheckboxToggles();
    void doubleClickTogglesTwice();
    void toggleUndoesInOneStep();
    void enterGivesUncheckedItem();
    void checkedTextIsGrayed();
    void toggleIsSaved();
    void checkedItemMovesToBottom();
    void moveSettingIsRemembered();

private:
    void openWindow();
    // Makes paragraphs 1 to 3 ("Milk", "Eggs", "Tea") a checklist.
    void makeChecklist();
    [[nodiscard]] QTextBlock block(int index) const;
    [[nodiscard]] QStringList items() const;
    // The middle of paragraph `index`'s checkbox / of its text, in the viewport.
    [[nodiscard]] QPoint checkboxOf(int index) const;
    [[nodiscard]] QPoint textOf(int index) const;
    [[nodiscard]] QAction* moveCheckedAction() const;

    std::unique_ptr<notes::test::TempDb> m_dir;
    std::unique_ptr<notes::app::AppContext> m_context;
    std::unique_ptr<MainWindow> m_window;
    NoteTextEdit* m_edit = nullptr;
    Note m_note;
};

void tst_Checklists::init()
{
    m_dir = std::make_unique<notes::test::TempDb>();
    m_context = notes::test::makeTestContext(m_dir->dirPath());
    (void)await(m_context->folders().ensureDefaultFolder());
    m_context->settings().setValue(MainWindow::kTrashNoticeShownKey, true);
    m_note = await(m_context->notes().create(roles::kAllNotesFolderId, {},
                                             QStringLiteral("Groceries\nMilk\nEggs\nTea")))
                 .value();
}

void tst_Checklists::cleanup()
{
    m_window = nullptr;
    m_context = nullptr;
    m_dir = nullptr;
}

void tst_Checklists::openWindow()
{
    m_window = std::make_unique<MainWindow>(*m_context);
    m_window->show();
    m_window->activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(m_window.get()));
    m_edit = m_window->findChild<EditorView*>(QStringLiteral("editorPane"))->textEdit();
    QTRY_COMPARE(m_edit->toPlainText(), QStringLiteral("Groceries\nMilk\nEggs\nTea"));
    m_edit->setFocus();
    QTRY_VERIFY(m_edit->hasFocus());
}

void tst_Checklists::makeChecklist()
{
    QTextCursor cursor(block(1));
    cursor.setPosition(block(3).position() + 1, QTextCursor::KeepAnchor);
    m_edit->setTextCursor(cursor);
    QTest::keyClick(m_edit, Qt::Key_L, Qt::ControlModifier | Qt::ShiftModifier);
}

QTextBlock tst_Checklists::block(int index) const
{
    return m_edit->document()->findBlockByNumber(index);
}

QStringList tst_Checklists::items() const
{
    QStringList result;
    for (QTextBlock item = m_edit->document()->begin(); item.isValid(); item = item.next()) {
        QString prefix;
        if (StyleManager::isChecklistItem(item)) {
            prefix =
                StyleManager::isChecked(item) ? QStringLiteral("[x] ") : QStringLiteral("[ ] ");
        }
        result.append(prefix + item.text());
    }
    return result;
}

QPoint tst_Checklists::checkboxOf(int index) const
{
    return m_edit->checkboxRect(block(index)).center().toPoint();
}

QPoint tst_Checklists::textOf(int index) const
{
    QTextCursor cursor(block(index));
    cursor.movePosition(QTextCursor::NextCharacter);
    return m_edit->cursorRect(cursor).center();
}

QAction* tst_Checklists::moveCheckedAction() const
{
    return m_window->findChild<QAction*>(QStringLiteral("moveCheckedToBottomAction"));
}

// TC-EDT-015
void tst_Checklists::shortcutMakesChecklist()
{
    openWindow();
    makeChecklist();
    QCOMPARE(items(), (QStringList{QStringLiteral("Groceries"), QStringLiteral("[ ] Milk"),
                                   QStringLiteral("[ ] Eggs"), QStringLiteral("[ ] Tea")}));
    QCOMPARE(StyleManager::styleOf(block(0)), ParagraphStyle::Body); // untouched
    // No Aa style is checked in a checklist; the action is enabled.
    const auto* menu = m_window->findChild<FormatMenu*>(QStringLiteral("formatMenu"));
    for (const QAction* action : menu->styleActions()) {
        QVERIFY2(!action->isChecked(), qPrintable(action->objectName()));
    }
    QVERIFY(m_window->actions().action(notes::ui::ActionId::Checklist)->isEnabled());
    // The box is drawn left of the text, in the list's margin.
    const QRectF box = m_edit->checkboxRect(block(1));
    QVERIFY(!box.isEmpty());
    QVERIFY(box.right() < textOf(1).x());
    QVERIFY(box.left() >= 0);
}

void tst_Checklists::shortcutAgainGivesBody()
{
    openWindow();
    makeChecklist();
    QTest::keyClick(m_edit, Qt::Key_L, Qt::ControlModifier | Qt::ShiftModifier);
    QCOMPARE(items(), (QStringList{QStringLiteral("Groceries"), QStringLiteral("Milk"),
                                   QStringLiteral("Eggs"), QStringLiteral("Tea")}));
    QCOMPARE(StyleManager::styleOf(block(2)), ParagraphStyle::Body);
    QCOMPARE(block(2).blockFormat().marker(), QTextBlockFormat::MarkerType::NoMarker);
}

// TC-EDT-016
void tst_Checklists::clickOnCheckboxToggles()
{
    openWindow();
    makeChecklist();
    const int caret = m_edit->textCursor().position();
    QTest::mouseClick(m_edit->viewport(), Qt::LeftButton, Qt::NoModifier, checkboxOf(2));
    QCOMPARE(items().at(2), QStringLiteral("[x] Eggs"));
    QCOMPARE(m_edit->textCursor().position(), caret); // the caret stays

    QTest::mouseClick(m_edit->viewport(), Qt::LeftButton, Qt::NoModifier, textOf(2));
    QCOMPARE(items().at(2), QStringLiteral("[x] Eggs")); // the text doesn't toggle

    QTest::mouseClick(m_edit->viewport(), Qt::LeftButton, Qt::NoModifier, checkboxOf(2));
    QCOMPARE(items().at(2), QStringLiteral("[ ] Eggs"));
}

void tst_Checklists::doubleClickTogglesTwice()
{
    openWindow();
    makeChecklist();
    m_edit->moveCursor(QTextCursor::End);
    // Qt sends press, release, press, double-click, release.
    QTest::mouseDClick(m_edit->viewport(), Qt::LeftButton, Qt::NoModifier, checkboxOf(1));
    QCOMPARE(items().at(1), QStringLiteral("[ ] Milk"));
    QVERIFY(!m_edit->textCursor().hasSelection()); // no word selected
}

// TC-EDT-017
void tst_Checklists::toggleUndoesInOneStep()
{
    openWindow();
    makeChecklist();
    QTest::mouseClick(m_edit->viewport(), Qt::LeftButton, Qt::NoModifier, checkboxOf(1));
    QCOMPARE(items().at(1), QStringLiteral("[x] Milk"));
    QTest::keyClick(m_edit, Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(items().at(1), QStringLiteral("[ ] Milk"));
    QCOMPARE(items().size(), 4);
}

void tst_Checklists::enterGivesUncheckedItem()
{
    openWindow();
    makeChecklist();
    QTest::mouseClick(m_edit->viewport(), Qt::LeftButton, Qt::NoModifier, checkboxOf(3));
    QTextCursor end(block(3));
    end.movePosition(QTextCursor::EndOfBlock);
    m_edit->setTextCursor(end);
    QTest::keyClick(m_edit, Qt::Key_Return);
    QTest::keyClicks(m_edit, QStringLiteral("Bread"));
    QCOMPARE(items().mid(3), (QStringList{QStringLiteral("[x] Tea"), QStringLiteral("[ ] Bread")}));
    QCOMPARE(block(4).textList(), block(1).textList());

    // Enter on the empty item ends the checklist, as in other lists.
    QTest::keyClick(m_edit, Qt::Key_Return);
    QTest::keyClick(m_edit, Qt::Key_Return);
    QCOMPARE(StyleManager::styleOf(block(5)), ParagraphStyle::Body);
    QCOMPARE(block(5).blockFormat().marker(), QTextBlockFormat::MarkerType::NoMarker);
}

void tst_Checklists::checkedTextIsGrayed()
{
    openWindow();
    makeChecklist();
    QVERIFY(m_edit->extraSelections().isEmpty());
    QTest::mouseClick(m_edit->viewport(), Qt::LeftButton, Qt::NoModifier, checkboxOf(2));
    const QList<QTextEdit::ExtraSelection> grayed = m_edit->extraSelections();
    QCOMPARE(grayed.size(), 1);
    QCOMPARE(grayed.first().cursor.selectedText(), QStringLiteral("Eggs"));
    QCOMPARE(grayed.first().format.foreground().color(),
             m_edit->palette().color(QPalette::PlaceholderText));
    // Drawn, not stored: the text's own format has no color.
    QCOMPARE(m_edit->noteBody().contains(QStringLiteral("\"fg\"")), false);
}

// "Toggle ... triggers autosave".
void tst_Checklists::toggleIsSaved()
{
    openWindow();
    makeChecklist();
    QTest::mouseClick(m_edit->viewport(), Qt::LeftButton, Qt::NoModifier, checkboxOf(1));
    QTRY_VERIFY_WITH_TIMEOUT(await(m_context->notes().get(m_note.id))
                                 .value()
                                 .body.contains(QStringLiteral("\"check\":true")),
                             5000);
}

// TC-EDT-018
void tst_Checklists::checkedItemMovesToBottom()
{
    openWindow();
    makeChecklist();
    moveCheckedAction()->setChecked(true);
    QTest::mouseClick(m_edit->viewport(), Qt::LeftButton, Qt::NoModifier, checkboxOf(1));
    QCOMPARE(items(), (QStringList{QStringLiteral("Groceries"), QStringLiteral("[ ] Eggs"),
                                   QStringLiteral("[ ] Tea"), QStringLiteral("[x] Milk")}));
    QTest::keyClick(m_edit, Qt::Key_Z, Qt::ControlModifier); // one step back
    QCOMPARE(items(), (QStringList{QStringLiteral("Groceries"), QStringLiteral("[ ] Milk"),
                                   QStringLiteral("[ ] Eggs"), QStringLiteral("[ ] Tea")}));

    moveCheckedAction()->setChecked(false);
    QTest::mouseClick(m_edit->viewport(), Qt::LeftButton, Qt::NoModifier, checkboxOf(1));
    QCOMPARE(items().at(1), QStringLiteral("[x] Milk")); // stays
}

void tst_Checklists::moveSettingIsRemembered()
{
    openWindow();
    QVERIFY(moveCheckedAction() != nullptr);
    QVERIFY(!moveCheckedAction()->isChecked()); // off by default
    moveCheckedAction()->setChecked(true);
    QVERIFY(m_context->settings().value(MainWindow::kMoveCheckedToBottomKey).toBool());
    m_window = nullptr;

    openWindow();
    QVERIFY(moveCheckedAction()->isChecked());
    QVERIFY(m_edit->moveCheckedToBottom());
}

QTEST_MAIN(tst_Checklists)
#include "tst_checklists.moc"
