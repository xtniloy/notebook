// NoteRepository (task 2.6): TC-STO-013..018, plus date sorting, trash /
// restore / purge, paging, and which changes touch updated_at.

#include "FakeClock.h"
#include "TestHelpers.h"
#include "models/Uuid.h"
#include "storage/Database.h"
#include "storage/FolderRepository.h"
#include "storage/Migrator.h"
#include "storage/NoteRepository.h"

#include <QLocale>
#include <QTest>

#include <chrono>
#include <memory>

using namespace std::chrono_literals;
using notes::models::Note;
using notes::models::NoteSummary;
using notes::models::SyncState;
using notes::storage::Database;
using notes::storage::DbError;
using notes::storage::DbResult;
using notes::storage::DbStatus;
using notes::storage::FolderRepository;
using notes::storage::NoteContent;
using notes::storage::NoteRepository;
using notes::storage::NoteSort;
using Kind = DbError::Kind;

Q_DECLARE_METATYPE(NoteSort)

namespace {

NoteContent content(const QString& title, const QString& body = {})
{
    return {.title = title,
            .snippet = title + QStringLiteral(" snippet"),
            .bodyHtml = QStringLiteral("<p>%1</p>").arg(body.isEmpty() ? title : body),
            .bodyText = body.isEmpty() ? title : body};
}

QStringList titles(const DbResult<QList<NoteSummary>>& notes)
{
    if (!notes) {
        return {QStringLiteral("ERROR: ") + notes.error().message};
    }
    QStringList result;
    for (const NoteSummary& note : notes.value()) {
        result.append(note.title);
    }
    return result;
}

} // namespace

class tst_NoteRepository : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();

    void createThenGetIsIdentical();
    void listsNeverLoadBodies();
    void pinnedFirstInEverySort_data();
    void pinnedFirstInEverySort();
    void titleSortIsLocaleAware();
    void dateSorts();
    void updateBodyBumpsVersionAndTime();
    void oneMegabyteBodyRoundTrips();
    void metadataChangesKeepEditTime();
    void moveToFolder();
    void trashAndRestore();
    void restoreFallsBackToDefaultFolder();
    void purgeLeavesContentFreeTombstone();
    void stateRules();
    void paging();

private:
    Note create(const QString& title, std::optional<qint64> folder = {});
    void markSynced();

    std::unique_ptr<notes::test::TempDb> m_dir;
    std::unique_ptr<Database> m_db;
    notes::test::FakeClock m_clock;
    std::unique_ptr<FolderRepository> m_folders;
    std::unique_ptr<NoteRepository> m_notes;
    qint64 m_work = 0;
};

void tst_NoteRepository::initTestCase()
{
    // The LOCALE collation follows the default locale; pin it for stable results.
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
}

void tst_NoteRepository::init()
{
    m_dir = std::make_unique<notes::test::TempDb>();
    DbResult<std::unique_ptr<Database>> db = Database::open(m_dir->path());
    QVERIFY(db);
    m_db = std::move(db).value();
    QVERIFY(notes::storage::Migrator().migrate(*m_db));
    m_clock.setNowMs(notes::test::FakeClock::kDefaultStartMs);
    m_folders = std::make_unique<FolderRepository>(*m_db, m_clock);
    m_notes = std::make_unique<NoteRepository>(*m_db, m_clock);
    QVERIFY(m_folders->ensureDefaultFolder());
    m_work = m_folders->create(QStringLiteral("Work")).value().id;
}

void tst_NoteRepository::cleanup()
{
    m_notes = nullptr;
    m_folders = nullptr;
    m_db = nullptr;
    m_dir = nullptr;
}

Note tst_NoteRepository::create(const QString& title, std::optional<qint64> folder)
{
    DbResult<Note> note = m_notes->create(folder.value_or(m_work), content(title));
    if (!note) {
        qWarning() << "create failed:" << note.error().message;
        return {};
    }
    return note.value();
}

void tst_NoteRepository::markSynced()
{
    QVERIFY(m_db->exec(QStringLiteral("UPDATE notes SET sync_state = 1")));
}

