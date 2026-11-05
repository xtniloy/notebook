// Images stored inside the database (task 3.7): TC-EDT-021 they survive a
// restart with no loose files, TC-EDT-025 one deleted from the text is gone
// after the save, and the note list's thumbnail (the data behind TC-EDT-026).
// NoteService on a real DbThread and database.

#include "FakeClock.h"
#include "TestHelpers.h"
#include "models/Attachment.h"
#include "models/Note.h"
#include "models/SidebarRoles.h"
#include "models/Uuid.h"
#include "services/FolderService.h"
#include "services/NoteService.h"
#include "services/NoteText.h"
#include "storage/Database.h"
#include "storage/DbThread.h"
#include "storage/Migrator.h"

#include <QDir>
#include <QTest>

#include <memory>

using notes::models::Attachment;
using notes::models::Note;
using notes::services::NoteService;
using notes::services::NoteText;
using notes::services::OpenedNote;
using notes::storage::Database;
using notes::storage::DbResult;
using notes::test::await;

namespace {

Attachment image(const QByteArray& data, const QByteArray& thumbnail = "thumb")
{
    Attachment attachment;
    attachment.uuid = notes::models::newUuid();
    attachment.fileName = QStringLiteral("photo.png");
    attachment.mimeType = QStringLiteral("image/png");
    attachment.data = data;
    attachment.sizeBytes = data.size();
    attachment.thumbnail = thumbnail;
    return attachment;
}

// A body in the note format showing `images` (uuids) after some text.
QString bodyWith(const QStringList& uuids)
{
    QString runs = QStringLiteral("{\"text\":\"Trip\"}");
    for (const QString& uuid : uuids) {
        runs += QStringLiteral(",{\"image\":\"attachment://%1\"}").arg(uuid);
    }
    return QStringLiteral("{\"blocks\":[{\"runs\":[%1]}]}").arg(runs);
}

} // namespace

class tst_Attachments : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void savingStoresNewImages();
    void imagesSurviveRestartInsideTheDatabase();
    void deletedImageIsRemovedOnSave();
    void imageBroughtBackIsStoredAgain();
    void thumbnailIsTheFirstImage();
    void anotherNotesImageIsNeverTaken();
    void failedSaveChangesNoAttachments();
    void duplicateGetsItsOwnCopies();
    void bodyNamesItsImages();

private:
    void openDatabase();
    [[nodiscard]] OpenedNote open(qint64 id);
    [[nodiscard]] Note save(qint64 id, const QStringList& uuids,
                            const QList<Attachment>& attachments);

    std::unique_ptr<notes::test::TempDb> m_dir;
    notes::test::FakeClock m_clock;
    std::unique_ptr<notes::storage::DbThread> m_db;
    std::unique_ptr<NoteService> m_notes;
    Note m_note;
};

void tst_Attachments::init()
{
    m_dir = std::make_unique<notes::test::TempDb>();
    m_clock.setNowMs(notes::test::FakeClock::kDefaultStartMs);
    openDatabase();
    QVERIFY(await(notes::services::FolderService(*m_db, m_clock).ensureDefaultFolder()));
    m_note = await(m_notes->create(notes::models::kAllNotesFolderId, {}, QStringLiteral("Trip")))
                 .value();
}

void tst_Attachments::cleanup()
{
    m_notes = nullptr;
    m_db = nullptr;
    m_dir = nullptr;
}

void tst_Attachments::openDatabase()
{
    m_notes = nullptr;
    m_db = nullptr; // the old one closes first
    m_db = std::make_unique<notes::storage::DbThread>(m_dir->path());
    const DbResult<notes::storage::Migrator::Result> migrated =
        await(m_db->post([](Database& db) { return notes::storage::Migrator().migrate(db); }));
    QVERIFY2(migrated, qPrintable(migrated.error().message));
    m_notes = std::make_unique<NoteService>(*m_db, m_clock);
}

OpenedNote tst_Attachments::open(qint64 id)
{
    const DbResult<OpenedNote> opened = await(m_notes->open(id));
    if (!opened) {
        qFatal("open failed: %s", qPrintable(opened.error().message));
    }
    return opened.value();
}

