// Inline formats in the window (task 3.4): Bold, Italic, Underline and
// Strikethrough from their shortcuts and the Format menu, check marks that
// follow the cursor, the next typed text with nothing selected, and headings,
// which are bold by their style.

#include "TestHelpers.h"
#include "editor/NoteFormat.h"
#include "editor/StyleManager.h"
#include "models/Note.h"
#include "models/SidebarRoles.h"
#include "services/FolderService.h"
#include "services/NoteService.h"
#include "ui/EditorView.h"
#include "ui/FormatMenu.h"
#include "ui/MainWindow.h"
#include "ui/NoteTextEdit.h"

#include <QAction>
#include <QSettings>
#include <QTest>
#include <QTextBlock>

#include <memory>

using notes::editor::InlineStyle;
using notes::editor::StyleManager;
using notes::models::Note;
using notes::test::await;
using notes::ui::EditorView;
using notes::ui::FormatMenu;
using notes::ui::MainWindow;
using notes::ui::NoteTextEdit;
namespace roles = notes::models;

class tst_InlineFormats : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void shortcutTogglesSelection_data();
    void shortcutTogglesSelection();
    void actionFollowsCursor();
    void noSelectionFormatsNextText();
    void menuAppliesToSelection();
    void toggleIsOneUndoStep();
    void headingsKeepTheirBold();
    void formatsSurviveSaving();

private:
    void openWindow();
    // Selects characters [from, to) of the note's text.
    void select(int from, int to);
    // The format of the character at `position`.
    [[nodiscard]] QTextCharFormat formatAt(int position) const;
    [[nodiscard]] QAction* action(InlineStyle style) const;

    std::unique_ptr<notes::test::TempDb> m_dir;
    std::unique_ptr<notes::app::AppContext> m_context;
    std::unique_ptr<MainWindow> m_window;
    NoteTextEdit* m_edit = nullptr;
    Note m_note;
};

void tst_InlineFormats::init()
{
    m_dir = std::make_unique<notes::test::TempDb>();
    m_context = notes::test::makeTestContext(m_dir->dirPath());
    (void)await(m_context->folders().ensureDefaultFolder());
    m_context->settings().setValue(MainWindow::kTrashNoticeShownKey, true);
    m_note = await(m_context->notes().create(roles::kAllNotesFolderId, {},
                                             QStringLiteral("Shopping\nbuy milk today")))
                 .value();
}

void tst_InlineFormats::cleanup()
{
    m_window = nullptr;
    m_context = nullptr;
    m_dir = nullptr;
}

void tst_InlineFormats::openWindow()
{
    m_window = std::make_unique<MainWindow>(*m_context);
    m_window->show();
    m_window->activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(m_window.get()));
    m_edit = m_window->findChild<EditorView*>(QStringLiteral("editorPane"))->textEdit();
    QTRY_COMPARE(m_edit->toPlainText(), QStringLiteral("Shopping\nbuy milk today"));
    m_edit->setFocus();
    QTRY_VERIFY(m_edit->hasFocus());
}

void tst_InlineFormats::select(int from, int to)
{
    QTextCursor cursor(m_edit->document());
    cursor.setPosition(from);
    cursor.setPosition(to, QTextCursor::KeepAnchor);
    m_edit->setTextCursor(cursor);
}

QTextCharFormat tst_InlineFormats::formatAt(int position) const
{
    QTextCursor cursor(m_edit->document());
    cursor.setPosition(position + 1); // charFormat() is the one before
    return cursor.charFormat();
}

QAction* tst_InlineFormats::action(InlineStyle style) const
{
    return m_window->findChild<FormatMenu*>(QStringLiteral("formatMenu"))->inlineAction(style);
}

// "milk" in "Shopping\nbuy milk today".
constexpr int kMilkStart = 13;
constexpr int kMilkEnd = 17;

