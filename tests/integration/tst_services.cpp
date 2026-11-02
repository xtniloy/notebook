// NoteService and FolderService (task 2.7): TC-STO-021 noteCreated is emitted
// exactly once, plus every other signal, All Notes / Recently Deleted handling,
// and that signals arrive on the service's thread. Runs against a real DbThread
// and a migrated database in a temp dir.

#include "FakeClock.h"
#include "TestHelpers.h"
#include "models/SidebarRoles.h"
#include "services/FolderService.h"
#include "services/NoteService.h"
#include "storage/DbThread.h"
#include "storage/FolderRepository.h"
#include "storage/Migrator.h"

#include <QSignalSpy>
#include <QTest>
#include <QThread>

#include <chrono>
#include <memory>

using namespace std::chrono_literals;
using notes::models::Folder;
using notes::models::Note;
using notes::models::NoteSummary;
using notes::services::FolderService;
using notes::services::NoteService;
using notes::storage::Database;
using notes::storage::DbError;
using notes::storage::DbResult;
using notes::storage::DbStatus;
using notes::storage::NoteSort;
using notes::test::await;
using Kind = DbError::Kind;

Q_DECLARE_METATYPE(notes::models::Note)
Q_DECLARE_METATYPE(notes::models::Folder)

namespace {

QStringList titles(const DbResult<QList<NoteSummary>>& notes)
{
    QStringList result;
    if (notes) {
        for (const NoteSummary& note : notes.value()) {
            result.append(note.title);
        }
    }
    return result;
}

} // namespace

class tst_Services : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();

    void createEmitsNoteCreatedOnce();
    void createInAllNotesUsesDefaultFolder();
    void cannotCreateInRecentlyDeleted();
    void updateBodyDerivesTitleAndSnippet();
    void pinMoveTrashRestorePurgeSignals();
    void unchangedCallsEmitNothing();
    void failedCallsEmitNothing();
    void listUnderstandsVirtualFolders();
    void signalsArriveOnServiceThread();
    void folderSignals();
    void purgeExpiredSignalsEachNote();

private:
    std::unique_ptr<notes::test::TempDb> m_dir;
    notes::test::FakeClock m_clock;
    std::unique_ptr<notes::storage::DbThread> m_db;
    std::unique_ptr<NoteService> m_notes;
    std::unique_ptr<FolderService> m_folders;
    qint64 m_defaultFolder = 0;
    qint64 m_work = 0;
};

void tst_Services::initTestCase()
{
    qRegisterMetaType<notes::models::Note>();
    qRegisterMetaType<notes::models::Folder>();
}

void tst_Services::init()
{
    m_dir = std::make_unique<notes::test::TempDb>();
    m_clock.setNowMs(notes::test::FakeClock::kDefaultStartMs);
    m_db = std::make_unique<notes::storage::DbThread>(m_dir->path());
    const DbResult<notes::storage::Migrator::Result> migrated =
        await(m_db->post([](Database& db) { return notes::storage::Migrator().migrate(db); }));
    QVERIFY2(migrated, qPrintable(migrated.error().message));

    m_notes = std::make_unique<NoteService>(*m_db, m_clock);
    m_folders = std::make_unique<FolderService>(*m_db, m_clock);
    const DbResult<Folder> defaultFolder = await(m_folders->ensureDefaultFolder());
    QVERIFY(defaultFolder);
    m_defaultFolder = defaultFolder.value().id;
    const DbResult<Folder> work = await(m_folders->create(QStringLiteral("Work")));
    QVERIFY(work);
    m_work = work.value().id;
}

void tst_Services::cleanup()
{
    m_notes = nullptr;
    m_folders = nullptr;
    m_db = nullptr; // drains the queue, closes the database
    m_dir = nullptr;
}

// TC-STO-021
void tst_Services::createEmitsNoteCreatedOnce()
{
    const QSignalSpy spy(m_notes.get(), &NoteService::noteCreated);
    const QString text =
        QStringLiteral("Shopping\nMilk\nEggs ") + QString::fromLatin1(notes::test::kSecretMarker);
    const DbResult<Note> created =
        await(m_notes->create(m_work, QStringLiteral("<p>Shopping</p>"), text));
    QVERIFY2(created, qPrintable(created.error().message));

    QCOMPARE(spy.count(), 1);
    const auto emitted = spy.at(0).at(0).value<Note>();
    QCOMPARE(emitted, created.value());
    QCOMPARE(emitted.folderId, m_work);
    QCOMPARE(emitted.title, QStringLiteral("Shopping"));
    QCOMPARE(emitted.snippet,
             QStringLiteral("Milk Eggs ") + QString::fromLatin1(notes::test::kSecretMarker));
    QCOMPARE(emitted.bodyText, text);
    QCOMPARE(emitted.createdAt, m_clock.nowMs());
}

