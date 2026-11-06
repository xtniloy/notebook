// Undo and redo (task 3.10): TC-EDT-032 for every editor operation, one
// Ctrl+Z gives back the exact document from before it and Ctrl+Shift+Z the
// one after; TC-EDT-033 the undo history is the open note's own.

#include "TestHelpers.h"
#include "editor/StyleManager.h"
#include "models/Note.h"
#include "models/SidebarRoles.h"
#include "services/Autosave.h"
#include "services/NoteService.h"
#include "ui/EditorView.h"
#include "ui/MainWindow.h"
#include "ui/NoteListView.h"
#include "ui/NoteTextEdit.h"

#include <QApplication>
#include <QClipboard>
#include <QImage>
#include <QMimeData>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBlock>

#include <chrono>
#include <functional>
#include <memory>

using namespace std::chrono_literals;
using notes::editor::InlineStyle;
using notes::editor::ParagraphStyle;
using notes::models::Note;
using notes::services::Autosave;
using notes::test::await;
using notes::ui::EditorView;
using notes::ui::MainWindow;
using notes::ui::NoteListView;
using notes::ui::NoteTextEdit;

namespace {

// An editing operation on the editor: `setup` brings the note to where the
// operation starts (not undoable: the history starts after it), `run` does it.
// Styles are set as the window's shortcuts and Aa menu set them (their keys:
// tst_paragraphstyles).
struct Operation
{
    std::function<void(NoteTextEdit&)> setup;
    std::function<void(NoteTextEdit&)> run;
};

// The cursor on `from`..`to` (document positions).
void select(NoteTextEdit& edit, int from, int to)
{
    QTextCursor cursor(edit.document());
    cursor.setPosition(from);
    cursor.setPosition(to, QTextCursor::KeepAnchor);
    edit.setTextCursor(cursor);
}

// The cursor at the end of paragraph `index`.
void endOf(NoteTextEdit& edit, int index)
{
    QTextCursor cursor(edit.document()->findBlockByNumber(index));
    cursor.movePosition(QTextCursor::EndOfBlock);
    edit.setTextCursor(cursor);
}

void shortcut(NoteTextEdit& edit, Qt::Key key, Qt::KeyboardModifiers modifiers)
{
    QTest::keyClick(&edit, key, modifiers);
}

const auto kCtrlShift = Qt::ControlModifier | Qt::ShiftModifier;

void clipboardHtml(const QString& html)
{
    auto* mime = new QMimeData; // the clipboard takes it
    mime->setHtml(html);
    mime->setText(QStringLiteral("plain"));
    QApplication::clipboard()->setMimeData(mime);
}

QTemporaryDir* imageDir()
{
    static QTemporaryDir dir;
    return &dir;
}

} // namespace

Q_DECLARE_METATYPE(Operation)

class tst_UndoRedo : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void undoRestoresTheDocument_data();
    void undoRestoresTheDocument();
    void historyIsTheNotesOwn();

private:
    // What the note is: its stored body (styles, lists, checks, images,
    // links) and its text.
    [[nodiscard]] QString snapshot() const;
    // A key the user presses, and what the editor does with it afterwards
    // (code spacing, link look) before the next one.
    void press(Qt::Key key, Qt::KeyboardModifiers modifiers);

    std::unique_ptr<NoteTextEdit> m_edit;
};

void tst_UndoRedo::init()
{
    m_edit = std::make_unique<NoteTextEdit>();
    m_edit->resize(500, 400);
    m_edit->setNoteBody({}, QStringLiteral("Trip\nPacking list\nPassport\nTickets"));
    m_edit->show();
    QVERIFY(QTest::qWaitForWindowExposed(m_edit.get()));
    m_edit->setFocus();
}

void tst_UndoRedo::cleanup()
{
    m_edit.reset();
    QApplication::clipboard()->clear();
}

QString tst_UndoRedo::snapshot() const
{
    return m_edit->noteBody() + u'\n' + m_edit->toPlainText();
}

void tst_UndoRedo::press(Qt::Key key, Qt::KeyboardModifiers modifiers)
{
    QTest::keyClick(m_edit.get(), key, modifiers);
    QCoreApplication::processEvents();
}