// TC-STO-013
void tst_NoteRepository::createThenGetIsIdentical()
{
    const NoteContent body{.title = QStringLiteral("Shopping List"),
                           .snippet = QStringLiteral("Milk, eggs"),
                           .bodyHtml = QStringLiteral("<h1>Shopping List</h1><p>Milk, eggs 🥚 ") +
                                       QString::fromLatin1(notes::test::kSecretMarker) +
                                       QStringLiteral("</p>"),
                           .bodyText = QStringLiteral("Shopping List\nMilk, eggs 🥚 ") +
                                       QString::fromLatin1(notes::test::kSecretMarker)};
    const DbResult<Note> created = m_notes->create(m_work, body);
    QVERIFY2(created, qPrintable(created.error().message));
    const DbResult<Note> loaded = m_notes->get(created.value().id);
    QVERIFY(loaded);
    QCOMPARE(loaded.value(), created.value());

    const Note& note = loaded.value();
    QVERIFY(note.id > 0);
    QVERIFY(notes::models::parseUuid(note.uuid).has_value());
    QCOMPARE(note.folderId, m_work);
    QCOMPARE(note.title, body.title);
    QCOMPARE(note.snippet, body.snippet);
    QCOMPARE(note.bodyHtml, body.bodyHtml);
    QCOMPARE(note.bodyText, body.bodyText);
    QCOMPARE(note.createdAt, m_clock.nowMs());
    QCOMPARE(note.updatedAt, m_clock.nowMs());
    QCOMPARE(note.version, 1);
    QCOMPARE(note.syncState, SyncState::Dirty);
    QVERIFY(!note.pinned && !note.locked && !note.isTrashed() && !note.deletedAt);
}

// TC-STO-014
void tst_NoteRepository::listsNeverLoadBodies()
{
    create(QStringLiteral("One"));
    const Note two = create(QStringLiteral("Two"));
    QVERIFY(m_notes->trash(two.id));

    QStringList executed;
    m_db->setTraceHook([&executed](const QString& sql) { executed.append(sql); });
    const DbResult<QList<NoteSummary>> inFolder = m_notes->listForFolder(m_work, NoteSort::Title);
    const DbResult<QList<NoteSummary>> all = m_notes->listAll(NoteSort::DateEdited);
    const DbResult<QList<NoteSummary>> trashed = m_notes->listTrashed();
    const QStringList listSql = executed;
    executed.clear();
    QVERIFY(m_notes->get(two.id)); // sanity: the hook does see body reads
    const QStringList getSql = executed;
    m_db->setTraceHook({});

    QCOMPARE(titles(inFolder), QStringList{QStringLiteral("One")});
    QCOMPARE(inFolder.value().first().snippet, QStringLiteral("One snippet"));
    QCOMPARE(titles(all), QStringList{QStringLiteral("One")});
    QCOMPARE(titles(trashed), QStringList{QStringLiteral("Two")});

    QCOMPARE(listSql.size(), 3);
    for (const QString& sql : listSql) {
        QVERIFY2(!sql.contains(QStringLiteral("body_html")), qPrintable(sql));
        QVERIFY2(!sql.contains(QStringLiteral("body_text")), qPrintable(sql));
        QVERIFY2(!sql.contains(QLatin1Char('*')), qPrintable(sql));
    }
    QVERIFY(getSql.join(QLatin1Char(' ')).contains(QStringLiteral("body_html")));
}

// TC-STO-015
void tst_NoteRepository::pinnedFirstInEverySort_data()
{
    QTest::addColumn<NoteSort>("sort");
    QTest::newRow("date edited") << NoteSort::DateEdited;
    QTest::newRow("date created") << NoteSort::DateCreated;
    QTest::newRow("title") << NoteSort::Title;
}

void tst_NoteRepository::pinnedFirstInEverySort()
{
    QFETCH(NoteSort, sort);
    create(QStringLiteral("Alpha"));
    m_clock.advance(1min);
    const Note zulu = create(QStringLiteral("Zulu")); // last by title, oldest-but-one
    m_clock.advance(1min);
    create(QStringLiteral("Mike"));
    QVERIFY(m_notes->setPinned(zulu.id, true));

    const QStringList inFolder = titles(m_notes->listForFolder(m_work, sort));
    QCOMPARE(inFolder.size(), 3);
    QCOMPARE(inFolder.first(), QStringLiteral("Zulu"));
    QCOMPARE(titles(m_notes->listAll(sort)).first(), QStringLiteral("Zulu"));
}