// Task 2.7: "Notes" when "All Notes" is selected.
void tst_Services::createInAllNotesUsesDefaultFolder()
{
    const DbResult<Note> created =
        await(m_notes->create(notes::models::kAllNotesFolderId, {}, QStringLiteral("Hello")));
    QVERIFY(created);
    QCOMPARE(created.value().folderId, m_defaultFolder);
}

void tst_Services::cannotCreateInRecentlyDeleted()
{
    const QSignalSpy spy(m_notes.get(), &NoteService::noteCreated);
    const DbResult<Note> created =
        await(m_notes->create(notes::models::kRecentlyDeletedFolderId, {}, QStringLiteral("x")));
    QVERIFY(!created);
    QCOMPARE(created.error().kind, Kind::InvalidArgument);
    QCOMPARE(spy.count(), 0);
}

void tst_Services::updateBodyDerivesTitleAndSnippet()
{
    const Note note = await(m_notes->create(m_work, {}, QStringLiteral("Draft"))).value();
    m_clock.advance(5min);
    QSignalSpy spy(m_notes.get(), &NoteService::noteUpdated);

    const DbResult<Note> updated = await(m_notes->updateBody(
        note.id, QStringLiteral("<h1>Trip</h1>"), QStringLiteral("\n  Trip  \n\nTickets\nHotel")));
    QVERIFY(updated);
    QCOMPARE(updated.value().title, QStringLiteral("Trip"));
    QCOMPARE(updated.value().snippet, QStringLiteral("Tickets Hotel"));
    QCOMPARE(updated.value().body, QStringLiteral("<h1>Trip</h1>"));
    QCOMPARE(updated.value().updatedAt, m_clock.nowMs());
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.takeFirst().at(0).value<Note>(), updated.value());
}

void tst_Services::pinMoveTrashRestorePurgeSignals()
{
    const Note note = await(m_notes->create(m_work, {}, QStringLiteral("Note"))).value();

    QSignalSpy pinned(m_notes.get(), &NoteService::notePinnedChanged);
    QVERIFY(await(m_notes->setPinned(note.id, true)));
    QCOMPARE(pinned.count(), 1);
    QCOMPARE(pinned.at(0), (QList<QVariant>{note.id, true}));

    QSignalSpy moved(m_notes.get(), &NoteService::noteMoved);
    QVERIFY(await(m_notes->moveToFolder(note.id, m_defaultFolder)));
    QCOMPARE(moved.count(), 1);
    QCOMPARE(moved.at(0), (QList<QVariant>{note.id, m_work, m_defaultFolder}));

    QSignalSpy trashed(m_notes.get(), &NoteService::noteTrashed);
    QVERIFY(await(m_notes->trash(note.id)));
    QCOMPARE(trashed.count(), 1);
    QCOMPARE(trashed.at(0).at(0).toLongLong(), note.id);

    QSignalSpy restored(m_notes.get(), &NoteService::noteRestored);
    QVERIFY(await(m_notes->restore(note.id)));
    QCOMPARE(restored.count(), 1);
    QCOMPARE(restored.at(0), (QList<QVariant>{note.id, m_defaultFolder}));

    QSignalSpy purged(m_notes.get(), &NoteService::notePurged);
    QVERIFY(await(m_notes->purge(note.id)));
    QCOMPARE(purged.count(), 1);
    QCOMPARE(purged.at(0).at(0).toLongLong(), note.id);
}

void tst_Services::unchangedCallsEmitNothing()
{
    const Note note = await(m_notes->create(m_work, {}, QStringLiteral("Note"))).value();
    const QSignalSpy pinned(m_notes.get(), &NoteService::notePinnedChanged);
    const QSignalSpy moved(m_notes.get(), &NoteService::noteMoved);
    QVERIFY(await(m_notes->setPinned(note.id, false)));     // already unpinned
    QVERIFY(await(m_notes->moveToFolder(note.id, m_work))); // already there
    QCOMPARE(pinned.count(), 0);
    QCOMPARE(moved.count(), 0);
}

void tst_Services::failedCallsEmitNothing()
{
    const QSignalSpy updated(m_notes.get(), &NoteService::noteUpdated);
    const QSignalSpy trashed(m_notes.get(), &NoteService::noteTrashed);
    const QSignalSpy folderChanged(m_folders.get(), &FolderService::folderChanged);

    const DbResult<Note> missing = await(m_notes->updateBody(999, {}, QStringLiteral("x")));
    QVERIFY(!missing);
    QCOMPARE(missing.error().kind, Kind::NotFound);
    QVERIFY(!await(m_notes->trash(999)));
    QCOMPARE(await(m_folders->rename(m_work, QStringLiteral("   "))).error().kind,
             Kind::InvalidArgument);

    QCOMPARE(updated.count(), 0);
    QCOMPARE(trashed.count(), 0);
    QCOMPARE(folderChanged.count(), 0);
}