// TC-EDT-008, for all four.
void tst_InlineFormats::shortcutTogglesSelection_data()
{
    QTest::addColumn<InlineStyle>("style");
    QTest::addColumn<int>("key");
    QTest::addColumn<bool>("shift");
    QTest::newRow("Ctrl+B") << InlineStyle::Bold << int(Qt::Key_B) << false;
    QTest::newRow("Ctrl+I") << InlineStyle::Italic << int(Qt::Key_I) << false;
    QTest::newRow("Ctrl+U") << InlineStyle::Underline << int(Qt::Key_U) << false;
    QTest::newRow("Ctrl+Shift+X") << InlineStyle::Strikethrough << int(Qt::Key_X) << true;
}

void tst_InlineFormats::shortcutTogglesSelection()
{
    QFETCH(InlineStyle, style);
    QFETCH(int, key);
    QFETCH(bool, shift);
    const Qt::KeyboardModifiers modifiers =
        shift ? (Qt::ControlModifier | Qt::ShiftModifier) : Qt::ControlModifier;
    openWindow();
    select(kMilkStart, kMilkEnd);

    QTest::keyClick(m_edit, static_cast<Qt::Key>(key), modifiers);
    for (int i = kMilkStart; i < kMilkEnd; ++i) {
        QVERIFY(StyleManager::hasInline(formatAt(i), style));
    }
    QVERIFY(!StyleManager::hasInline(formatAt(kMilkStart - 1), style)); // the space before
    QVERIFY(!StyleManager::hasInline(formatAt(kMilkEnd), style));       // and after
    QVERIFY(action(style)->isChecked());
    QCOMPARE(m_edit->toPlainText(), QStringLiteral("Shopping\nbuy milk today")); // nothing typed

    QTest::keyClick(m_edit, static_cast<Qt::Key>(key), modifiers);
    for (int i = kMilkStart; i < kMilkEnd; ++i) {
        QVERIFY(!StyleManager::hasInline(formatAt(i), style));
    }
    QVERIFY(!action(style)->isChecked());
}

// TC-EDT-009
void tst_InlineFormats::actionFollowsCursor()
{
    openWindow();
    select(kMilkStart, kMilkEnd);
    QTest::keyClick(m_edit, Qt::Key_B, Qt::ControlModifier);
    QTest::keyClick(m_edit, Qt::Key_I, Qt::ControlModifier);

    select(kMilkStart + 2, kMilkStart + 2); // inside "milk"
    QVERIFY(action(InlineStyle::Bold)->isChecked());
    QVERIFY(action(InlineStyle::Italic)->isChecked());
    QVERIFY(!action(InlineStyle::Underline)->isChecked());

    select(kMilkEnd + 3, kMilkEnd + 3); // inside "today"
    QVERIFY(!action(InlineStyle::Bold)->isChecked());
    QVERIFY(!action(InlineStyle::Italic)->isChecked());
}

void tst_InlineFormats::noSelectionFormatsNextText()
{
    openWindow();
    select(kMilkEnd, kMilkEnd); // after "milk"
    QTest::keyClick(m_edit, Qt::Key_B, Qt::ControlModifier);
    QVERIFY(action(InlineStyle::Bold)->isChecked());
    QCOMPARE(m_edit->toPlainText(), QStringLiteral("Shopping\nbuy milk today")); // unchanged
    QVERIFY(!StyleManager::hasInline(formatAt(kMilkStart), InlineStyle::Bold));

    QTest::keyClicks(m_edit, QStringLiteral("!!"));
    QVERIFY(StyleManager::hasInline(formatAt(kMilkEnd), InlineStyle::Bold));
    QVERIFY(StyleManager::hasInline(formatAt(kMilkEnd + 1), InlineStyle::Bold));
    QVERIFY(!StyleManager::hasInline(formatAt(kMilkEnd + 2), InlineStyle::Bold)); // " today"
}

void tst_InlineFormats::menuAppliesToSelection()
{
    openWindow();
    select(kMilkStart, kMilkEnd);
    action(InlineStyle::Underline)->trigger();
    QVERIFY(StyleManager::hasInline(formatAt(kMilkStart), InlineStyle::Underline));
    QVERIFY(action(InlineStyle::Underline)->isChecked());
}