// TC-STO-016
void tst_NoteRepository::titleSortIsLocaleAware()
{
    for (const char* title : {"zebra", "Élan", "Note 10", "Banana", "apple", "Note 2", "éclair"}) {
        create(QString::fromUtf8(title));
    }
    QCOMPARE(
        titles(m_notes->listForFolder(m_work, NoteSort::Title)),
        (QStringList{QStringLiteral("apple"), QStringLiteral("Banana"), QStringLiteral("éclair"),
                     QStringLiteral("Élan"), QStringLiteral("Note 2"), QStringLiteral("Note 10"),
                     QStringLiteral("zebra")}));
}

void tst_NoteRepository::dateSorts()
{
    const Note first = create(QStringLiteral("First"));
    m_clock.advance(1h);
    create(QStringLiteral("Second"));
    m_clock.advance(1h);
    create(QStringLiteral("Third"));
    m_clock.advance(1h);
    QVERIFY(
        m_notes->updateBody(first.id, content(QStringLiteral("First"), QStringLiteral("edited"))));

    QCOMPARE(
        titles(m_notes->listForFolder(m_work, NoteSort::DateEdited)),
        (QStringList{QStringLiteral("First"), QStringLiteral("Third"), QStringLiteral("Second")}));
    QCOMPARE(
        titles(m_notes->listForFolder(m_work, NoteSort::DateCreated)),
        (QStringList{QStringLiteral("Third"), QStringLiteral("Second"), QStringLiteral("First")}));
}

// TC-STO-017
void tst_NoteRepository::updateBodyBumpsVersionAndTime()
{
    const Note note = create(QStringLiteral("Draft"));
    markSynced();
    m_clock.advance(10min);

    const DbResult<Note> updated =
        m_notes->updateBody(note.id, content(QStringLiteral("Final"), QStringLiteral("All done")));
    QVERIFY2(updated, qPrintable(updated.error().message));
    QCOMPARE(updated.value().version, note.version + 1);
    QCOMPARE(updated.value().updatedAt, m_clock.nowMs());
    QCOMPARE(updated.value().createdAt, note.createdAt);
    QCOMPARE(updated.value().syncState, SyncState::Dirty);
    QCOMPARE(updated.value().title, QStringLiteral("Final"));
    QCOMPARE(updated.value().bodyText, QStringLiteral("All done"));
    QCOMPARE(updated.value().uuid, note.uuid);
}

// TC-STO-018
void tst_NoteRepository::oneMegabyteBodyRoundTrips()
{
    QString text;
    text.reserve(1 << 20);
    const QString chunk = QStringLiteral("Line with ünïcode, 日本語 and emoji 🗒️ ") +
                          QString::fromLatin1(notes::test::kSecretMarker) + QLatin1Char('\n');
    while (text.toUtf8().size() < (1 << 20)) {
        text += chunk.repeated(512);
    }
    QVERIFY(text.toUtf8().size() >= 1 << 20);
    const NoteContent big{.title = QStringLiteral("Big"),
                          .snippet = QString(),
                          .bodyHtml = QStringLiteral("<pre>") + text + QStringLiteral("</pre>"),
                          .bodyText = text};

    const DbResult<Note> created = m_notes->create(m_work, big);
    QVERIFY(created);
    const Note loaded = m_notes->get(created.value().id).value();
    QCOMPARE(loaded.bodyText, big.bodyText);
    QCOMPARE(loaded.bodyHtml, big.bodyHtml);
}

