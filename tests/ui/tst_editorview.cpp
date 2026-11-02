// EditorView (task 1.7): TC-UI-016 list selection shows the note and its date,
// TC-UI-017 empty state is read-only. Runs under QT_QPA_PLATFORM=offscreen. The
// main window tests use real notes in a temp database (task 2.8).

#include "TestHelpers.h"
#include "models/Note.h"
#include "models/NoteListRoles.h"
#include "models/SidebarRoles.h"
#include "services/NoteService.h"
#include "ui/EditorView.h"
#include "ui/MainWindow.h"
#include "ui/NoteListView.h"
#include "ui/NoteTextEdit.h"

#include <QDateTime>
#include <QLabel>
#include <QScrollBar>
#include <QStackedWidget>
#include <QTest>

#include <memory>

using notes::ui::EditorView;
namespace roles = notes::models;

namespace {

QLabel* headerOf(const EditorView& view)
{
    return view.findChild<QLabel*>(QStringLiteral("editorHeader"));
}

QLabel* placeholderOf(const EditorView& view)
{
    return view.findChild<QLabel*>(QStringLiteral("editorPlaceholder"));
}

QDateTime modifiedTime()
{
    return {QDate(2026, 10, 28), QTime(21, 41), QTimeZone::UTC};
}

} // namespace

class tst_EditorView : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void emptyStateByDefault();
    void showNoteDisplaysTextAndDate();
    void clearReturnsToEmptyState();
    void lockedNoteHidesContent();
    void headerText();
    void editorIsPlainText();
    void scrollBarAtPaneEdge();

    void mainWindowOpensFirstNote();
    void selectingNoteShowsItInEditor();

private:
    void verifyEmptyState(const EditorView& view);
    [[nodiscard]] std::unique_ptr<notes::ui::MainWindow> showMainWindow();
    // Three notes in "Notes", oldest first; "Third" is the newest.
    void addNotes();

    QList<notes::models::Note> m_notes;

    std::unique_ptr<EditorView> m_view;
    std::unique_ptr<notes::test::TempDb> m_dir;
    std::unique_ptr<notes::app::AppContext> m_context;
};

void tst_EditorView::init()
{
    m_view = std::make_unique<EditorView>();
    m_view->resize(600, 500);
    m_view->show();
    QVERIFY(QTest::qWaitForWindowExposed(m_view.get()));
    m_dir = std::make_unique<notes::test::TempDb>();
    m_context = notes::test::makeTestContext(m_dir->dirPath());
}

void tst_EditorView::cleanup()
{
    m_view = nullptr;
    m_context = nullptr;
    m_dir = nullptr;
}

std::unique_ptr<notes::ui::MainWindow> tst_EditorView::showMainWindow()
{
    auto window = std::make_unique<notes::ui::MainWindow>(*m_context);
    window->show();
    if (!QTest::qWaitForWindowExposed(window.get())) {
        return nullptr;
    }
    return window;
}

void tst_EditorView::addNotes()
{
    notes::test::FakeClock& clock = notes::test::fakeClockOf(*m_context);
    for (const QString& title :
         {QStringLiteral("First"), QStringLiteral("Second"), QStringLiteral("Third")}) {
        clock.advance(std::chrono::minutes(1));
        const auto note = notes::test::await(m_context->notes().create(
            notes::models::kAllNotesFolderId, {}, title + QStringLiteral("\nBody of ") + title));
        QVERIFY(note);
        m_notes.append(note.value());
    }
}

void tst_EditorView::verifyEmptyState(const EditorView& view)
{
    QVERIFY(!view.hasNote());
    QVERIFY(placeholderOf(view)->isVisible());
    QCOMPARE(placeholderOf(view)->text(), QStringLiteral("No note selected"));
    QVERIFY(!headerOf(view)->isVisible());
    QVERIFY(view.textEdit()->isReadOnly());
    QVERIFY(view.textEdit()->toPlainText().isEmpty());
}

// TC-UI-017
void tst_EditorView::emptyStateByDefault()
{
    verifyEmptyState(*m_view);
}

void tst_EditorView::showNoteDisplaysTextAndDate()
{
    const QString text = QStringLiteral("Shopping List\nMilk\n") + notes::test::kSecretMarker;
    m_view->showNote(text, modifiedTime());

    QVERIFY(m_view->hasNote());
    QVERIFY(m_view->textEdit()->isVisible());
    QVERIFY(!placeholderOf(*m_view)->isVisible());
    QVERIFY(!m_view->textEdit()->isReadOnly());
    QCOMPARE(m_view->textEdit()->toPlainText(), text);
    QVERIFY(headerOf(*m_view)->isVisible());
    QCOMPARE(headerOf(*m_view)->text(), EditorView::headerText(modifiedTime()));
    QCOMPARE(headerOf(*m_view)->alignment() & Qt::AlignHCenter, Qt::AlignHCenter);
    // A new note starts with a clean undo history.
    QVERIFY(!m_view->textEdit()->document()->isUndoAvailable());
}