void tst_InlineFormats::toggleIsOneUndoStep()
{
    openWindow();
    select(0, static_cast<int>(m_edit->toPlainText().size())); // both paragraphs
    QTest::keyClick(m_edit, Qt::Key_X, Qt::ControlModifier | Qt::ShiftModifier);
    QVERIFY(StyleManager::hasInline(formatAt(0), InlineStyle::Strikethrough));
    QVERIFY(StyleManager::hasInline(formatAt(kMilkStart), InlineStyle::Strikethrough));

    QTest::keyClick(m_edit, Qt::Key_Z, Qt::ControlModifier);
    QVERIFY(!StyleManager::hasInline(formatAt(0), InlineStyle::Strikethrough));
    QVERIFY(!StyleManager::hasInline(formatAt(kMilkStart), InlineStyle::Strikethrough));
    QVERIFY(!m_edit->document()->isUndoAvailable());
}

// A heading is bold by its style, which isn't stored as the user's bold: Bold
// is shown on and can't be changed there, and a selection across a heading
// leaves the heading as it is.
void tst_InlineFormats::headingsKeepTheirBold()
{
    openWindow();
    select(0, 0);
    QTest::keyClick(m_edit, Qt::Key_T, Qt::ControlModifier | Qt::ShiftModifier); // Title
    select(2, 2);
    QVERIFY(action(InlineStyle::Bold)->isChecked());
    QVERIFY(!action(InlineStyle::Bold)->isEnabled());
    QVERIFY(action(InlineStyle::Italic)->isEnabled()); // the others still work

    select(0, static_cast<int>(m_edit->toPlainText().size()));
    QVERIFY(action(InlineStyle::Bold)->isEnabled()); // the body part can change
    action(InlineStyle::Bold)->trigger();            // off -> on: bold
    QVERIFY(StyleManager::hasInline(formatAt(kMilkStart), InlineStyle::Bold));
    action(InlineStyle::Bold)->trigger(); // and off again
    QVERIFY(!StyleManager::hasInline(formatAt(kMilkStart), InlineStyle::Bold));
    QVERIFY(StyleManager::hasInline(formatAt(0), InlineStyle::Bold)); // the title
}

// Done when: all four survive saving.
void tst_InlineFormats::formatsSurviveSaving()
{
    openWindow();
    select(kMilkStart, kMilkEnd);
    QTest::keyClick(m_edit, Qt::Key_B, Qt::ControlModifier);
    QTest::keyClick(m_edit, Qt::Key_I, Qt::ControlModifier);
    select(kMilkEnd + 1, kMilkEnd + 6); // "today"
    QTest::keyClick(m_edit, Qt::Key_U, Qt::ControlModifier);
    QTest::keyClick(m_edit, Qt::Key_X, Qt::ControlModifier | Qt::ShiftModifier);
    m_window->close(); // saves
    m_window = nullptr;

    const Note saved = await(m_context->notes().get(m_note.id)).value();
    QTextDocument stored;
    QVERIFY(!notes::editor::NoteFormat::deserialize(saved.body, stored).has_value());
    const auto at = [&](int position) {
        QTextCursor cursor(&stored);
        cursor.setPosition(position + 1);
        return cursor.charFormat();
    };
    QVERIFY(StyleManager::hasInline(at(kMilkStart), InlineStyle::Bold));
    QVERIFY(StyleManager::hasInline(at(kMilkStart), InlineStyle::Italic));
    QVERIFY(!StyleManager::hasInline(at(kMilkStart), InlineStyle::Underline));
    QVERIFY(StyleManager::hasInline(at(kMilkEnd + 1), InlineStyle::Underline));
    QVERIFY(StyleManager::hasInline(at(kMilkEnd + 1), InlineStyle::Strikethrough));
    QVERIFY(!StyleManager::hasInline(at(kMilkEnd + 1), InlineStyle::Bold));
}

QTEST_MAIN(tst_InlineFormats)
#include "tst_inlineformats.moc"
