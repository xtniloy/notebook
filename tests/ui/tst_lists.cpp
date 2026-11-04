// Lists in the window (task 3.5): typed markers, Tab / Shift+Tab, Enter on an
// empty item, Backspace at an item's start, the Format menu, the drawn dash,
// and saving.

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

#include <QAbstractTextDocumentLayout>
#include <QAction>
#include <QSettings>
#include <QTest>
#include <QTextBlock>
#include <QTextLayout>
#include <QTextList>

#include <memory>

using notes::editor::ParagraphStyle;
using notes::editor::StyleManager;
using notes::models::Note;
using notes::test::await;
using notes::ui::EditorView;
using notes::ui::FormatMenu;
using notes::ui::MainWindow;
using notes::ui::NoteTextEdit;
namespace roles = notes::models;

class tst_Lists : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void typedMarkerStartsList_data();
    void typedMarkerStartsList();
    void markersOnlyAtTheStartOfBody();
    void enterTwiceEndsList();
    void enterFlowLikeGoogleDocs_data();
    void enterFlowLikeGoogleDocs();
    void tabNestsAndRestartsNumbering();
    void tabOutsideListIsATab();
    void backspaceRemovesMarkerFirst();
    void menuAndShortcutsMakeLists();
    void dashIsDrawn();
    void listsSurviveSaving();

private:
    void openWindow();
    // A new Body paragraph at the end of the note, the cursor in it.
    void newParagraph();
    [[nodiscard]] QTextBlock block(int index) const;
    [[nodiscard]] ParagraphStyle styleOf(int index) const;
    [[nodiscard]] int levelOf(int index) const;
    [[nodiscard]] QString markerOf(int index) const;

    std::unique_ptr<notes::test::TempDb> m_dir;
    std::unique_ptr<notes::app::AppContext> m_context;
    std::unique_ptr<MainWindow> m_window;
    NoteTextEdit* m_edit = nullptr;
    Note m_note;
};

void tst_Lists::init()
{
    m_dir = std::make_unique<notes::test::TempDb>();
    m_context = notes::test::makeTestContext(m_dir->dirPath());
    (void)await(m_context->folders().ensureDefaultFolder());
    m_context->settings().setValue(MainWindow::kTrashNoticeShownKey, true);
    m_note =
        await(m_context->notes().create(roles::kAllNotesFolderId, {}, QStringLiteral("Shopping")))
            .value();
}

void tst_Lists::cleanup()
{
    m_window = nullptr;
    m_context = nullptr;
    m_dir = nullptr;
}

void tst_Lists::openWindow()
{
    m_window = std::make_unique<MainWindow>(*m_context);
    m_window->show();
    m_window->activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(m_window.get()));
    m_edit = m_window->findChild<EditorView*>(QStringLiteral("editorPane"))->textEdit();
    QTRY_VERIFY(m_edit->toPlainText().startsWith(QStringLiteral("Shopping")));
    m_edit->setFocus();
    QTRY_VERIFY(m_edit->hasFocus());
}

void tst_Lists::newParagraph()
{
    m_edit->moveCursor(QTextCursor::End);
    QTest::keyClick(m_edit, Qt::Key_Return);
}

QTextBlock tst_Lists::block(int index) const
{
    return m_edit->document()->findBlockByNumber(index);
}

ParagraphStyle tst_Lists::styleOf(int index) const
{
    return StyleManager::styleOf(block(index));
}

int tst_Lists::levelOf(int index) const
{
    const QTextList* list = block(index).textList();
    return list == nullptr ? 0 : list->format().indent();
}

QString tst_Lists::markerOf(int index) const
{
    const QTextBlock item = block(index);
    return item.textList() == nullptr ? QString() : item.textList()->itemText(item);
}

// TC-EDT-011, and the other two markers.
void tst_Lists::typedMarkerStartsList_data()
{
    QTest::addColumn<QString>("marker");
    QTest::addColumn<ParagraphStyle>("style");
    QTest::newRow("- ") << QStringLiteral("-") << ParagraphStyle::DashedList;
    QTest::newRow("* ") << QStringLiteral("*") << ParagraphStyle::BulletedList;
    QTest::newRow("1. ") << QStringLiteral("1.") << ParagraphStyle::NumberedList;
}

