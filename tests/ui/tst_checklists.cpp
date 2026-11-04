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
#include "ui/NoteListView.h"
#include "ui/NoteTextEdit.h"

#include <QAbstractTextDocumentLayout>
#include <QAction>
#include <QMenuBar>
#include <QSettings>
#include <QTest>
#include <QTextBlock>
#include <QTextLayout>
#include <QTextList>

#include <algorithm>
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
    void hoverThenClickTogglesOnce();
    void clickChangesOnlyTheClickedItem();
    void clicksAddNoParagraphs();
    void dragOffTheCheckboxCancels();
    void toggleUndoesInOneStep();
    void enterGivesUncheckedItem();
    void enterOnEmptyItemLeavesTheChecklist();
    void checkedTextIsGrayed();
    void toggleIsSaved();
    void grayIsNotUpdatedInsideDocumentSignals();
    void switchingFromCheckedItemsIsSafe();
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
    QVERIFY(!block(2).blockFormat().hasProperty(notes::editor::NoteFormat::kCheckedProperty));
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

// A real click comes after the pointer moved there. (Qt's own checkbox
// handling, for its block markers, toggled again on the release.)
void tst_Checklists::hoverThenClickTogglesOnce()
{
    openWindow();
    makeChecklist();
    for (const QString& expected :
         {QStringLiteral("[x] Eggs"), QStringLiteral("[ ] Eggs"), QStringLiteral("[x] Eggs")}) {
        QTest::mouseMove(m_edit->viewport(), checkboxOf(2));
        QTest::mouseClick(m_edit->viewport(), Qt::LeftButton, Qt::NoModifier, checkboxOf(2));
        QCOMPARE(items().at(2), expected);
    }
}

// The caret is in another item (as after typing it): clicking a checkbox
// changes that item only.
void tst_Checklists::clickChangesOnlyTheClickedItem()
{
    openWindow();
    makeChecklist();
    for (const int caretIn : {2, 3}) {
        QTextCursor caret(block(caretIn));
        caret.movePosition(QTextCursor::EndOfBlock);
        m_edit->setTextCursor(caret);
        QTest::mouseMove(m_edit->viewport(), checkboxOf(1));
        QTest::mouseClick(m_edit->viewport(), Qt::LeftButton, Qt::NoModifier, checkboxOf(1));
        QTest::mouseMove(m_edit->viewport(), checkboxOf(3));
        QTest::mouseClick(m_edit->viewport(), Qt::LeftButton, Qt::NoModifier, checkboxOf(3));
    }
    // Two clicks each: back to unchecked, and Eggs never touched.
    QCOMPARE(items(), (QStringList{QStringLiteral("Groceries"), QStringLiteral("[ ] Milk"),
                                   QStringLiteral("[ ] Eggs"), QStringLiteral("[ ] Tea")}));
}

void tst_Checklists::clicksAddNoParagraphs()
{
    openWindow();
    makeChecklist();
    QTextCursor end(block(3));
    end.movePosition(QTextCursor::EndOfBlock);
    m_edit->setTextCursor(end);
    QTest::keyClick(m_edit, Qt::Key_Return); // an empty item, the caret in it
    QCOMPARE(m_edit->document()->blockCount(), 5);
    moveCheckedAction()->setChecked(true);
    for (int i = 0; i < 6; ++i) {
        const int item = 1 + (i % 4);
        QTest::mouseMove(m_edit->viewport(), checkboxOf(item));
        QTest::mouseClick(m_edit->viewport(), Qt::LeftButton, Qt::NoModifier, checkboxOf(item));
        QCOMPARE(m_edit->document()->blockCount(), 5);
    }
    QStringList texts;
    for (const QString& item : items()) {
        texts.append(item.mid(item.indexOf(QLatin1Char(']')) + 1).trimmed());
    }
    texts.sort();
    QCOMPARE(texts, (QStringList{QString(), QStringLiteral("Eggs"), QStringLiteral("Groceries"),
                                 QStringLiteral("Milk"), QStringLiteral("Tea")}));
}

// Pressing on a checkbox and letting go elsewhere doesn't toggle it.
void tst_Checklists::dragOffTheCheckboxCancels()
{
    openWindow();
    makeChecklist();
    QTest::mousePress(m_edit->viewport(), Qt::LeftButton, Qt::NoModifier, checkboxOf(1));
    QTest::mouseRelease(m_edit->viewport(), Qt::LeftButton, Qt::NoModifier, textOf(2));
    QCOMPARE(items().at(1), QStringLiteral("[ ] Milk"));
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
    QVERIFY(!block(5).blockFormat().hasProperty(notes::editor::NoteFormat::kCheckedProperty));
}

