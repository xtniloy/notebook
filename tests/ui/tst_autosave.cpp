// Autosave (task 2.9) in the main window: TC-STO-025 debounce, TC-STO-026
// switching notes saves first, TC-STO-028 selecting doesn't save, TC-STO-027
// typing then quitting keeps the text. Real database in a temp dir; real
// 500 ms timers.

#include "TestHelpers.h"
#include "editor/NoteFormat.h"
#include "models/Note.h"
#include "models/SidebarRoles.h"
#include "services/Autosave.h"
#include "services/NoteService.h"
#include "ui/EditorView.h"
#include "ui/MainWindow.h"
#include "ui/NoteListView.h"
#include "ui/NoteTextEdit.h"

#include <QSignalSpy>
#include <QTest>
#include <QTextDocument>

#include <chrono>
#include <memory>

using namespace std::chrono_literals;
using notes::models::Note;
using notes::services::Autosave;
using notes::services::NoteService;
using notes::test::await;
using notes::ui::EditorView;
using notes::ui::MainWindow;
using notes::ui::NoteListView;
using notes::ui::NoteTextEdit;

class tst_Autosave : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void typingSavesOnceAfterDelay();
    void switchingNoteSavesFirst();
    void selectingWithoutEditingSavesNothing();
    void undoneTypingSavesNothing();
    void typedTextSurvivesQuit();

private:
    void openWindow();
    // Selects `note` and waits until the editor shows it.
    void select(const Note& note);
    [[nodiscard]] Note reload(const Note& note);

    std::unique_ptr<notes::test::TempDb> m_dir;
    std::unique_ptr<notes::app::AppContext> m_context;
    std::unique_ptr<MainWindow> m_window;
    NoteListView* m_list = nullptr;
    EditorView* m_editor = nullptr;
    Note m_first;
    Note m_second;
};

void tst_Autosave::init()
{
    m_dir = std::make_unique<notes::test::TempDb>();
    m_context = notes::test::makeTestContext(m_dir->dirPath());
    m_first = await(m_context->notes().create(notes::models::kAllNotesFolderId, {},
                                              QStringLiteral("First")))
                  .value();
    notes::test::fakeClockOf(*m_context).advance(1min);
    m_second = await(m_context->notes().create(notes::models::kAllNotesFolderId, {},
                                               QStringLiteral("Second")))
                   .value();
    openWindow();
}

void tst_Autosave::cleanup()
{
    m_window = nullptr;
    m_context = nullptr;
    m_dir = nullptr;
}

void tst_Autosave::openWindow()
{
    m_window = std::make_unique<MainWindow>(*m_context);
    m_window->show();
    QVERIFY(QTest::qWaitForWindowExposed(m_window.get()));
    m_list = m_window->findChild<NoteListView*>(QStringLiteral("noteListPane"));
    m_editor = m_window->findChild<EditorView*>(QStringLiteral("editorPane"));
    QVERIFY(m_list && m_editor);
    QTRY_VERIFY(m_editor->hasNote()); // the newest note, "Second"
    QCOMPARE(m_editor->textEdit()->toPlainText(), reload(m_second).bodyText);
}

void tst_Autosave::select(const Note& note)
{
    QTRY_VERIFY(m_list->selectNote(note.id));
    QTRY_COMPARE(m_window->findChild<Autosave*>()->trackedNote(), note.id);
}

Note tst_Autosave::reload(const Note& note)
{
    return await(m_context->notes().get(note.id)).value();
}

// TC-STO-025
void tst_Autosave::typingSavesOnceAfterDelay()
{
    const QSignalSpy saves(&m_context->notes(), &NoteService::noteUpdated);
    NoteTextEdit* edit = m_editor->textEdit();
    edit->setFocus();
    edit->moveCursor(QTextCursor::End);
    QTest::keyClicks(edit, QStringLiteral(" abcdefghi"), Qt::NoModifier, 40); // 10 keys, ~400 ms
    QCOMPARE(saves.count(), 0);                                               // still typing

    QTest::qWait(static_cast<int>(Autosave::kDelay.count()) + 300);
    QCOMPARE(saves.count(), 1);
    const Note saved = reload(m_second);
    QCOMPARE(saved.bodyText, QStringLiteral("Second abcdefghi"));
    // Saved in the note format (task 3.1), which reads back to the same text.
    QTextDocument stored;
    QVERIFY(!notes::editor::NoteFormat::deserialize(saved.body, stored).has_value());
    QCOMPARE(stored.toPlainText(), saved.bodyText);
    QTest::qWait(static_cast<int>(Autosave::kDelay.count()) + 100);
    QCOMPARE(saves.count(), 1); // nothing more to save
}

// TC-STO-026
void tst_Autosave::switchingNoteSavesFirst()
{
    const QSignalSpy saves(&m_context->notes(), &NoteService::noteUpdated);
    NoteTextEdit* edit = m_editor->textEdit();
    edit->moveCursor(QTextCursor::End);
    QTest::keyClicks(edit, QStringLiteral(" edited"));
    QVERIFY(m_list->selectNote(m_first.id)); // well within 100 ms

    QTRY_COMPARE(m_editor->textEdit()->toPlainText(), QStringLiteral("First"));
    // Saved when the switch began, not 500 ms later.
    QCOMPARE(saves.count(), 1);
    QCOMPARE(reload(m_second).bodyText, QStringLiteral("Second edited"));
}

// TC-STO-028
void tst_Autosave::selectingWithoutEditingSavesNothing()
{
    const QSignalSpy saves(&m_context->notes(), &NoteService::noteUpdated);
    select(m_first);
    select(m_second);
    QTest::qWait(static_cast<int>(Autosave::kDelay.count()) + 200);
    QCOMPARE(saves.count(), 0);
    QCOMPARE(reload(m_first).version, m_first.version);
    QCOMPARE(reload(m_second).version, m_second.version);
}

void tst_Autosave::undoneTypingSavesNothing()
{
    const QSignalSpy saves(&m_context->notes(), &NoteService::noteUpdated);
    NoteTextEdit* edit = m_editor->textEdit();
    edit->moveCursor(QTextCursor::End);
    QTest::keyClicks(edit, QStringLiteral("xyz"));
    QTest::keyClick(edit, Qt::Key_Backspace);
    QTest::keyClick(edit, Qt::Key_Backspace);
    QTest::keyClick(edit, Qt::Key_Backspace);
    QTest::qWait(static_cast<int>(Autosave::kDelay.count()) + 200);
    QCOMPARE(saves.count(), 0); // same content as loaded: same hash
}

// TC-STO-027
void tst_Autosave::typedTextSurvivesQuit()
{
    NoteTextEdit* edit = m_editor->textEdit();
    edit->moveCursor(QTextCursor::End);
    QTest::keyClicks(edit, QStringLiteral(" last words"));
    // Quit right away, as Application does: window first, then the context,
    // whose DB thread finishes queued saves before it closes.
    m_window = nullptr;
    m_context = nullptr;

    m_context = notes::test::makeTestContext(m_dir->dirPath());
    QCOMPARE(reload(m_second).bodyText, QStringLiteral("Second last words"));
    openWindow();
}

QTEST_MAIN(tst_Autosave)
#include "tst_autosave.moc"