void tst_Lists::typedMarkerStartsList()
{
    QFETCH(QString, marker);
    QFETCH(ParagraphStyle, style);
    openWindow();
    newParagraph();
    QTest::keyClicks(m_edit, marker + QStringLiteral(" item"));
    QCOMPARE(styleOf(1), style);
    QCOMPARE(block(1).text(), QStringLiteral("item"));
    QCOMPARE(levelOf(1), 1);

    // Undoing the typed word, then the list: the marker comes back as text.
    QTest::keyClick(m_edit, Qt::Key_Z, Qt::ControlModifier);
    QTest::keyClick(m_edit, Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(block(1).text(), marker + QStringLiteral(" "));
    QCOMPARE(styleOf(1), ParagraphStyle::Body);
}

void tst_Lists::markersOnlyAtTheStartOfBody()
{
    openWindow();
    newParagraph();
    QTest::keyClicks(m_edit, QStringLiteral("a - b"));
    QCOMPARE(styleOf(1), ParagraphStyle::Body);
    QCOMPARE(block(1).text(), QStringLiteral("a - b"));

    // Not in a heading or in code.
    for (const ParagraphStyle style : {ParagraphStyle::Title, ParagraphStyle::Monospaced}) {
        newParagraph();
        m_edit->setParagraphStyle(style);
        QTest::keyClicks(m_edit, QStringLiteral("- x"));
        const int last = m_edit->document()->blockCount() - 1;
        QCOMPARE(block(last).text(), QStringLiteral("- x"));
        QCOMPARE(styleOf(last), style);
    }
}

// TC-EDT-012
void tst_Lists::enterTwiceEndsList()
{
    openWindow();
    newParagraph();
    QTest::keyClicks(m_edit, QStringLiteral("* Milk"));
    QTest::keyClick(m_edit, Qt::Key_Return);
    QCOMPARE(styleOf(2), ParagraphStyle::BulletedList); // a new, empty item
    QTest::keyClick(m_edit, Qt::Key_Return);
    QCOMPARE(m_edit->document()->blockCount(), 3); // no extra paragraph
    QCOMPARE(styleOf(2), ParagraphStyle::Body);
    QCOMPARE(m_edit->textCursor().block(), block(2));
    // It's a plain line now: the next Enter starts a new one at once (Qt's
    // Enter only reset an empty line's leftover list format the first time).
    QTest::keyClick(m_edit, Qt::Key_Return);
    QCOMPARE(m_edit->document()->blockCount(), 4);
    QTest::keyClick(m_edit, Qt::Key_Backspace);
    QCOMPARE(m_edit->document()->blockCount(), 3);
    QTest::keyClicks(m_edit, QStringLiteral("Done"));
    QCOMPARE(styleOf(2), ParagraphStyle::Body);
    QCOMPARE(styleOf(1), ParagraphStyle::BulletedList);

    // On an empty nested item, Enter goes up a level first.
    newParagraph();
    QTest::keyClicks(m_edit, QStringLiteral("1. one"));
    QTest::keyClick(m_edit, Qt::Key_Return);
    QTest::keyClick(m_edit, Qt::Key_Tab);
    QCOMPARE(levelOf(4), 2);
    QTest::keyClick(m_edit, Qt::Key_Return);
    QCOMPARE(levelOf(4), 1);
    QTest::keyClick(m_edit, Qt::Key_Return);
    QCOMPARE(levelOf(4), 0);
}

// TC-EDT-013
// Enter in every kind of list, as Google Docs does it (and macOS Notes):
// an empty item is never left behind and never added twice.
void tst_Lists::enterFlowLikeGoogleDocs_data()
{
    QTest::addColumn<int>("key");
    QTest::addColumn<ParagraphStyle>("style");
    QTest::newRow("bulleted") << int(Qt::Key_7) << ParagraphStyle::BulletedList;
    QTest::newRow("dashed") << int(Qt::Key_8) << ParagraphStyle::DashedList;
    QTest::newRow("numbered") << int(Qt::Key_9) << ParagraphStyle::NumberedList;
    QTest::newRow("checklist") << int(Qt::Key_L) << ParagraphStyle::Checklist;
}

void tst_Lists::enterFlowLikeGoogleDocs()
{
    QFETCH(int, key);
    QFETCH(ParagraphStyle, style);
    openWindow();
    QTextDocument* document = m_edit->document();
    const auto makeList = [&] {
        QTest::keyClick(m_edit, static_cast<Qt::Key>(key), Qt::ControlModifier | Qt::ShiftModifier);
    };
    // No list item is ever empty, except the one the caret is in.
    const auto noEmptyItems = [&] {
        for (QTextBlock item = document->begin(); item.isValid(); item = item.next()) {
            if (item.textList() != nullptr && item.text().isEmpty() &&
                item != m_edit->textCursor().block()) {
                return false;
            }
        }
        return true;
    };

    // 1. A new list's empty item, then Enter: back to the plain line it was.
    newParagraph();
    makeList();
    QCOMPARE(styleOf(1), style);
    QTest::keyClick(m_edit, Qt::Key_Return);
    QCOMPARE(document->blockCount(), 2);
    QCOMPARE(styleOf(1), ParagraphStyle::Body);
    QCOMPARE(m_edit->textCursor().blockNumber(), 1);

    // 2. One item, Enter, Enter: the item, then a plain line.
    makeList();
    QTest::keyClicks(m_edit, QStringLiteral("One"));
    QTest::keyClick(m_edit, Qt::Key_Return);
    QCOMPARE(styleOf(2), style); // a new, empty item
    QTest::keyClick(m_edit, Qt::Key_Return);
    QCOMPARE(document->blockCount(), 3);
    QCOMPARE(styleOf(1), style);
    QCOMPARE(styleOf(2), ParagraphStyle::Body);
    QCOMPARE(m_edit->textCursor().blockNumber(), 2);
    QVERIFY(noEmptyItems());

    // 3.1 More items, then Enter twice at the end of the last one.
    QTextCursor one(block(1));
    one.movePosition(QTextCursor::EndOfBlock);
    m_edit->setTextCursor(one);
    for (const QString& text : {QStringLiteral("Two"), QStringLiteral("Three")}) {
        QTest::keyClick(m_edit, Qt::Key_Return);
        QTest::keyClicks(m_edit, text);
    }
    QTest::keyClick(m_edit, Qt::Key_Return);
    QTest::keyClick(m_edit, Qt::Key_Return);
    QCOMPARE(document->toPlainText(), QStringLiteral("Shopping\nOne\nTwo\nThree\n\n"));
    for (int i = 1; i <= 3; ++i) {
        QCOMPARE(styleOf(i), style);
    }
    QCOMPARE(styleOf(4), ParagraphStyle::Body);
    QCOMPARE(m_edit->textCursor().blockNumber(), 4);
    QVERIFY(noEmptyItems());

    // 3.2 Enter twice after "One": the list splits around a plain line.
    one = QTextCursor(block(1));
    one.movePosition(QTextCursor::EndOfBlock);
    m_edit->setTextCursor(one);
    QTest::keyClick(m_edit, Qt::Key_Return);
    QTest::keyClick(m_edit, Qt::Key_Return);
    QCOMPARE(document->toPlainText(), QStringLiteral("Shopping\nOne\n\nTwo\nThree\n\n"));
    QCOMPARE(styleOf(1), style);
    QCOMPARE(styleOf(2), ParagraphStyle::Body);
    QCOMPARE(styleOf(3), style);
    QCOMPARE(styleOf(4), style);
    QCOMPARE(m_edit->textCursor().blockNumber(), 2);
    QVERIFY(noEmptyItems());
    // And the next Enter is an ordinary new line.
    QTest::keyClick(m_edit, Qt::Key_Return);
    QCOMPARE(document->toPlainText(), QStringLiteral("Shopping\nOne\n\n\nTwo\nThree\n\n"));
    QCOMPARE(m_edit->textCursor().blockNumber(), 3);
}

void tst_Lists::tabNestsAndRestartsNumbering()
{
    openWindow();
    newParagraph();
    QTest::keyClicks(m_edit, QStringLiteral("1. one"));
    QTest::keyClick(m_edit, Qt::Key_Return);
    QTest::keyClicks(m_edit, QStringLiteral("two"));
    QTest::keyClick(m_edit, Qt::Key_Return);
    QTest::keyClicks(m_edit, QStringLiteral("three"));
    QCOMPARE(markerOf(3), QStringLiteral("3."));

    QTextCursor two(block(2));
    m_edit->setTextCursor(two);
    QTest::keyClick(m_edit, Qt::Key_Tab);
    QCOMPARE(levelOf(2), 2);
    QCOMPARE(markerOf(2), QStringLiteral("1.")); // numbering restarts at the nested level
    QCOMPARE(markerOf(3), QStringLiteral("2."));
    QCOMPARE(block(2).text(), QStringLiteral("two")); // no tab typed

    QTest::keyClick(m_edit, Qt::Key_Backtab, Qt::ShiftModifier);
    QCOMPARE(levelOf(2), 1);
    QCOMPARE(markerOf(2), QStringLiteral("2."));
    QCOMPARE(markerOf(3), QStringLiteral("3."));
}

void tst_Lists::tabOutsideListIsATab()
{
    openWindow();
    m_edit->moveCursor(QTextCursor::End);
    QTest::keyClick(m_edit, Qt::Key_Tab);
    QCOMPARE(block(0).text(), QStringLiteral("Shopping\t"));
}

void tst_Lists::backspaceRemovesMarkerFirst()
{
    openWindow();
    newParagraph();
    QTest::keyClicks(m_edit, QStringLiteral("- Milk"));
    m_edit->moveCursor(QTextCursor::StartOfBlock);
    QTest::keyClick(m_edit, Qt::Key_Backspace);
    QCOMPARE(styleOf(1), ParagraphStyle::Body);
    QCOMPARE(block(1).text(), QStringLiteral("Milk"));
    QCOMPARE(block(1).blockFormat().indent(), 0);
    QTest::keyClick(m_edit, Qt::Key_Backspace); // then it joins the line above
    QCOMPARE(m_edit->toPlainText(), QStringLiteral("ShoppingMilk"));
}

void tst_Lists::menuAndShortcutsMakeLists()
{
    openWindow();
    auto* menu = m_window->findChild<FormatMenu*>(QStringLiteral("formatMenu"));
    QVERIFY(menu);
    m_edit->moveCursor(QTextCursor::End);
    QTest::keyClick(m_edit, Qt::Key_7, Qt::ControlModifier | Qt::ShiftModifier);
    QCOMPARE(styleOf(0), ParagraphStyle::BulletedList);
    QVERIFY(menu->styleAction(ParagraphStyle::BulletedList)->isChecked());
    QTest::keyClick(m_edit, Qt::Key_8, Qt::ControlModifier | Qt::ShiftModifier);
    QCOMPARE(styleOf(0), ParagraphStyle::DashedList);
    QTest::keyClick(m_edit, Qt::Key_9, Qt::ControlModifier | Qt::ShiftModifier);
    QCOMPARE(styleOf(0), ParagraphStyle::NumberedList);
    QVERIFY(menu->styleAction(ParagraphStyle::NumberedList)->isChecked());
    menu->styleAction(ParagraphStyle::Body)->trigger();
    QCOMPARE(styleOf(0), ParagraphStyle::Body);
    QVERIFY(block(0).textList() == nullptr);
}

// Qt draws no dash marker; the editor does, before the item's text.
void tst_Lists::dashIsDrawn()
{
    openWindow();
    newParagraph();
    QTest::keyClicks(m_edit, QStringLiteral("- Milk"));
    QTest::keyClick(m_edit, Qt::Key_Return); // a new item
    QTest::keyClick(m_edit, Qt::Key_Return); // out of the list
    QTest::keyClicks(m_edit, QStringLiteral("Body"));
    QCOMPARE(styleOf(2), ParagraphStyle::Body);

    const QImage image = m_edit->viewport()->grab().toImage();
    const QAbstractTextDocumentLayout* layout = m_edit->document()->documentLayout();
    const auto inkBeforeText = [&](int index) {
        const QRectF bounds = layout->blockBoundingRect(block(index));
        const QTextLine line = block(index).layout()->lineAt(0);
        const auto textLeft = static_cast<int>(bounds.left() + line.x());
        const QRect area(textLeft - 20, static_cast<int>(bounds.top() + line.y()), 18,
                         static_cast<int>(line.height()));
        int dark = 0;
        for (int y = area.top(); y <= area.bottom(); ++y) {
            for (int x = std::max(0, area.left()); x <= area.right(); ++x) {
                dark += qGray(image.pixel(x, y)) < 128 ? 1 : 0;
            }
        }
        return dark;
    };
    QVERIFY2(inkBeforeText(1) > 0, "no dash before the dashed item");
    QCOMPARE(inkBeforeText(2), 0); // the Body paragraph has nothing there
}

// Done when: lists survive saving, kinds and levels included.
void tst_Lists::listsSurviveSaving()
{
    openWindow();
    newParagraph();
    QTest::keyClicks(m_edit, QStringLiteral("- Milk"));
    QTest::keyClick(m_edit, Qt::Key_Return);
    QTest::keyClick(m_edit, Qt::Key_Tab);
    QTest::keyClicks(m_edit, QStringLiteral("Oat"));
    m_window->close(); // saves
    m_window = nullptr;

    const Note saved = await(m_context->notes().get(m_note.id)).value();
    QTextDocument stored;
    QVERIFY(!notes::editor::NoteFormat::deserialize(saved.body, stored).has_value());
    QCOMPARE(StyleManager::styleOf(stored.findBlockByNumber(1)), ParagraphStyle::DashedList);
    QCOMPARE(StyleManager::styleOf(stored.findBlockByNumber(2)), ParagraphStyle::DashedList);
    QCOMPARE(stored.findBlockByNumber(2).textList()->format().indent(), 2);

    openWindow();
    QCOMPARE(styleOf(1), ParagraphStyle::DashedList);
    QCOMPARE(levelOf(2), 2);
}

QTEST_MAIN(tst_Lists)
#include "tst_lists.moc"
