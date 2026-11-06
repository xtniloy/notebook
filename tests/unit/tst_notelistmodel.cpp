// NoteListModel (task 2.8): TC-STO-022 model contract while notes change,
// TC-STO-023 lazy loading, TC-STO-024 an edit changes one row only. Runs against
// a real DbThread and a migrated database in a temp dir.

#include "FakeClock.h"
#include "TestHelpers.h"
#include "models/NoteListModel.h"
#include "models/NoteListRoles.h"
#include "models/SidebarRoles.h"
#include "services/FolderService.h"
#include "services/NoteService.h"
#include "storage/DbThread.h"
#include "storage/Migrator.h"
#include "storage/NoteRepository.h"

#include <QAbstractItemModelTester>
#include <QSignalSpy>
#include <QTest>

#include <chrono>
#include <memory>

using namespace std::chrono_literals;
using notes::models::Note;
using notes::models::NoteListModel;
using notes::services::NoteService;
using notes::storage::Database;
using notes::storage::DbResult;
using notes::storage::DbStatus;
using notes::storage::NoteSort;
using notes::test::await;
namespace roles = notes::models;

namespace {

// Titles of the note rows, in order; headers as "[Header]".
QStringList rows(const QAbstractItemModel& model)
{
    QStringList result;
    for (int row = 0; row < model.rowCount(); ++row) {
        const QModelIndex index = model.index(row, 0);
        const QString text = index.data().toString();
        result.append(index.data(roles::kIsSectionHeaderRole).toBool()
                          ? QLatin1Char('[') + text + QLatin1Char(']')
                          : text);
    }
    return result;
}

} // namespace

class tst_NoteListModel : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void loadsFolderWithSections();
    void modelContractWhileNotesChange();
    void initialLoadFetchesOnePage();
    void editingTitleChangesOneRow();
    void editingMovesNoteToTop();
    void followsFolderMovesAndTrash();
    void recentlyDeletedList();
    void daysRemaining();
    void sortByTitle();
    void staleResultsAreDropped();
    void noSecondLoadDuringReset();

private:
    Note addNote(qint64 folderId, const QString& text);
    // Waits until the model has loaded its page and settled.
    void settle(const NoteListModel& model);

    std::unique_ptr<notes::test::TempDb> m_dir;
    notes::test::FakeClock m_clock;
    std::unique_ptr<notes::storage::DbThread> m_db;
    std::unique_ptr<NoteService> m_notes;
    std::unique_ptr<notes::services::FolderService> m_folders;
    qint64 m_default = 0;
    qint64 m_work = 0;
};

void tst_NoteListModel::init()
{
    m_dir = std::make_unique<notes::test::TempDb>();
    m_clock.setNowMs(notes::test::FakeClock::kDefaultStartMs);
    m_db = std::make_unique<notes::storage::DbThread>(m_dir->path());
    QVERIFY(await(m_db->post([](Database& db) { return notes::storage::Migrator().migrate(db); })));
    m_notes = std::make_unique<NoteService>(*m_db, m_clock);
    m_folders = std::make_unique<notes::services::FolderService>(*m_db, m_clock);
    m_default = await(m_folders->ensureDefaultFolder()).value().id;
    m_work = await(m_folders->create(QStringLiteral("Work"))).value().id;
}

void tst_NoteListModel::cleanup()
{
    m_notes = nullptr;
    m_folders = nullptr;
    m_db = nullptr;
    m_dir = nullptr;
}

Note tst_NoteListModel::addNote(qint64 folderId, const QString& text)
{
    m_clock.advance(1min);
    const DbResult<Note> note = await(m_notes->create(folderId, {}, text));
    if (!note) {
        qFatal("creating a note failed");
    }
    return note.value();
}

void tst_NoteListModel::settle(const NoteListModel& model)
{
    // Round trips through the DB thread finish in order: one more empty job
    // that comes back means every reload posted before it is done too.
    QTRY_VERIFY(!model.isLoading());
    QVERIFY(await(m_db->post([](Database&) { return DbStatus(); })));
    QCoreApplication::processEvents();
}

