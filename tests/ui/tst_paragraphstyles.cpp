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
#include "ui/AppStyle.h"
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
#include <QStyleOptionMenuItem>
#include <QTest>
#include <QTextBlock>
#include <QToolBar>
#include <QToolButton>
#include <QWidgetAction>

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
    void aaMenuLooksLikeMacNotes();
    void aaMenuButtonsSetInlineFormats();
    void undoRestoresStyleInOneStep();
    void monospacedOnWordsMakesInlineCode();
    void monospacedTurnsOff();
    void enterInCodeBlockAddsCodeLines();
    void enterAfterHeadingContinuesInBody();
    void enterOnEmptyLineAddsALine();
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
namespace {

// FormatMenu's look for one action, as it draws the item.
class ProbeMenu : public FormatMenu
{
public:
    QStyleOptionMenuItem optionFor(const QAction* action) const
    {
        QStyleOptionMenuItem option;
        initStyleOption(&option, action);
        return option;
    }
};

} // namespace

// As in macOS Notes: the inline formats as a row of buttons on top, then
// each style in its own look and the lists with their markers; no shortcuts
// or access keys there. The menu bar's Format menu stays plain.
void tst_ParagraphStyles::aaMenuLooksLikeMacNotes()
{
    openWindow();
    const QList<QAction*> items = formatMenu()->actions();
    QVERIFY(qobject_cast<QWidgetAction*>(items.first()) != nullptr); // the button row
    QVERIFY(items.at(1)->isSeparator());
    for (const QString& name :
         {QStringLiteral("boldButton"), QStringLiteral("italicButton"),
          QStringLiteral("underlineButton"), QStringLiteral("strikethroughButton")}) {
        QVERIFY2(formatMenu()->findChild<QToolButton*>(name) != nullptr, qPrintable(name));
    }
    for (QAction* action : formatMenu()->inlineActions()) {
        QVERIFY(!items.contains(action)); // buttons instead
    }
    QCOMPARE(items.mid(2), formatMenu()->styleActions()); // no separator before the lists

    const ProbeMenu probe;
    const auto option = [&](ParagraphStyle style) {
        return probe.optionFor(probe.styleAction(style));
    };
    const QFont body = option(ParagraphStyle::Body).font;
    QCOMPARE(option(ParagraphStyle::Body).text, QStringLiteral("Body"));
    QVERIFY(option(ParagraphStyle::Title).font.bold());
    QVERIFY(option(ParagraphStyle::Title).font.pointSizeF() >
            option(ParagraphStyle::Heading).font.pointSizeF());
    QVERIFY(option(ParagraphStyle::Heading).font.pointSizeF() >
            option(ParagraphStyle::Subheading).font.pointSizeF());
    QVERIFY(option(ParagraphStyle::Subheading).font.pointSizeF() > body.pointSizeF());
    QVERIFY(!body.bold());
    QVERIFY(option(ParagraphStyle::Monospaced)
                .font.families()
                .contains(QStringLiteral("JetBrains Mono")));
    QCOMPARE(option(ParagraphStyle::BulletedList).text, QStringLiteral("\u2022 Bulleted List"));
    QCOMPARE(option(ParagraphStyle::DashedList).text, QStringLiteral("\u2013 Dashed List"));
    QCOMPARE(option(ParagraphStyle::NumberedList).text, QStringLiteral("1. Numbered List"));
    QCOMPARE(option(ParagraphStyle::Title).reservedShortcutWidth, 0);
    // No room kept for the hidden shortcuts: only as wide as the styles.
    for (const QAction* action : formatMenu()->styleActions()) {
        QVERIFY(!action->isShortcutVisibleInContextMenu());
        QVERIFY(!action->shortcut().isEmpty()); // still there, for the menu bar
    }

    // The menu bar's Format menu: the same actions, plain, with shortcuts.
    QMenu* menuBarFormat = nullptr;
    for (QAction* top : m_window->menuBar()->actions()) {
        if (top->menu() != nullptr && top->menu() != formatMenu() &&
            top->menu()->actions().contains(formatMenu()->styleAction(ParagraphStyle::Title))) {
            menuBarFormat = top->menu();
        }
    }
    if (menuBarFormat == nullptr) {
        QFAIL("no Format menu in the menu bar");
    }
    for (QAction* action : formatMenu()->inlineActions()) {
        QVERIFY(menuBarFormat->actions().contains(action)); // their shortcuts live here
    }
    // A little more room between the lines than in other menus.
    QAction* bodyAction = formatMenu()->styleAction(ParagraphStyle::Body);
    QCOMPARE(formatMenu()->actionGeometry(bodyAction).height(),
             menuBarFormat->actionGeometry(bodyAction).height() + 6);
    // And room under the last one, not ending right at the border.
    const QRect last =
        formatMenu()->actionGeometry(formatMenu()->styleAction(ParagraphStyle::NumberedList));
    QVERIFY(formatMenu()->sizeHint().height() - last.bottom() >=
            8 + notes::ui::AppStyle::kPanelPadding);
}

