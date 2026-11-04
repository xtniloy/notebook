// Paragraph styles in the window (task 3.2): shortcuts and the Format menu
// apply them, the menu checks the current one, one Ctrl+Z undoes a change,
// Enter after a heading continues in Body, and styles survive saving. And the
// first line as title (task 3.3).

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
#include "ui/Sidebar.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QMenuBar>
#include <QSettings>
#include <QTest>
#include <QTextBlock>
#include <QToolBar>
#include <QToolButton>

#include <chrono>
#include <memory>
#include <optional>

using namespace std::chrono_literals;
using notes::editor::ParagraphStyle;
using notes::editor::StyleManager;
using notes::models::Note;
using notes::test::await;
using notes::ui::ActionId;
using notes::ui::EditorView;
using notes::ui::FormatMenu;
using notes::ui::MainWindow;
using notes::ui::NoteTextEdit;
using notes::ui::Sidebar;
namespace roles = notes::models;

class tst_ParagraphStyles : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void shortcutsApplyStyles_data();
    void shortcutsApplyStyles();
    void menuShowsCurrentStyle();
    void aaButtonOpensStyles();
    void undoRestoresStyleInOneStep();
    void enterAfterHeadingContinuesInBody();
    void stylesSurviveSaving();
    void stylesNeedAnEditableNote();
    void newNoteStartsWithTitle();
    void deletingFirstLineRenamesNote();
    void pasteIntoTitleGivesBody();

private:
    void openWindow();
    // Puts the cursor in paragraph `index` of the editor.
    void moveToParagraph(int index);
    [[nodiscard]] ParagraphStyle styleOfParagraph(int index) const;
    [[nodiscard]] FormatMenu* formatMenu() const;
    // Creates a note with the New Note action; the editor has it, focused.
    void newNote();
    // Closes the window (which saves) and returns the note that was open.
    [[nodiscard]] Note closeAndReload();

    std::unique_ptr<notes::test::TempDb> m_dir;
    std::unique_ptr<notes::app::AppContext> m_context;
    std::unique_ptr<MainWindow> m_window;
    EditorView* m_editor = nullptr;
    NoteTextEdit* m_edit = nullptr;
    Note m_note;
};

void tst_ParagraphStyles::init()
{
    m_dir = std::make_unique<notes::test::TempDb>();
    m_context = notes::test::makeTestContext(m_dir->dirPath());
    (void)await(m_context->folders().ensureDefaultFolder());
    m_context->settings().setValue(MainWindow::kTrashNoticeShownKey, true);
    m_note = await(m_context->notes().create(roles::kAllNotesFolderId, {},
                                             QStringLiteral("Trip\nPacking\nPassport")))
                 .value();
}

void tst_ParagraphStyles::cleanup()
{
    m_window = nullptr;
    m_context = nullptr;
    m_dir = nullptr;
}

void tst_ParagraphStyles::openWindow()
{
    m_window = std::make_unique<MainWindow>(*m_context);
    m_window->show();
    m_window->activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(m_window.get()));
    m_editor = m_window->findChild<EditorView*>(QStringLiteral("editorPane"));
    QVERIFY(m_editor);
    m_edit = m_editor->textEdit();
    QTRY_COMPARE(m_edit->toPlainText(), QStringLiteral("Trip\nPacking\nPassport"));
    m_edit->setFocus();
    QTRY_VERIFY(m_edit->hasFocus());
}

void tst_ParagraphStyles::moveToParagraph(int index)
{
    QTextCursor cursor(m_edit->document()->findBlockByNumber(index));
    cursor.movePosition(QTextCursor::EndOfBlock);
    m_edit->setTextCursor(cursor);
}

ParagraphStyle tst_ParagraphStyles::styleOfParagraph(int index) const
{
    return StyleManager::styleOf(m_edit->document()->findBlockByNumber(index));
}

FormatMenu* tst_ParagraphStyles::formatMenu() const
{
    return m_window->findChild<FormatMenu*>(QStringLiteral("formatMenu"));
}

void tst_ParagraphStyles::newNote()
{
    m_window->actions().action(ActionId::NewNote)->trigger();
    QTRY_VERIFY(m_edit->toPlainText().isEmpty());
    QTRY_VERIFY(m_edit->hasFocus());
}