void tst_NoteListModel::loadsFolderWithSections()
{
    addNote(m_work, QStringLiteral("Old\nfirst words"));
    m_clock.advance(2 * 24h);
    const Note recent = addNote(m_work, QStringLiteral("Recent"));
    addNote(m_default, QStringLiteral("Elsewhere"));

    NoteListModel model(*m_notes, m_clock);
    model.setFolder(m_work);
    settle(model);
    QCOMPARE(rows(model),
             (QStringList{QStringLiteral("[Today]"), QStringLiteral("Recent"),
                          QStringLiteral("[Previous 7 Days]"), QStringLiteral("Old")}));

    const QModelIndex note = model.index(1);
    QCOMPARE(note.data(roles::kNoteIdRole).toLongLong(), recent.id);
    QCOMPARE(note.data(roles::kModifiedRole).toLongLong(), recent.updatedAt);
    QCOMPARE(note.data(roles::kSectionRole).toString(), QStringLiteral("Today"));
    QVERIFY(!note.data(roles::kTimeTextRole).toString().isEmpty());
    QCOMPARE(model.index(3).data(roles::kPreviewRole).toString(), QStringLiteral("first words"));
    // Headers can't be selected, so arrow keys and clicks pass over them.
    QVERIFY(!(model.flags(model.index(0)) & Qt::ItemIsSelectable));
    QVERIFY(model.flags(note) & Qt::ItemIsSelectable);
}

// TC-STO-022 (note list half; tst_foldertreemodel has the sidebar's).
void tst_NoteListModel::modelContractWhileNotesChange()
{
    const Note a = addNote(m_work, QStringLiteral("A"));
    const Note b = addNote(m_work, QStringLiteral("B"));
    addNote(m_default, QStringLiteral("Elsewhere"));
    m_clock.advance(3 * 24h);

    NoteListModel model(*m_notes, m_clock);
    const QAbstractItemModelTester tester(&model,
                                          QAbstractItemModelTester::FailureReportingMode::QtTest);
    model.setFolder(m_work);
    settle(model);
    const Note c = addNote(m_work, QStringLiteral("C")); // joins a loaded list
    settle(model);
    QCOMPARE(rows(model), (QStringList{QStringLiteral("[Today]"), QStringLiteral("C"),
                                       QStringLiteral("[Previous 7 Days]"), QStringLiteral("B"),
                                       QStringLiteral("A")}));

    QVERIFY(await(m_notes->setPinned(a.id, true)));
    QVERIFY(await(m_notes->updateBody(b.id, {}, QStringLiteral("B edited"))));
    QVERIFY(await(m_notes->moveToFolder(c.id, m_default)));
    settle(model);
    QCOMPARE(rows(model), (QStringList{QStringLiteral("[Pinned]"), QStringLiteral("A"),
                                       QStringLiteral("[Today]"), QStringLiteral("B edited")}));

    QVERIFY(await(m_notes->trash(a.id)));
    QVERIFY(await(m_notes->moveToFolder(c.id, m_work)));
    settle(model);
    QCOMPARE(rows(model), (QStringList{QStringLiteral("[Today]"), QStringLiteral("C"),
                                       QStringLiteral("B edited")}));

    QVERIFY(await(m_notes->trash(b.id)));
    QVERIFY(await(m_notes->purge(b.id)));
    QVERIFY(await(m_notes->trash(c.id)));
    settle(model);
    QCOMPARE(model.rowCount(), 0);

    model.setFolder(roles::kAllNotesFolderId);
    settle(model);
    QCOMPARE(rows(model),
             (QStringList{QStringLiteral("[Previous 7 Days]"), QStringLiteral("Elsewhere")}));
}

// TC-STO-023
void tst_NoteListModel::initialLoadFetchesOnePage()
{
    constexpr int kNotes = 1'000;
    const qint64 folder = m_work;
    const notes::app::IClock* clock = &m_clock;
    QVERIFY(await(m_db->post([folder, clock](Database& db) -> DbStatus {
        notes::storage::NoteRepository repo(db, *clock);
        for (int i = 0; i < kNotes; ++i) {
            const auto note = repo.create(folder, {.title = QString::number(i),
                                                   .snippet = {},
                                                   .body = {},
                                                   .bodyText = QString::number(i),
                                                   .thumbnailUuid = {}});
            if (!note) {
                return note.error();
            }
        }
        return {};
    })));

    NoteListModel model(*m_notes, m_clock);
    QSignalSpy loading(&model, &NoteListModel::loadingChanged);
    model.setFolder(m_work);
    settle(model);
    QCOMPARE(model.loadedNoteCount(), NoteListModel::kPageSize);
    QVERIFY(model.loadedNoteCount() <= 200);
    QVERIFY(model.canFetchMore({}));

    // The view asks for more as the user scrolls.
    while (model.canFetchMore({})) {
        model.fetchMore({});
        settle(model);
    }
    QCOMPARE(model.loadedNoteCount(), kNotes);
    QCOMPARE(model.rowCount(), kNotes + 1); // all made "now": one "Today" header

    // Ids are unique: no page overlapped another.
    QSet<qint64> ids;
    for (int row = 1; row < model.rowCount(); ++row) {
        ids.insert(model.index(row).data(roles::kNoteIdRole).toLongLong());
    }
    QCOMPARE(ids.size(), kNotes);
}