// TC-EDT-032
void tst_UndoRedo::undoRestoresTheDocument_data()
{
    QTest::addColumn<Operation>("operation");
    const auto none = [](NoteTextEdit&) {};
    const auto row = [](const char* name, auto setup, auto run) {
        QTest::newRow(name) << Operation{.setup = setup, .run = run};
    };
    // Paragraphs: 0 "Trip" (from 0), 1 "Packing list" (5), 2 "Passport"
    // (18), 3 "Tickets" (27).

    // Typing and deleting.
    row("typing", none, [](NoteTextEdit& edit) {
        endOf(edit, 1);
        QTest::keyClicks(&edit, QStringLiteral(" for Rome"));
    });
    row("enter", none, [](NoteTextEdit& edit) {
        endOf(edit, 1);
        shortcut(edit, Qt::Key_Return, Qt::NoModifier);
    });
    row("delete selection", none, [](NoteTextEdit& edit) {
        select(edit, 7, 22);
        shortcut(edit, Qt::Key_Backspace, Qt::NoModifier);
    });

    // Paragraph styles (task 3.2).
    row("title", none, [](NoteTextEdit& edit) {
        endOf(edit, 1);
        edit.setParagraphStyle(ParagraphStyle::Title);
    });
    row("heading over two paragraphs", none, [](NoteTextEdit& edit) {
        select(edit, 6, 20);
        edit.setParagraphStyle(ParagraphStyle::Heading);
    });
    row("code block", none, [](NoteTextEdit& edit) {
        endOf(edit, 1);
        edit.setParagraphStyle(ParagraphStyle::Monospaced);
    });
    row(
        "code block back to body",
        [](NoteTextEdit& edit) {
            endOf(edit, 1);
            edit.setParagraphStyle(ParagraphStyle::Monospaced);
        },
        [](NoteTextEdit& edit) { edit.setParagraphStyle(ParagraphStyle::Monospaced); });
    row(
        "enter in a code block",
        [](NoteTextEdit& edit) {
            endOf(edit, 1);
            edit.setParagraphStyle(ParagraphStyle::Monospaced);
        },
        [](NoteTextEdit& edit) { shortcut(edit, Qt::Key_Return, Qt::NoModifier); });

    // Inline formats (task 3.4).
    row("bold", none, [](NoteTextEdit& edit) {
        select(edit, 5, 12);
        edit.setInlineStyle(InlineStyle::Bold, true);
    });
    row("strikethrough", none, [](NoteTextEdit& edit) {
        select(edit, 5, 12);
        edit.setInlineStyle(InlineStyle::Strikethrough, true);
    });
    row("inline code", none, [](NoteTextEdit& edit) {
        select(edit, 13, 17); // "list"
        edit.setParagraphStyle(ParagraphStyle::Monospaced);
    });

    // Lists (task 3.5).
    row("bulleted list", none, [](NoteTextEdit& edit) {
        select(edit, 6, 30);
        edit.setParagraphStyle(ParagraphStyle::BulletedList);
    });
    row(
        "list indented",
        [](NoteTextEdit& edit) {
            select(edit, 6, 30);
            edit.setParagraphStyle(ParagraphStyle::BulletedList);
            endOf(edit, 2);
        },
        [](NoteTextEdit& edit) { shortcut(edit, Qt::Key_Tab, Qt::NoModifier); });

    // Checklists (task 3.6).
    row("checklist", none, [](NoteTextEdit& edit) {
        select(edit, 6, 30);
        edit.toggleChecklist();
    });
    row(
        "item checked",
        [](NoteTextEdit& edit) {
            select(edit, 6, 30);
            edit.toggleChecklist();
            endOf(edit, 3);
        },
        [](NoteTextEdit& edit) {
            QCoreApplication::processEvents();
            const QRectF box = edit.checkboxRect(edit.document()->findBlockByNumber(2));
            QTest::mouseClick(edit.viewport(), Qt::LeftButton, Qt::NoModifier,
                              box.center().toPoint());
        });

    // Images (task 3.7).
    row("image inserted", none, [](NoteTextEdit& edit) {
        QImage image(40, 30, QImage::Format_RGB32);
        image.fill(Qt::red);
        const QString path = imageDir()->filePath(QStringLiteral("red.png"));
        image.save(path);
        endOf(edit, 1);
        edit.insertImageFiles({path});
    });

    // Paste (task 3.8).
    row("paste html", none, [](NoteTextEdit& edit) {
        clipboardHtml(QStringLiteral("<p><b>Bold</b> and <i>italic</i></p><ul><li>one</li></ul>"));
        endOf(edit, 1);
        shortcut(edit, Qt::Key_V, Qt::ControlModifier);
    });
    row("paste plain text", none, [](NoteTextEdit& edit) {
        clipboardHtml(QStringLiteral("<b>Bold</b>"));
        endOf(edit, 1);
        shortcut(edit, Qt::Key_V, kCtrlShift);
    });
    row("paste markdown", none, [](NoteTextEdit& edit) {
        QApplication::clipboard()->setText(QStringLiteral("## Plan\n- one\n- two\n\n**Done**"));
        endOf(edit, 3);
        shortcut(edit, Qt::Key_V, Qt::ControlModifier | Qt::AltModifier);
    });
    row("paste over a selection", none, [](NoteTextEdit& edit) {
        QApplication::clipboard()->setText(QStringLiteral("Bags"));
        select(edit, 5, 12); // "Packing"
        shortcut(edit, Qt::Key_V, Qt::ControlModifier);
    });
}