Note tst_Attachments::save(qint64 id, const QStringList& uuids,
                           const QList<Attachment>& attachments)
{
    const DbResult<Note> saved =
        await(m_notes->updateBody(id, bodyWith(uuids), QStringLiteral("Trip"), attachments));
    if (!saved) {
        qFatal("save failed: %s", qPrintable(saved.error().message));
    }
    return saved.value();
}

void tst_Attachments::savingStoresNewImages()
{
    const Attachment photo = image(QByteArrayLiteral("PNG-BYTES-1"));
    const Note saved = save(m_note.id, {photo.uuid}, {photo});
    QCOMPARE(saved.thumbnailUuid, photo.uuid);
    QCOMPARE(saved.thumbnail, photo.thumbnail);

    OpenedNote opened = open(m_note.id);
    QCOMPARE(opened.attachments.size(), 1);
    const Attachment stored = opened.attachments.first(); // a copy: `opened` is reused below
    QCOMPARE(stored.uuid, photo.uuid);
    QCOMPARE(stored.noteId, m_note.id);
    QCOMPARE(stored.data, photo.data);
    QCOMPARE(stored.thumbnail, photo.thumbnail);
    QCOMPARE(stored.fileName, photo.fileName);
    QCOMPARE(stored.createdAt, notes::test::FakeClock::kDefaultStartMs);

    // Saving again (the editor sends its copies every time) adds nothing.
    (void)save(m_note.id, {photo.uuid}, {photo});
    opened = open(m_note.id);
    QCOMPARE(opened.attachments.size(), 1);
    QCOMPARE(opened.attachments.first().id, stored.id);

    // The list carries the thumbnail, not the body.
    const auto listed = await(
        m_notes->list(notes::models::kAllNotesFolderId, notes::storage::NoteSort::DateEdited));
    QVERIFY(listed);
    QCOMPARE(listed.value().first().thumbnail, photo.thumbnail);
    QCOMPARE(listed.value().first().thumbnailUuid, photo.uuid);
}

// TC-EDT-021
void tst_Attachments::imagesSurviveRestartInsideTheDatabase()
{
    const QByteArray bytes = QByteArrayLiteral("\x89PNG pretend image data ") +
                             QByteArray(4096, 'x') + QByteArray(notes::test::kSecretMarker);
    const Attachment photo = image(bytes);
    (void)save(m_note.id, {photo.uuid}, {photo});

    openDatabase(); // as after a restart
    const OpenedNote opened = open(m_note.id);
    QCOMPARE(opened.attachments.size(), 1);
    QCOMPARE(opened.attachments.first().data, bytes);
    QCOMPARE(opened.note.thumbnailUuid, photo.uuid);

    // Nothing next to the database but its own files.
    const QStringList files = QDir(m_dir->dirPath()).entryList(QDir::Files | QDir::NoDotAndDotDot);
    for (const QString& file : files) {
        QVERIFY2(file.startsWith(QStringLiteral("notes.db")), qPrintable(file));
    }
}

// TC-EDT-025
void tst_Attachments::deletedImageIsRemovedOnSave()
{
    const Attachment first = image(QByteArrayLiteral("ONE"));
    const Attachment second = image(QByteArrayLiteral("TWO"));
    (void)save(m_note.id, {first.uuid, second.uuid}, {first, second});
    QCOMPARE(open(m_note.id).attachments.size(), 2);

    const Note saved = save(m_note.id, {second.uuid}, {second});
    const OpenedNote opened = open(m_note.id);
    QCOMPARE(opened.attachments.size(), 1);
    QCOMPARE(opened.attachments.first().uuid, second.uuid);
    QCOMPARE(saved.thumbnailUuid, second.uuid);

    const Note empty = save(m_note.id, {}, {});
    QVERIFY(open(m_note.id).attachments.isEmpty());
    QVERIFY(empty.thumbnailUuid.isEmpty());
    QVERIFY(empty.thumbnail.isEmpty());
}

// Undo after a save: the editor still has the data, so the next save stores
// the image again.
void tst_Attachments::imageBroughtBackIsStoredAgain()
{
    const Attachment photo = image(QByteArrayLiteral("BACK"));
    (void)save(m_note.id, {photo.uuid}, {photo});
    (void)save(m_note.id, {}, {});
    QVERIFY(open(m_note.id).attachments.isEmpty());
    (void)save(m_note.id, {photo.uuid}, {photo});
    const OpenedNote opened = open(m_note.id);
    QCOMPARE(opened.attachments.size(), 1);
    QCOMPARE(opened.attachments.first().data, photo.data);

    // A body naming an image nobody has data for keeps nothing, and fails
    // nothing: it shows as missing.
    const Note saved = save(m_note.id, {notes::models::newUuid()}, {});
    QVERIFY(open(m_note.id).attachments.isEmpty());
    QVERIFY(saved.thumbnailUuid.isEmpty());
}