Note tst_ParagraphStyles::closeAndReload()
{
    const std::optional<qint64> open =
        m_window->findChild<notes::ui::NoteListView*>(QStringLiteral("noteListPane"))
            ->currentNote();
    m_window->close(); // saves
    m_window = nullptr;
    return await(m_context->notes().get(open.value_or(0))).value();
}

// TC-EDT-002
void tst_ParagraphStyles::shortcutsApplyStyles_data()
{
    QTest::addColumn<int>("key");
    QTest::addColumn<ParagraphStyle>("style");
    QTest::addColumn<int>("level");
    QTest::newRow("Ctrl+Shift+T") << int(Qt::Key_T) << ParagraphStyle::Title << 1;
    QTest::newRow("Ctrl+Shift+H") << int(Qt::Key_H) << ParagraphStyle::Heading << 2;
    QTest::newRow("Ctrl+Shift+J") << int(Qt::Key_J) << ParagraphStyle::Subheading << 3;
    QTest::newRow("Ctrl+Shift+M") << int(Qt::Key_M) << ParagraphStyle::Monospaced << 0;
}

void tst_ParagraphStyles::shortcutsApplyStyles()
{
    QFETCH(int, key);
    QFETCH(ParagraphStyle, style);
    QFETCH(int, level);
    openWindow();
    moveToParagraph(1);

    QTest::keyClick(m_edit, static_cast<Qt::Key>(key), Qt::ControlModifier | Qt::ShiftModifier);
    QCOMPARE(styleOfParagraph(1), style);
    QCOMPARE(m_edit->document()->findBlockByNumber(1).blockFormat().headingLevel(), level);
    QCOMPARE(styleOfParagraph(0), ParagraphStyle::Body);
    QCOMPARE(m_edit->toPlainText(), QStringLiteral("Trip\nPacking\nPassport")); // no text typed

    // Ctrl+Shift+B brings it back to Body.
    QTest::keyClick(m_edit, Qt::Key_B, Qt::ControlModifier | Qt::ShiftModifier);
    QCOMPARE(styleOfParagraph(1), ParagraphStyle::Body);
}

// TC-EDT-003
void tst_ParagraphStyles::menuShowsCurrentStyle()
{
    openWindow();
    moveToParagraph(0);
    QTest::keyClick(m_edit, Qt::Key_T, Qt::ControlModifier | Qt::ShiftModifier);
    FormatMenu* menu = formatMenu();
    QVERIFY(menu);
    QVERIFY(menu->styleAction(ParagraphStyle::Title)->isChecked());

    moveToParagraph(2);
    QVERIFY(menu->styleAction(ParagraphStyle::Body)->isChecked());
    QVERIFY(!menu->styleAction(ParagraphStyle::Title)->isChecked());
    moveToParagraph(0);
    QVERIFY(menu->styleAction(ParagraphStyle::Title)->isChecked());

    // The menu bar's Format menu has the same actions, first.
    QMenu* bar = nullptr;
    for (QAction* top : m_window->menuBar()->actions()) {
        if (top->text().remove(QLatin1Char('&')) == u"Format") {
            bar = top->menu();
        }
    }
    QVERIFY(bar);
    QList<QAction*> items = bar->actions();
    items.removeIf([](const QAction* item) { return item->isSeparator(); });
    QCOMPARE(items.mid(0, menu->styleActions().size()), menu->styleActions());
}

void tst_ParagraphStyles::aaButtonOpensStyles()
{
    openWindow();
    QToolButton* aa = nullptr;
    for (auto* button : m_window->findChildren<QToolButton*>()) {
        if (button->defaultAction() == m_window->actions().action(ActionId::Format)) {
            aa = button;
        }
    }
    if (aa == nullptr) {
        QFAIL("no Aa button");
    }
    QCOMPARE(aa->menu(), formatMenu());
    QCOMPARE(aa->popupMode(), QToolButton::InstantPopup);
    QVERIFY(aa->isEnabled());

    // Choosing from the menu applies the style.
    moveToParagraph(2);
    formatMenu()->styleAction(ParagraphStyle::Subheading)->trigger();
    QCOMPARE(styleOfParagraph(2), ParagraphStyle::Subheading);
}