void tst_UndoRedo::undoRestoresTheDocument()
{
    QFETCH(Operation, operation);
    operation.setup(*m_edit);
    QCoreApplication::processEvents();
    m_edit->document()->clearUndoRedoStacks();
    const QString before = snapshot();

    operation.run(*m_edit);
    QCoreApplication::processEvents();
    const QString after = snapshot();
    QVERIFY2(after != before, "the operation changed nothing");
    QVERIFY(m_edit->document()->isUndoAvailable());

    // One Ctrl+Z: exactly the document from before.
    press(Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(snapshot(), before);
    // Ctrl+Shift+Z: exactly the one from after.
    press(Qt::Key_Z, kCtrlShift);
    QCOMPARE(snapshot(), after);
    // And back again.
    press(Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(snapshot(), before);
}

// TC-EDT-033
void tst_UndoRedo::historyIsTheNotesOwn()
{
    m_edit.reset();
    const notes::test::TempDb dir;
    const std::unique_ptr<notes::app::AppContext> context =
        notes::test::makeTestContext(dir.dirPath());
    const Note a =
        await(context->notes().create(notes::models::kAllNotesFolderId, {}, QStringLiteral("A")))
            .value();
    notes::test::fakeClockOf(*context).advance(1min);
    const Note b =
        await(context->notes().create(notes::models::kAllNotesFolderId, {}, QStringLiteral("B")))
            .value();
    MainWindow window(*context);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    auto* list = window.findChild<NoteListView*>(QStringLiteral("noteListPane"));
    auto* editor = window.findChild<EditorView*>(QStringLiteral("editorPane"));
    QVERIFY(list && editor);
    NoteTextEdit* edit = editor->textEdit();
    const auto open = [&](const Note& note, const QString& text) {
        QTRY_VERIFY(list->selectNote(note.id));
        QTRY_COMPARE(window.findChild<Autosave*>()->trackedNote(), note.id);
        QTRY_COMPARE(edit->toPlainText(), text);
        edit->setFocus();
        edit->moveCursor(QTextCursor::End);
    };

    // A: typed in.
    open(a, QStringLiteral("A"));
    QTest::keyClicks(edit, QStringLiteral(" one"));
    QVERIFY(edit->document()->isUndoAvailable());
    // B: opens with no history; typed in.
    open(b, QStringLiteral("B"));
    QVERIFY(!edit->document()->isUndoAvailable());
    QVERIFY(!edit->document()->isRedoAvailable());
    QTest::keyClicks(edit, QStringLiteral(" two"));
    // Back to A: none of B's edits (nor A's earlier ones) to undo.
    open(a, QStringLiteral("A one"));
    QVERIFY(!edit->document()->isUndoAvailable());
    QTest::keyClick(edit, Qt::Key_Z, Qt::ControlModifier);
    QTest::keyClick(edit, Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(edit->toPlainText(), QStringLiteral("A one"));
    // Nor redo either.
    QTest::keyClick(edit, Qt::Key_Z, kCtrlShift);
    QCOMPARE(edit->toPlainText(), QStringLiteral("A one"));
    // B kept its edit.
    open(b, QStringLiteral("B two"));
}

QTEST_MAIN(tst_UndoRedo)
#include "tst_undoredo.moc"