// TC-STO-024
void tst_NoteListModel::editingTitleChangesOneRow()
{
    addNote(m_work, QStringLiteral("Older"));
    const Note top = addNote(m_work, QStringLiteral("Draft"));
    NoteListModel model(*m_notes, m_clock);
    model.setFolder(m_work);
    settle(model);
    QCOMPARE(rows(model), (QStringList{QStringLiteral("[Today]"), QStringLiteral("Draft"),
                                       QStringLiteral("Older")}));

    QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
    const QSignalSpy inserted(&model, &QAbstractItemModel::rowsInserted);
    const QSignalSpy removed(&model, &QAbstractItemModel::rowsRemoved);
    const QSignalSpy reset(&model, &QAbstractItemModel::modelReset);
    QVERIFY(await(m_notes->updateBody(top.id, {}, QStringLiteral("Final title"))));
    settle(model);

    QCOMPARE(changed.count(), 1);
    QCOMPARE(changed.at(0).at(0).value<QModelIndex>(), model.index(1));
    QCOMPARE(changed.at(0).at(1).value<QModelIndex>(), model.index(1));
    QCOMPARE(inserted.count(), 0);
    QCOMPARE(removed.count(), 0);
    QCOMPARE(reset.count(), 0);
    QCOMPARE(model.index(1).data().toString(), QStringLiteral("Final title"));
}

void tst_NoteListModel::editingMovesNoteToTop()
{
    const Note old = addNote(m_work, QStringLiteral("Old"));
    addNote(m_work, QStringLiteral("New"));
    NoteListModel model(*m_notes, m_clock);
    model.setFolder(m_work);
    settle(model);

    const QSignalSpy reset(&model, &QAbstractItemModel::modelReset);
    m_clock.advance(1min);
    QVERIFY(await(m_notes->updateBody(old.id, {}, QStringLiteral("Old, edited"))));
    settle(model);
    QCOMPARE(rows(model), (QStringList{QStringLiteral("[Today]"), QStringLiteral("Old, edited"),
                                       QStringLiteral("New")}));
    QCOMPARE(reset.count(), 0);
}

void tst_NoteListModel::followsFolderMovesAndTrash()
{
    const Note note = addNote(m_default, QStringLiteral("Wanderer"));
    NoteListModel model(*m_notes, m_clock);
    model.setFolder(m_work);
    settle(model);
    QCOMPARE(model.rowCount(), 0);

    QVERIFY(await(m_notes->moveToFolder(note.id, m_work)));
    settle(model);
    QCOMPARE(rows(model), (QStringList{QStringLiteral("[Today]"), QStringLiteral("Wanderer")}));

    QVERIFY(await(m_notes->trash(note.id)));
    settle(model);
    QCOMPARE(model.rowCount(), 0);

    QVERIFY(await(m_notes->restore(note.id)));
    settle(model);
    QCOMPARE(rows(model), (QStringList{QStringLiteral("[Today]"), QStringLiteral("Wanderer")}));
}

void tst_NoteListModel::recentlyDeletedList()
{
    const Note first = addNote(m_work, QStringLiteral("First"));
    const Note second = addNote(m_work, QStringLiteral("Second"));
    QVERIFY(await(m_notes->trash(first.id)));
    m_clock.advance(1min);
    QVERIFY(await(m_notes->trash(second.id)));

    NoteListModel model(*m_notes, m_clock);
    model.setFolder(roles::kRecentlyDeletedFolderId);
    settle(model);
    // Most recently deleted first.
    QCOMPARE(rows(model), (QStringList{QStringLiteral("[Today]"), QStringLiteral("Second"),
                                       QStringLiteral("First")}));

    // A note trashed while the list is open joins it at the top.
    const Note third = addNote(m_work, QStringLiteral("Third"));
    QVERIFY(await(m_notes->trash(third.id)));
    QVERIFY(await(m_notes->restore(first.id)));
    settle(model);
    QCOMPARE(rows(model), (QStringList{QStringLiteral("[Today]"), QStringLiteral("Third"),
                                       QStringLiteral("Second")}));
}