// TC-UI-017: after a note was shown.
void tst_EditorView::clearReturnsToEmptyState()
{
    m_view->showNote(QStringLiteral("Some note"), modifiedTime());
    m_view->clear();
    verifyEmptyState(*m_view);
}

void tst_EditorView::lockedNoteHidesContent()
{
    m_view->showNote(QString::fromLatin1(notes::test::kSecretMarker), modifiedTime());
    m_view->showLockedNote(modifiedTime());

    QVERIFY(m_view->hasNote());
    QCOMPARE(placeholderOf(*m_view)->text(), QStringLiteral("This note is locked."));
    QVERIFY(placeholderOf(*m_view)->isVisible());
    QCOMPARE(headerOf(*m_view)->text(), EditorView::headerText(modifiedTime()));
    QVERIFY(m_view->textEdit()->isReadOnly());
    QVERIFY(
        !m_view->textEdit()->toPlainText().contains(QLatin1StringView(notes::test::kSecretMarker)));
}

void tst_EditorView::headerText()
{
    const QLocale english(QLocale::English, QLocale::UnitedStates);
    const QString text = EditorView::headerText(modifiedTime(), english);
    QVERIFY2(text.startsWith(QStringLiteral("Wednesday, October 28, 2026 at ")), qPrintable(text));
    QVERIFY2(text.endsWith(english.toString(QTime(21, 41), QLocale::ShortFormat)),
             qPrintable(text));
}

void tst_EditorView::editorIsPlainText()
{
    QVERIFY(!m_view->textEdit()->acceptRichText());
}

// UI review: the scroll bar sits 3 px from the pane's right edge, not against
// the text; the text keeps 24 px of space on the right, and the date stays
// centered over it.
void tst_EditorView::scrollBarAtPaneEdge()
{
    QString text;
    for (int line = 0; line < 200; ++line) {
        text += QStringLiteral("Line %1\n").arg(line);
    }
    m_view->showNote(text, modifiedTime());
    QCoreApplication::processEvents();
    const auto* edit = m_view->textEdit();
    const QScrollBar* bar = edit->verticalScrollBar();
    QVERIFY(bar->isVisible());
    QVERIFY(bar->maximum() > 0);

    const int barRight = bar->mapTo(m_view.get(), QPoint(bar->width(), 0)).x();
    QCOMPARE(m_view->width() - barRight, 3);
    const QWidget* viewport = edit->viewport();
    const int textRight = viewport->mapTo(m_view.get(), QPoint(viewport->width(), 0)).x();
    QCOMPARE(m_view->width() - textRight, 24);

    const QLabel* header = headerOf(*m_view);
    const int headerCenter = header->mapTo(m_view.get(), header->contentsRect().center()).x();
    const int textLeft = viewport->mapTo(m_view.get(), QPoint(0, 0)).x();
    QVERIFY(qAbs(headerCenter - ((textLeft + textRight) / 2)) <= 1);
}

void tst_EditorView::mainWindowOpensFirstNote()
{
    addNotes();
    const auto window = showMainWindow();
    QVERIFY(window);
    const auto* list = window->findChild<notes::ui::NoteListView*>(QStringLiteral("noteListPane"));
    const auto* editor = window->findChild<EditorView*>(QStringLiteral("editorPane"));
    QVERIFY(list && editor);
    // Folders, then notes, then the note's body load asynchronously.
    QTRY_COMPARE(list->currentNote(), std::optional<qint64>(m_notes.constLast().id));
    QTRY_VERIFY(editor->hasNote());
    QCOMPARE(editor->textEdit()->toPlainText(), m_notes.constLast().bodyText);
    QCOMPARE(list->currentIndex().data().toString(), QStringLiteral("Third"));
}

// TC-UI-016
void tst_EditorView::selectingNoteShowsItInEditor()
{
    addNotes();
    const auto window = showMainWindow();
    QVERIFY(window);
    auto* list = window->findChild<notes::ui::NoteListView*>(QStringLiteral("noteListPane"));
    const auto* editor = window->findChild<EditorView*>(QStringLiteral("editorPane"));
    QTRY_VERIFY(list->currentNote().has_value());

    const notes::models::Note& first = m_notes.constFirst();
    const QAbstractItemModel* model = list->model();
    const QModelIndex target =
        model->match(model->index(0, 0), roles::kNoteIdRole, first.id, 1, Qt::MatchExactly)
            .value(0);
    QVERIFY(target.isValid());
    QTest::mouseClick(list->viewport(), Qt::LeftButton, {}, list->visualRect(target).center());

    QCOMPARE(list->currentNote(), std::optional<qint64>(first.id));
    QTRY_COMPARE(editor->textEdit()->toPlainText(), first.bodyText);
    QCOMPARE(headerOf(*editor)->text(),
             EditorView::headerText(QDateTime::fromMSecsSinceEpoch(first.updatedAt)));
}

QTEST_MAIN(tst_EditorView)
#include "tst_editorview.moc"