void tst_ParagraphStyles::aaMenuButtonsSetInlineFormats()
{
    openWindow();
    auto* bold = formatMenu()->findChild<QToolButton*>(QStringLiteral("boldButton"));
    auto* italic = formatMenu()->findChild<QToolButton*>(QStringLiteral("italicButton"));
    if (bold == nullptr || italic == nullptr) {
        QFAIL("no buttons");
    }
    QTextCursor packing(m_edit->document()->findBlockByNumber(1));
    packing.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
    m_edit->setTextCursor(packing);
    QVERIFY(bold->isEnabled());
    QVERIFY(!bold->isChecked());

    bold->click();
    QVERIFY(StyleManager::hasInline(packing.charFormat(), notes::editor::InlineStyle::Bold));
    QVERIFY(bold->isChecked()); // follows the selection
    QVERIFY(!italic->isChecked());
    bold->click();
    QVERIFY(!StyleManager::hasInline(m_edit->textCursor().charFormat(),
                                     notes::editor::InlineStyle::Bold));
    QVERIFY(!bold->isChecked());

    // In a heading, Bold is its style's: shown pressed, can't be changed.
    moveToParagraph(0);
    QTest::keyClick(m_edit, Qt::Key_T, Qt::ControlModifier | Qt::ShiftModifier);
    QVERIFY(bold->isChecked());
    QVERIFY(!bold->isEnabled());
    QVERIFY(italic->isEnabled());
}

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

// Monospaced for a few selected words makes them inline code, not the
// paragraph a code block; whole paragraphs still become code blocks.
void tst_ParagraphStyles::monospacedOnWordsMakesInlineCode()
{
    openWindow();
    const auto select = [this](int from, int to) {
        QTextCursor cursor(m_edit->document());
        cursor.setPosition(from);
        cursor.setPosition(to, QTextCursor::KeepAnchor);
        m_edit->setTextCursor(cursor);
    };
    const auto monospacedAt = [this](int position) {
        QTextCursor cursor(m_edit->document());
        cursor.setPosition(position + 1); // the format of the character before
        return StyleManager::isMonospaced(cursor.charFormat());
    };
    const auto monospaced = [this] {
        QTest::keyClick(m_edit, Qt::Key_M, Qt::ControlModifier | Qt::ShiftModifier);
    };
    // "ack" of "Packing" (paragraph 1, from 5).
    select(6, 9);
    monospaced();
    QCOMPARE(styleOfParagraph(1), ParagraphStyle::Body);
    QVERIFY(!monospacedAt(5));
    QVERIFY(monospacedAt(6) && monospacedAt(8));
    QVERIFY(!monospacedAt(9));
    QVERIFY(m_edit->noteBody().contains(QStringLiteral("\"mono\":true")));
    QCOMPARE(m_edit->toPlainText(), QStringLiteral("Trip\nPacking\nPassport"));

    // Again: plain text again; and one Ctrl+Z undoes each.
    monospaced();
    QVERIFY(!monospacedAt(6) && !monospacedAt(8));
    QTest::keyClick(m_edit, Qt::Key_Z, Qt::ControlModifier);
    QVERIFY(monospacedAt(6) && monospacedAt(8));
    QTest::keyClick(m_edit, Qt::Key_Z, Qt::ControlModifier);
    QVERIFY(!monospacedAt(6));
    QCOMPARE(styleOfParagraph(1), ParagraphStyle::Body);

    // The whole paragraph selected: a code block.
    select(5, 12);
    monospaced();
    QCOMPARE(styleOfParagraph(1), ParagraphStyle::Monospaced);
    // Across paragraphs, even partly: code blocks too.
    QTest::keyClick(m_edit, Qt::Key_B, Qt::ControlModifier | Qt::ShiftModifier);
    select(2, 8);
    monospaced();
    QCOMPARE(styleOfParagraph(0), ParagraphStyle::Monospaced);
    QCOMPARE(styleOfParagraph(1), ParagraphStyle::Monospaced);
    // Words inside a code block: Monospaced turns off, for the paragraph.
    select(6, 9);
    monospaced();
    QCOMPARE(styleOfParagraph(1), ParagraphStyle::Body);
}