// Enter on an empty item: out of the checklist, the item a plain line where
// the caret stays, and the next Enter a new line, at the end and in the
// middle (where the items below stay checklist items).
void tst_Checklists::enterOnEmptyItemLeavesTheChecklist()
{
    openWindow();
    makeChecklist();
    QTextCursor end(block(3));
    end.movePosition(QTextCursor::EndOfBlock);
    m_edit->setTextCursor(end);
    QTest::keyClick(m_edit, Qt::Key_Return); // a new, empty item
    QCOMPARE(items().last(), QStringLiteral("[ ] "));
    QTest::keyClick(m_edit, Qt::Key_Return); // out: no second empty item
    QCOMPARE(items(),
             (QStringList{QStringLiteral("Groceries"), QStringLiteral("[ ] Milk"),
                          QStringLiteral("[ ] Eggs"), QStringLiteral("[ ] Tea"), QString()}));
    QCOMPARE(m_edit->textCursor().blockNumber(), 4);
    QTest::keyClick(m_edit, Qt::Key_Return); // a new line at once
    QCOMPARE(items().size(), 6);
    QCOMPARE(m_edit->textCursor().blockNumber(), 5);

    QTextCursor milk(block(1));
    milk.movePosition(QTextCursor::EndOfBlock);
    m_edit->setTextCursor(milk);
    QTest::keyClick(m_edit, Qt::Key_Return);
    QTest::keyClick(m_edit, Qt::Key_Return);
    QCOMPARE(items().mid(0, 5),
             (QStringList{QStringLiteral("Groceries"), QStringLiteral("[ ] Milk"), QString(),
                          QStringLiteral("[ ] Eggs"), QStringLiteral("[ ] Tea")}));
    QTest::keyClick(m_edit, Qt::Key_Return);
    QCOMPARE(items().mid(0, 6),
             (QStringList{QStringLiteral("Groceries"), QStringLiteral("[ ] Milk"), QString(),
                          QString(), QStringLiteral("[ ] Eggs"), QStringLiteral("[ ] Tea")}));
    QCOMPARE(m_edit->textCursor().blockNumber(), 3);
}

void tst_Checklists::checkedTextIsGrayed()
{
    openWindow();
    makeChecklist();
    QVERIFY(m_edit->extraSelections().isEmpty());
    QTest::mouseClick(m_edit->viewport(), Qt::LeftButton, Qt::NoModifier, checkboxOf(2));
    // Updated from the event loop, not inside the document's signals.
    QTRY_COMPARE(m_edit->extraSelections().size(), 1);
    const QList<QTextEdit::ExtraSelection> grayed = m_edit->extraSelections();
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

// The gray text's extra selections are cursors in the document: replacing
// them inside its signals freed cursors QTextDocument::clear() was holding
// aside, and the app aborted on the next note (heap corruption).
void tst_Checklists::grayIsNotUpdatedInsideDocumentSignals()
{
    openWindow();
    makeChecklist();
    QList<qsizetype> seen; // the selections while the document signals
    connect(m_edit->document(), &QTextDocument::contentsChanged, this,
            [&] { seen.append(m_edit->extraSelections().size()); });
    QTest::mouseClick(m_edit->viewport(), Qt::LeftButton, Qt::NoModifier, checkboxOf(1));
    QVERIFY(!seen.isEmpty());
    QVERIFY(std::ranges::all_of(seen, [](qsizetype size) { return size == 0; }));
    QTRY_COMPARE(m_edit->extraSelections().size(), 1); // afterwards

    m_edit->setNoteBody(QString(), QStringLiteral("Other")); // as opening a note does
    QVERIFY(m_edit->extraSelections().isEmpty());
}

// The reported crash: open a note with checked items, then another one, and
// type there.
void tst_Checklists::switchingFromCheckedItemsIsSafe()
{
    const Note other = await(m_context->notes().create(roles::kAllNotesFolderId, {},
                                                       QStringLiteral("asd\nmore text")))
                           .value();
    m_window = std::make_unique<MainWindow>(*m_context); // "asd" is newer: it opens first
    m_window->show();
    QVERIFY(QTest::qWaitForWindowExposed(m_window.get()));
    m_edit = m_window->findChild<EditorView*>(QStringLiteral("editorPane"))->textEdit();
    auto* list = m_window->findChild<notes::ui::NoteListView*>(QStringLiteral("noteListPane"));
    QTRY_VERIFY(list->selectNote(m_note.id));
    QTRY_COMPARE(m_edit->toPlainText(), QStringLiteral("Groceries\nMilk\nEggs\nTea"));
    makeChecklist();
    for (const int item : {1, 2, 3}) {
        QTest::mouseClick(m_edit->viewport(), Qt::LeftButton, Qt::NoModifier, checkboxOf(item));
    }
    QTRY_COMPARE(m_edit->extraSelections().size(), 3);

    for (int round = 0; round < 3; ++round) {
        QVERIFY(list->selectNote(other.id));
        QTRY_VERIFY(m_edit->toPlainText().startsWith(QStringLiteral("asd")));
        m_edit->moveCursor(QTextCursor::End);
        QTest::keyClicks(m_edit, QStringLiteral(" x"));
        QVERIFY(list->selectNote(m_note.id));
        QTRY_VERIFY(m_edit->toPlainText().startsWith(QStringLiteral("Groceries")));
        QTRY_COMPARE(m_edit->extraSelections().size(), 3); // grayed again at once
    }
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