// Task 2.10: Recently Deleted rows count down to the purge.
void tst_NoteListModel::daysRemaining()
{
    constexpr qint64 kDay = 24LL * 60 * 60 * 1000;
    const qint64 trashed = notes::test::FakeClock::kDefaultStartMs;
    QCOMPARE(NoteListModel::daysRemainingText(trashed, trashed),
             QStringLiteral("30 days remaining"));
    QCOMPARE(NoteListModel::daysRemainingText(trashed, trashed + 1),
             QStringLiteral("30 days remaining"));
    QCOMPARE(NoteListModel::daysRemainingText(trashed, trashed + (29 * kDay)),
             QStringLiteral("1 day remaining"));
    QCOMPARE(NoteListModel::daysRemainingText(trashed, trashed + (29 * kDay) + 1),
             QStringLiteral("1 day remaining"));
    QCOMPARE(NoteListModel::daysRemainingText(trashed, trashed + (30 * kDay)),
             QStringLiteral("0 days remaining"));

    const Note note = addNote(m_work, QStringLiteral("Doomed"));
    QVERIFY(await(m_notes->trash(note.id)));
    m_clock.advance(std::chrono::days(3));
    NoteListModel model(*m_notes, m_clock);
    model.setFolder(roles::kRecentlyDeletedFolderId);
    settle(model);
    QCOMPARE(model.index(1).data(roles::kTimeTextRole).toString(),
             QStringLiteral("27 days remaining"));
}

void tst_NoteListModel::sortByTitle()
{
    addNote(m_work, QStringLiteral("banana"));
    addNote(m_work, QStringLiteral("Apple"));
    const Note cherry = addNote(m_work, QStringLiteral("cherry"));
    NoteListModel model(*m_notes, m_clock);
    model.setFolder(m_work, NoteSort::Title);
    settle(model);
    QCOMPARE(rows(model), (QStringList{QStringLiteral("[Notes]"), QStringLiteral("Apple"),
                                       QStringLiteral("banana"), QStringLiteral("cherry")}));

    // A renamed note moves to its new place in the same order the database uses.
    QVERIFY(await(m_notes->updateBody(cherry.id, {}, QStringLiteral("Avocado"))));
    addNote(m_work, QStringLiteral("Blueberry"));
    settle(model);
    QCOMPARE(rows(model), (QStringList{QStringLiteral("[Notes]"), QStringLiteral("Apple"),
                                       QStringLiteral("Avocado"), QStringLiteral("banana"),
                                       QStringLiteral("Blueberry")}));
}

void tst_NoteListModel::staleResultsAreDropped()
{
    addNote(m_work, QStringLiteral("Work note"));
    addNote(m_default, QStringLiteral("Default note"));
    NoteListModel model(*m_notes, m_clock);
    // Switching before the first answer arrives: only the last folder shows.
    model.setFolder(m_default);
    model.setFolder(m_work);
    settle(model);
    QCOMPARE(rows(model), (QStringList{QStringLiteral("[Today]"), QStringLiteral("Work note")}));
}

// While a folder's list resets, its first page is on the way already: a view
// asking for more then (QAbstractItemModelTester does) must not load it again,
// nor get rows inserted inside the reset when the result is ready at once.
void tst_NoteListModel::noSecondLoadDuringReset()
{
    addNote(m_work, QStringLiteral("A"));
    NoteListModel model(*m_notes, m_clock);
    bool couldFetch = true;
    connect(&model, &QAbstractItemModel::modelReset, this,
            [&] { couldFetch = model.canFetchMore({}); });
    model.setFolder(m_work);
    QVERIFY(!couldFetch);
    settle(model);
    QCOMPARE(rows(model), (QStringList{QStringLiteral("[Today]"), QStringLiteral("A")}));
}

QTEST_MAIN(tst_NoteListModel)
#include "tst_notelistmodel.moc"