// Only content changes move a note in "date edited" order; every change is synced.
void tst_NoteRepository::metadataChangesKeepEditTime()
{
    const Note note = create(QStringLiteral("Note"));
    const qint64 other = m_folders->create(QStringLiteral("Other")).value().id;
    int version = note.version;

    const auto check = [&](const char* step) {
        const Note now = m_notes->get(note.id).value();
        QVERIFY2(now.version == ++version, step);
        QVERIFY2(now.syncState == SyncState::Dirty, step);
        QVERIFY2(now.updatedAt == note.updatedAt, step);
        QVERIFY(m_db->exec(QStringLiteral("UPDATE notes SET sync_state = 1")));
    };
    m_clock.advance(1h);
    markSynced();
    QVERIFY(m_notes->setPinned(note.id, true));
    check("pin");
    QVERIFY(m_notes->setPinned(note.id, true)); // unchanged: no bump
    QCOMPARE(m_notes->get(note.id).value().version, version);
    QVERIFY(m_notes->moveToFolder(note.id, other));
    check("move");
    QVERIFY(m_notes->trash(note.id));
    check("trash");
    QVERIFY(m_notes->restore(note.id));
    check("restore");
}

void tst_NoteRepository::moveToFolder()
{
    const Note note = create(QStringLiteral("Note"));
    const qint64 other = m_folders->create(QStringLiteral("Other")).value().id;
    const qint64 gone = m_folders->create(QStringLiteral("Gone")).value().id;
    QVERIFY(m_folders->softDelete(gone));

    QVERIFY(m_notes->moveToFolder(note.id, other));
    QCOMPARE(titles(m_notes->listForFolder(other, NoteSort::Title)),
             QStringList{QStringLiteral("Note")});
    QVERIFY(titles(m_notes->listForFolder(m_work, NoteSort::Title)).isEmpty());

    QCOMPARE(m_notes->moveToFolder(note.id, gone).error().kind, Kind::NotFound);
    QCOMPARE(m_notes->moveToFolder(note.id, 999).error().kind, Kind::NotFound);
    QCOMPARE(m_notes->get(note.id).value().folderId, other);
}

void tst_NoteRepository::trashAndRestore()
{
    const Note note = create(QStringLiteral("Note"));
    create(QStringLiteral("Other"));
    m_clock.advance(1h);

    QVERIFY(m_notes->trash(note.id));
    const Note trashed = m_notes->get(note.id).value();
    QCOMPARE(trashed.trashedAt, std::optional<qint64>(m_clock.nowMs()));
    QCOMPARE(trashed.folderId, m_work); // kept for restore
    QCOMPARE(titles(m_notes->listForFolder(m_work, NoteSort::Title)),
             QStringList{QStringLiteral("Other")});
    QCOMPARE(titles(m_notes->listAll(NoteSort::Title)), QStringList{QStringLiteral("Other")});
    QCOMPARE(titles(m_notes->listTrashed()), QStringList{QStringLiteral("Note")});

    QVERIFY(m_notes->restore(note.id));
    QVERIFY(!m_notes->get(note.id).value().isTrashed());
    QCOMPARE(titles(m_notes->listForFolder(m_work, NoteSort::Title)),
             (QStringList{QStringLiteral("Note"), QStringLiteral("Other")}));
    QVERIFY(titles(m_notes->listTrashed()).isEmpty());
}

void tst_NoteRepository::restoreFallsBackToDefaultFolder()
{
    const Note note = create(QStringLiteral("Note"));
    QVERIFY(m_notes->trash(note.id));
    QVERIFY(m_folders->softDelete(m_work));

    QVERIFY(m_notes->restore(note.id));
    const qint64 defaultFolder = m_folders->ensureDefaultFolder().value().id;
    QCOMPARE(m_notes->get(note.id).value().folderId, defaultFolder);
    QCOMPARE(titles(m_notes->listForFolder(defaultFolder, NoteSort::Title)),
             QStringList{QStringLiteral("Note")});
}