void tst_Services::listUnderstandsVirtualFolders()
{
    QVERIFY(await(m_notes->create(m_work, {}, QStringLiteral("In work"))));
    m_clock.advance(1min);
    QVERIFY(await(m_notes->create(m_defaultFolder, {}, QStringLiteral("In notes"))));
    const Note gone = await(m_notes->create(m_work, {}, QStringLiteral("Gone"))).value();
    QVERIFY(await(m_notes->trash(gone.id)));

    QCOMPARE(titles(await(m_notes->list(m_work, NoteSort::Title))),
             QStringList{QStringLiteral("In work")});
    QCOMPARE(titles(await(m_notes->list(notes::models::kAllNotesFolderId, NoteSort::Title))),
             (QStringList{QStringLiteral("In notes"), QStringLiteral("In work")}));
    QCOMPARE(titles(await(m_notes->list(notes::models::kRecentlyDeletedFolderId, NoteSort::Title))),
             QStringList{QStringLiteral("Gone")});
    // Paging passes through.
    QCOMPARE(titles(await(m_notes->list(notes::models::kAllNotesFolderId, NoteSort::Title, 1, 1))),
             QStringList{QStringLiteral("In work")});
}

// ADR 0003: the UI gets its signals on its own thread, never the DB thread.
void tst_Services::signalsArriveOnServiceThread()
{
    QThread* signalThread = nullptr;
    connect(
        m_notes.get(), &NoteService::noteCreated, this,
        [&signalThread] { signalThread = QThread::currentThread(); }, Qt::DirectConnection);
    QVERIFY(await(m_notes->create(m_work, {}, QStringLiteral("x"))));
    QCOMPARE(signalThread, QThread::currentThread());
    QCOMPARE(signalThread, m_notes->thread());
}

void tst_Services::folderSignals()
{
    QSignalSpy created(m_folders.get(), &FolderService::folderCreated);
    const DbResult<Folder> personal = await(m_folders->create(QStringLiteral("Personal")));
    QVERIFY(personal);
    QCOMPARE(created.count(), 1);
    QCOMPARE(created.at(0).at(0).value<Folder>(), personal.value());

    QSignalSpy changed(m_folders.get(), &FolderService::folderChanged);
    QVERIFY(await(m_folders->rename(personal.value().id, QStringLiteral("Home"))));
    QVERIFY(await(m_folders->move(personal.value().id, m_work)));
    QCOMPARE(changed.count(), 2);
    QCOMPARE(changed.at(1).at(0).value<Folder>().parentId, std::optional<qint64>(m_work));

    QSignalSpy reordered(m_folders.get(), &FolderService::foldersReordered);
    QVERIFY(await(m_folders->reorder(std::nullopt, {m_work, m_defaultFolder})));
    QCOMPARE(reordered.count(), 1);

    // Deleting a folder sends its notes to Recently Deleted.
    const Note note = await(m_notes->create(m_work, {}, QStringLiteral("Plan"))).value();
    QSignalSpy deleted(m_folders.get(), &FolderService::folderDeleted);
    QVERIFY(await(m_folders->remove(m_work)));
    QCOMPARE(deleted.count(), 1);
    QCOMPARE(deleted.at(0).at(0).toLongLong(), m_work);
    QCOMPARE(titles(await(m_notes->list(notes::models::kRecentlyDeletedFolderId, NoteSort::Title))),
             QStringList{note.title});
    const DbResult<QList<notes::storage::FolderTreeEntry>> tree = await(m_folders->listTree());
    QVERIFY(tree);
    QCOMPARE(tree.value().size(), 1); // only "Notes" left: Home went with Work
}

// Task 2.10: the list and sidebar drop auto-purged notes like manual ones.
void tst_Services::purgeExpiredSignalsEachNote()
{
    const Note a = await(m_notes->create(m_work, {}, QStringLiteral("a"))).value();
    const Note b = await(m_notes->create(m_work, {}, QStringLiteral("b"))).value();
    QVERIFY(await(m_notes->trash(a.id)));
    QVERIFY(await(m_notes->trash(b.id)));
    m_clock.advance(std::chrono::days(31));

    QSignalSpy purged(m_notes.get(), &NoteService::notePurged);
    const DbResult<QList<qint64>> result = await(m_notes->purgeExpired());
    QVERIFY(result);
    QCOMPARE(result.value(), (QList<qint64>{a.id, b.id}));
    QCOMPARE(purged.count(), 2);
    QCOMPARE(purged.at(0).at(0).toLongLong(), a.id);
    QCOMPARE(purged.at(1).at(0).toLongLong(), b.id);
}

QTEST_GUILESS_MAIN(tst_Services)
#include "tst_services.moc"