void tst_Attachments::thumbnailIsTheFirstImage()
{
    const Attachment a = image(QByteArrayLiteral("A"), QByteArrayLiteral("thumb-a"));
    const Attachment b = image(QByteArrayLiteral("B"), QByteArrayLiteral("thumb-b"));
    QCOMPARE(save(m_note.id, {a.uuid, b.uuid}, {a, b}).thumbnail, a.thumbnail);
    // B moved above A: B's thumbnail, though A was stored first.
    QCOMPARE(save(m_note.id, {b.uuid, a.uuid}, {a, b}).thumbnail, b.thumbnail);
}

void tst_Attachments::anotherNotesImageIsNeverTaken()
{
    const Attachment photo = image(QByteArrayLiteral("MINE"));
    (void)save(m_note.id, {photo.uuid}, {photo});
    const Note other =
        await(m_notes->create(notes::models::kAllNotesFolderId, {}, QStringLiteral("Other")))
            .value();

    // The other note names the same uuid, with different data.
    Attachment impostor = photo;
    impostor.data = QByteArrayLiteral("NOT MINE");
    (void)save(other.id, {photo.uuid}, {impostor});
    QVERIFY(open(other.id).attachments.isEmpty());
    QCOMPARE(open(m_note.id).attachments.first().data, photo.data);

    // And its saves never delete the first note's.
    (void)save(other.id, {}, {});
    QCOMPARE(open(m_note.id).attachments.size(), 1);
}

void tst_Attachments::failedSaveChangesNoAttachments()
{
    const Attachment photo = image(QByteArrayLiteral("KEEP"));
    (void)save(m_note.id, {photo.uuid}, {photo});
    QVERIFY(await(m_notes->trash(m_note.id)));

    // Trashed notes can't be edited; the attachments stay as they were.
    const Attachment other = image(QByteArrayLiteral("NEW"));
    QVERIFY(!await(
        m_notes->updateBody(m_note.id, bodyWith({other.uuid}), QStringLiteral("Trip"), {other})));
    const OpenedNote opened = open(m_note.id);
    QCOMPARE(opened.attachments.size(), 1);
    QCOMPARE(opened.attachments.first().uuid, photo.uuid);
}

void tst_Attachments::duplicateGetsItsOwnCopies()
{
    const Attachment photo = image(QByteArrayLiteral("ORIGINAL"));
    (void)save(m_note.id, {photo.uuid}, {photo});
    const DbResult<Note> copy = await(m_notes->duplicate(m_note.id));
    QVERIFY(copy);

    const OpenedNote opened = open(copy.value().id);
    QCOMPARE(opened.attachments.size(), 1);
    const Attachment& copied = opened.attachments.first();
    QVERIFY(copied.uuid != photo.uuid);
    QCOMPARE(copied.data, photo.data);
    QCOMPARE(NoteText::attachmentUuids(opened.note.body), QStringList{copied.uuid});
    QCOMPARE(opened.note.thumbnailUuid, copied.uuid);
    QCOMPARE(opened.note.thumbnail, photo.thumbnail);

    // Deleting the image from the original leaves the copy's.
    (void)save(m_note.id, {}, {});
    QCOMPARE(open(copy.value().id).attachments.size(), 1);
}

void tst_Attachments::bodyNamesItsImages()
{
    const QString a = notes::models::newUuid();
    const QString b = notes::models::newUuid();
    QCOMPARE(NoteText::attachmentUuids(bodyWith({a, b, a})), (QStringList{a, b}));
    QVERIFY(NoteText::attachmentUuids(bodyWith({})).isEmpty());
    // Typed text that looks like a link isn't an image.
    QVERIFY(NoteText::attachmentUuids(
                QStringLiteral("{\"blocks\":[{\"runs\":[{\"text\":\"attachment://%1\"}]}]}").arg(a))
                .isEmpty());
}

QTEST_GUILESS_MAIN(tst_Attachments)
#include "tst_attachments.moc"
