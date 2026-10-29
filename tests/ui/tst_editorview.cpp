// EditorView (task 1.7): TC-UI-016 list selection shows the note and its date,
// TC-UI-017 empty state is read-only. Runs under QT_QPA_PLATFORM=offscreen.

#include "TestHelpers.h"
#include "models/FakeNoteListModel.h"
#include "models/NoteListRoles.h"
#include "ui/EditorView.h"
#include "ui/MainWindow.h"
#include "ui/NoteListView.h"
#include "ui/NoteTextEdit.h"

#include <QDateTime>
#include <QLabel>
#include <QStackedWidget>
#include <QTest>

#include <memory>

using notes::models::FakeNoteListModel;
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

    void mainWindowOpensFirstNote();
    void selectingNoteShowsItInEditor();
    void selectingLockedNoteShowsPlaceholder();

private:
    void verifyEmptyState(const EditorView& view);
    [[nodiscard]] std::unique_ptr<notes::ui::MainWindow> showMainWindow();

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

void tst_EditorView::mainWindowOpensFirstNote()
{
    const auto window = showMainWindow();
    QVERIFY(window);
    const auto* list = window->findChild<notes::ui::NoteListView*>(QStringLiteral("noteListPane"));
    const auto* editor = window->findChild<EditorView*>(QStringLiteral("editorPane"));
    QVERIFY(list && editor);
    QVERIFY(list->currentNote());
    QVERIFY(editor->hasNote());
    QCOMPARE(editor->textEdit()->toPlainText().section(QLatin1Char('\n'), 0, 0),
             list->currentIndex().data().toString()); // title on the first line
}

// TC-UI-016
void tst_EditorView::selectingNoteShowsItInEditor()
{
    const auto window = showMainWindow();
    QVERIFY(window);
    auto* list = window->findChild<notes::ui::NoteListView*>(QStringLiteral("noteListPane"));
    const auto* editor = window->findChild<EditorView*>(QStringLiteral("editorPane"));
    const auto* model = qobject_cast<const FakeNoteListModel*>(list->model());
    QVERIFY(model);

    // A visible, unlocked note other than the current one.
    QModelIndex target;
    for (int row = 0; row < model->rowCount() && !target.isValid(); ++row) {
        const QModelIndex index = model->index(row);
        if (index.data(roles::kNoteIdRole).isValid() && index != list->currentIndex() &&
            !index.data(roles::kLockedRole).toBool()) {
            target = index;
        }
    }
    QVERIFY(target.isValid());
    QTest::mouseClick(list->viewport(), Qt::LeftButton, {}, list->visualRect(target).center());

    const qint64 id = target.data(roles::kNoteIdRole).toLongLong();
    QCOMPARE(list->currentNote(), std::optional<qint64>(id));
    const auto found = model->note(id);
    if (!found) {
        QFAIL("note not found");
    }
    const FakeNoteListModel::FakeNote& note = *found;
    QCOMPARE(editor->textEdit()->toPlainText(), note.text);
    QCOMPARE(headerOf(*editor)->text(),
             EditorView::headerText(QDateTime::fromMSecsSinceEpoch(note.modifiedMs)));
}

void tst_EditorView::selectingLockedNoteShowsPlaceholder()
{
    const auto window = showMainWindow();
    QVERIFY(window);
    auto* list = window->findChild<notes::ui::NoteListView*>(QStringLiteral("noteListPane"));
    const auto* editor = window->findChild<EditorView*>(QStringLiteral("editorPane"));
    const QAbstractItemModel* model = list->model();

    const QModelIndex locked =
        model->match(model->index(0, 0), roles::kLockedRole, true, 1, Qt::MatchExactly).value(0);
    QVERIFY(locked.isValid());
    QVERIFY(list->selectNote(locked.data(roles::kNoteIdRole).toLongLong()));

    QCOMPARE(placeholderOf(*editor)->text(), QStringLiteral("This note is locked."));
    QVERIFY(editor->textEdit()->toPlainText().isEmpty());
    QVERIFY(headerOf(*editor)->isVisible());
}

QTEST_MAIN(tst_EditorView)
#include "tst_editorview.moc"