// TC-EDT-005
void tst_ParagraphStyles::undoRestoresStyleInOneStep()
{
    openWindow();
    moveToParagraph(1);
    QTest::keyClick(m_edit, Qt::Key_H, Qt::ControlModifier | Qt::ShiftModifier);
    QTest::keyClick(m_edit, Qt::Key_T, Qt::ControlModifier | Qt::ShiftModifier);
    QCOMPARE(styleOfParagraph(1), ParagraphStyle::Title);

    QTest::keyClick(m_edit, Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(styleOfParagraph(1), ParagraphStyle::Heading);
    QTest::keyClick(m_edit, Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(styleOfParagraph(1), ParagraphStyle::Body);
    QCOMPARE(m_edit->toPlainText(), QStringLiteral("Trip\nPacking\nPassport"));
}

// As in macOS Notes: a new paragraph after a heading is Body; inside a
// heading, Enter splits it and both halves stay headings.
void tst_ParagraphStyles::enterAfterHeadingContinuesInBody()
{
    openWindow();
    moveToParagraph(0);
    QTest::keyClick(m_edit, Qt::Key_T, Qt::ControlModifier | Qt::ShiftModifier);
    QTest::keyClick(m_edit, Qt::Key_Return);
    QTest::keyClicks(m_edit, QStringLiteral("Day one"));
    QCOMPARE(styleOfParagraph(0), ParagraphStyle::Title);
    QCOMPARE(styleOfParagraph(1), ParagraphStyle::Body);
    const QTextBlock typed = m_edit->document()->findBlockByNumber(1);
    QCOMPARE(typed.text(), QStringLiteral("Day one"));
    QVERIFY(typed.begin().fragment().charFormat().fontWeight() < QFont::Bold);

    // Inside a heading, Enter splits it: both halves stay headings.
    QTextCursor middle(m_edit->document()->findBlockByNumber(0));
    middle.setPosition(middle.position() + 2);
    m_edit->setTextCursor(middle);
    QTest::keyClick(m_edit, Qt::Key_Return);
    QCOMPARE(styleOfParagraph(0), ParagraphStyle::Title);
    QCOMPARE(styleOfParagraph(1), ParagraphStyle::Title);
}

// Done when: styles survive save.
void tst_ParagraphStyles::stylesSurviveSaving()
{
    openWindow();
    moveToParagraph(0);
    QTest::keyClick(m_edit, Qt::Key_T, Qt::ControlModifier | Qt::ShiftModifier);
    moveToParagraph(2);
    QTest::keyClick(m_edit, Qt::Key_M, Qt::ControlModifier | Qt::ShiftModifier);
    m_window->close(); // saves
    m_window = nullptr;

    const Note saved = await(m_context->notes().get(m_note.id)).value();
    QTextDocument stored;
    QVERIFY(!notes::editor::NoteFormat::deserialize(saved.body, stored).has_value());
    QCOMPARE(StyleManager::styleOf(stored.findBlockByNumber(0)), ParagraphStyle::Title);
    QCOMPARE(StyleManager::styleOf(stored.findBlockByNumber(1)), ParagraphStyle::Body);
    QCOMPARE(StyleManager::styleOf(stored.findBlockByNumber(2)), ParagraphStyle::Monospaced);

    openWindow();
    QCOMPARE(styleOfParagraph(0), ParagraphStyle::Title);
    QCOMPARE(styleOfParagraph(2), ParagraphStyle::Monospaced);
}

// No note, or one in Recently Deleted: nothing to style.
void tst_ParagraphStyles::stylesNeedAnEditableNote()
{
    QVERIFY(await(m_context->notes().trash(m_note.id)));
    m_window = std::make_unique<MainWindow>(*m_context);
    m_window->show();
    QVERIFY(QTest::qWaitForWindowExposed(m_window.get()));
    m_editor = m_window->findChild<EditorView*>(QStringLiteral("editorPane"));
    QTRY_VERIFY(!m_editor->hasNote());
    QVERIFY(!m_window->actions().action(ActionId::Format)->isEnabled());
    QVERIFY(!formatMenu()->styleAction(ParagraphStyle::Title)->isEnabled());

    QVERIFY(m_window->findChild<Sidebar*>(QStringLiteral("sidebarPane"))
                ->selectFolder(roles::kRecentlyDeletedFolderId));
    QTRY_VERIFY(m_editor->hasNote());
    QTRY_VERIFY(m_editor->textEdit()->isReadOnly());
    QVERIFY(!m_window->actions().action(ActionId::Format)->isEnabled());
    QVERIFY(!formatMenu()->styleAction(ParagraphStyle::Title)->isEnabled());
}

// TC-EDT-006: a new note starts with its title, and Enter gives Body.
void tst_ParagraphStyles::newNoteStartsWithTitle()
{
    openWindow();
    newNote();
    QCOMPARE(styleOfParagraph(0), ParagraphStyle::Title);
    QVERIFY(formatMenu()->styleAction(ParagraphStyle::Title)->isChecked());
    QVERIFY(!m_edit->document()->isUndoAvailable()); // not an edit

    QTest::keyClicks(m_edit, QStringLiteral("Groceries"));
    QTest::keyClick(m_edit, Qt::Key_Return);
    QTest::keyClicks(m_edit, QStringLiteral("Milk"));
    QCOMPARE(styleOfParagraph(0), ParagraphStyle::Title);
    QCOMPARE(styleOfParagraph(1), ParagraphStyle::Body);
    // Typed in the title's font, not just marked as a heading.
    const QTextBlock title = m_edit->document()->firstBlock();
    QVERIFY(title.begin().fragment().charFormat().fontWeight() >= QFont::Bold);

    const Note saved = closeAndReload();
    QCOMPARE(saved.title, QStringLiteral("Groceries"));
    QCOMPARE(saved.bodyText, QStringLiteral("Groceries\nMilk"));
    QTextDocument stored;
    QVERIFY(!notes::editor::NoteFormat::deserialize(saved.body, stored).has_value());
    QCOMPARE(StyleManager::styleOf(stored.firstBlock()), ParagraphStyle::Title);
    QCOMPARE(StyleManager::styleOf(stored.findBlockByNumber(1)), ParagraphStyle::Body);
}

// TC-EDT-007: the note's title is its first line, whatever that is now.
void tst_ParagraphStyles::deletingFirstLineRenamesNote()
{
    openWindow();
    newNote();
    QTest::keyClicks(m_edit, QStringLiteral("Groceries"));
    QTest::keyClick(m_edit, Qt::Key_Return);
    QTest::keyClicks(m_edit, QStringLiteral("Milk"));

    QTextCursor firstLine(m_edit->document()->firstBlock());
    firstLine.movePosition(QTextCursor::NextBlock, QTextCursor::KeepAnchor);
    m_edit->setTextCursor(firstLine);
    QTest::keyClick(m_edit, Qt::Key_Delete);
    QCOMPARE(m_edit->toPlainText(), QStringLiteral("Milk"));

    const Note saved = closeAndReload();
    QCOMPARE(saved.title, QStringLiteral("Milk"));
}

// Pasted lines come in the title's font: below the first line they're Body,
// at Body's size and weight, not Body that looks (and checks Bold) like a
// title. One Ctrl+Z takes the paste back.
void tst_ParagraphStyles::pasteIntoTitleGivesBody()
{
    openWindow();
    newNote();
    QApplication::clipboard()->setText(QStringLiteral("Groceries\nMilk\nEggs"));
    QTest::keyClick(m_edit, Qt::Key_V, Qt::ControlModifier);
    QCOMPARE(m_edit->toPlainText(), QStringLiteral("Groceries\nMilk\nEggs"));

    QCOMPARE(styleOfParagraph(0), ParagraphStyle::Title);
    const QTextBlock title = m_edit->document()->firstBlock();
    QVERIFY(title.begin().fragment().charFormat().fontWeight() >= QFont::Bold);
    for (int index : {1, 2}) {
        QCOMPARE(styleOfParagraph(index), ParagraphStyle::Body);
        const QTextBlock block = m_edit->document()->findBlockByNumber(index);
        const QTextCharFormat format = block.begin().fragment().charFormat();
        QVERIFY(!format.hasProperty(QTextFormat::FontPointSize));
        QVERIFY(!format.hasProperty(QTextFormat::FontPixelSize));
        QVERIFY(format.fontWeight() < QFont::Bold);
    }
    moveToParagraph(1);
    QVERIFY(formatMenu()->styleAction(ParagraphStyle::Body)->isChecked());
    QVERIFY(!formatMenu()->inlineAction(notes::editor::InlineStyle::Bold)->isChecked());

    QTest::keyClick(m_edit, Qt::Key_Z, Qt::ControlModifier);
    QVERIFY(m_edit->toPlainText().isEmpty());
}

QTEST_MAIN(tst_ParagraphStyles)
#include "tst_paragraphstyles.moc"