// Monospaced turns off the way it turned on, one press (and one Ctrl+Z)
// at a time; after inline code, typing goes on in plain text.
void tst_ParagraphStyles::monospacedTurnsOff()
{
    openWindow();
    const auto monospaced = [this] {
        QTest::keyClick(m_edit, Qt::Key_M, Qt::ControlModifier | Qt::ShiftModifier);
    };
    const auto monospacedAt = [this](int position) {
        QTextCursor cursor(m_edit->document());
        cursor.setPosition(position + 1); // the format of the character before
        return StyleManager::isMonospaced(cursor.charFormat());
    };
    // A code block, and back.
    moveToParagraph(1);
    monospaced();
    QCOMPARE(styleOfParagraph(1), ParagraphStyle::Monospaced);
    monospaced();
    QCOMPARE(styleOfParagraph(1), ParagraphStyle::Body);
    QTest::keyClick(m_edit, Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(styleOfParagraph(1), ParagraphStyle::Monospaced);
    QTest::keyClick(m_edit, Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(styleOfParagraph(1), ParagraphStyle::Body);

    // "Packing" (from 5) ends in inline code "ing": typing after it is code
    // until Monospaced is pressed, then plain text, still in the paragraph.
    QTextCursor cursor(m_edit->document());
    cursor.setPosition(9);
    cursor.setPosition(12, QTextCursor::KeepAnchor);
    m_edit->setTextCursor(cursor);
    monospaced();
    cursor.clearSelection();
    m_edit->setTextCursor(cursor); // at its end
    QTest::keyClicks(m_edit, QStringLiteral("s"));
    QVERIFY(monospacedAt(12));
    monospaced();
    QTest::keyClicks(m_edit, QStringLiteral(" ok"));
    QCOMPARE(m_edit->document()->findBlockByNumber(1).text(), QStringLiteral("Packings ok"));
    QVERIFY(!monospacedAt(13) && !monospacedAt(15));
    QCOMPARE(styleOfParagraph(1), ParagraphStyle::Body);
}

// Enter in a code block adds code lines, empty ones too; typing there is
// code. (Qt's Enter on an empty code line only reset its format.)
void tst_ParagraphStyles::enterInCodeBlockAddsCodeLines()
{
    openWindow();
    moveToParagraph(2);
    QTest::keyClick(m_edit, Qt::Key_Return);
    QTest::keyClick(m_edit, Qt::Key_M, Qt::ControlModifier | Qt::ShiftModifier);
    QCOMPARE(styleOfParagraph(3), ParagraphStyle::Monospaced);
    QTest::keyClick(m_edit, Qt::Key_Return);
    QTest::keyClick(m_edit, Qt::Key_Return);
    QTest::keyClicks(m_edit, QStringLiteral("make"));
    QCOMPARE(m_edit->toPlainText(), QStringLiteral("Trip\nPacking\nPassport\n\n\nmake"));
    for (const int index : {3, 4, 5}) {
        QCOMPARE(styleOfParagraph(index), ParagraphStyle::Monospaced);
    }
    // One Ctrl+Z per Enter.
    QTest::keyClick(m_edit, Qt::Key_Z, Qt::ControlModifier); // the typing
    QTest::keyClick(m_edit, Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(m_edit->toPlainText(), QStringLiteral("Trip\nPacking\nPassport\n\n"));
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

// Every Enter on an empty Body line adds a line: after a title, after
// choosing Body, and in a note loaded from storage. (A leftover heading level
// or indent of 0 made Qt reset the line's format instead, the first time.)
void tst_ParagraphStyles::enterOnEmptyLineAddsALine()
{
    openWindow();
    moveToParagraph(0); // "Trip", loaded from storage
    QTest::keyClick(m_edit, Qt::Key_T, Qt::ControlModifier | Qt::ShiftModifier);
    QTest::keyClick(m_edit, Qt::Key_Return); // an empty Body line after the title
    QCOMPARE(m_edit->document()->blockCount(), 4);
    QTest::keyClick(m_edit, Qt::Key_Return);
    QCOMPARE(m_edit->document()->blockCount(), 5);

    moveToParagraph(4); // "Passport": Heading, then Body, then two Enters
    QTest::keyClick(m_edit, Qt::Key_H, Qt::ControlModifier | Qt::ShiftModifier);
    QTest::keyClick(m_edit, Qt::Key_B, Qt::ControlModifier | Qt::ShiftModifier);
    QTest::keyClick(m_edit, Qt::Key_Return);
    QTest::keyClick(m_edit, Qt::Key_Return);
    QCOMPARE(m_edit->document()->blockCount(), 7);

    // Saved with empty lines, then opened again.
    const Note saved = closeAndReload();
    QTextDocument stored;
    QVERIFY(!notes::editor::NoteFormat::deserialize(saved.body, stored).has_value());
    for (QTextBlock block = stored.begin(); block.isValid(); block = block.next()) {
        if (block.blockFormat().headingLevel() == 0) {
            QVERIFY2(block.blockFormat().properties().isEmpty(), qPrintable(block.text()));
        }
    }
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