void tst_NoteRepository::purgeLeavesContentFreeTombstone()
{
    const Note note = create(QStringLiteral("Secret"));
    QVERIFY(m_db->exec(
        QStringLiteral("INSERT INTO attachments (uuid, note_id, file_name, data, created_at)"
                       "  VALUES ('a1', %1, 'photo.png', x'89504e47', 1);"
                       "INSERT INTO tags (id, name) VALUES (1, 'work');"
                       "INSERT INTO note_tags (note_id, tag_id) VALUES (%1, 1);")
            .arg(note.id)));
    QVERIFY(m_notes->trash(note.id));
    m_clock.advance(1h);

    QVERIFY(m_notes->purge(note.id));
    const Note tombstone = m_notes->get(note.id).value();
    QCOMPARE(tombstone.deletedAt, std::optional<qint64>(m_clock.nowMs()));
    QCOMPARE(tombstone.uuid, note.uuid); // identity stays for sync
    QCOMPARE(tombstone.syncState, SyncState::Dirty);
    QVERIFY(tombstone.title.isEmpty());
    QVERIFY(tombstone.snippet.isNull());
    QVERIFY(tombstone.bodyHtml.isNull());
    QVERIFY(tombstone.bodyText.isNull());

    const auto count = [&](const char* table) {
        DbResult<notes::storage::ScopedStatement> select =
            m_db->prepare(QStringLiteral("SELECT count(*) FROM %1 WHERE note_id = ?")
                              .arg(QLatin1StringView(table)));
        return select && select.value()->bind(1, note.id).step().valueOr(false)
                   ? select.value()->column<int>(0)
                   : -1;
    };
    QCOMPARE(count("attachments"), 0);
    QCOMPARE(count("note_tags"), 0);
    QVERIFY(titles(m_notes->listTrashed()).isEmpty());
    QVERIFY(titles(m_notes->listAll(NoteSort::Title)).isEmpty());

    // A tombstone takes no further changes.
    QCOMPARE(m_notes->purge(note.id).error().kind, Kind::NotFound);
    QCOMPARE(m_notes->restore(note.id).error().kind, Kind::NotFound);
    QCOMPARE(m_notes->updateBody(note.id, content(QStringLiteral("x"))).error().kind,
             Kind::NotFound);
}

void tst_NoteRepository::stateRules()
{
    const Note live = create(QStringLiteral("Live"));
    const Note trashed = create(QStringLiteral("Trashed"));
    QVERIFY(m_notes->trash(trashed.id));

    QCOMPARE(m_notes->restore(live.id).error().kind, Kind::InvalidArgument);
    QCOMPARE(m_notes->trash(trashed.id).error().kind, Kind::InvalidArgument);
    QCOMPARE(m_notes->updateBody(trashed.id, content(QStringLiteral("x"))).error().kind,
             Kind::InvalidArgument);
    QCOMPARE(m_notes->setPinned(trashed.id, true).error().kind, Kind::InvalidArgument);
    QCOMPARE(m_notes->get(999).error().kind, Kind::NotFound);

    const qint64 gone = m_folders->create(QStringLiteral("Gone")).value().id;
    QVERIFY(m_folders->softDelete(gone));
    QCOMPARE(m_notes->create(gone, content(QStringLiteral("x"))).error().kind, Kind::NotFound);

    // Locked notes are never edited in plain text (Phase 6 re-encrypts them).
    QVERIFY(
        m_db->exec(QStringLiteral("UPDATE notes SET is_locked = 1 WHERE id = %1").arg(live.id)));
    QCOMPARE(m_notes->updateBody(live.id, content(QStringLiteral("x"))).error().kind,
             Kind::Protected);
    QCOMPARE(m_notes->get(live.id).value().title, QStringLiteral("Live"));
}

void tst_NoteRepository::paging()
{
    for (const char* title : {"A", "B", "C", "D", "E"}) {
        create(QString::fromLatin1(title));
    }
    QCOMPARE(titles(m_notes->listForFolder(m_work, NoteSort::Title, 2, 0)),
             (QStringList{QStringLiteral("A"), QStringLiteral("B")}));
    QCOMPARE(titles(m_notes->listForFolder(m_work, NoteSort::Title, 2, 2)),
             (QStringList{QStringLiteral("C"), QStringLiteral("D")}));
    QCOMPARE(titles(m_notes->listForFolder(m_work, NoteSort::Title, 2, 4)),
             QStringList{QStringLiteral("E")});
    QVERIFY(titles(m_notes->listForFolder(m_work, NoteSort::Title, 2, 6)).isEmpty());
    QCOMPARE(titles(m_notes->listAll(NoteSort::Title, -1, 3)),
             (QStringList{QStringLiteral("D"), QStringLiteral("E")}));
}

QTEST_GUILESS_MAIN(tst_NoteRepository)
#include "tst_noterepository.moc"
