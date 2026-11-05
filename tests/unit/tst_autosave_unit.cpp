// Autosave (task 2.9) on its own: debounce, flush, hash comparison, and
// following a different note. Real DbThread and database; real timers.

#include "FakeClock.h"
#include "TestHelpers.h"
#include "services/Autosave.h"
#include "services/FolderService.h"
#include "services/NoteService.h"
#include "storage/DbThread.h"
#include "storage/Migrator.h"

#include <QSignalSpy>
#include <QTest>

#include <memory>

using notes::models::Note;
using notes::services::Autosave;
using notes::services::NoteService;
using notes::storage::Database;
using notes::test::await;

class tst_AutosaveUnit : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void savesAfterDelayOnce();
    void flushSavesAtOnce();
    void unchangedContentIsNotSaved();
    void trackingAnotherNoteFlushesFirst();
    void untrackedChangesAreIgnored();

private:
    [[nodiscard]] Autosave::Reader reader()
    {
        return [this] { return Autosave::Content{.body = {}, .text = m_text, .attachments = {}}; };
    }
    [[nodiscard]] QString textOf(qint64 id) { return await(m_notes->get(id)).value().bodyText; }

    std::unique_ptr<notes::test::TempDb> m_dir;
    notes::test::FakeClock m_clock;
    std::unique_ptr<notes::storage::DbThread> m_db;
    std::unique_ptr<NoteService> m_notes;
    std::unique_ptr<Autosave> m_autosave;
    Note m_note;
    QString m_text; // stands in for the editor
};

void tst_AutosaveUnit::init()
{
    m_dir = std::make_unique<notes::test::TempDb>();
    m_db = std::make_unique<notes::storage::DbThread>(m_dir->path());
    QVERIFY(await(m_db->post([](Database& db) { return notes::storage::Migrator().migrate(db); })));
    m_notes = std::make_unique<NoteService>(*m_db, m_clock);
    const qint64 folder =
        await(notes::services::FolderService(*m_db, m_clock).ensureDefaultFolder()).value().id;
    m_note = await(m_notes->create(folder, {}, QStringLiteral("Start"))).value();
    m_autosave = std::make_unique<Autosave>(*m_notes);
    m_text = m_note.bodyText;
    m_autosave->track(m_note.id, reader());
}

void tst_AutosaveUnit::cleanup()
{
    m_autosave = nullptr;
    m_notes = nullptr;
    m_db = nullptr;
    m_dir = nullptr;
}

void tst_AutosaveUnit::savesAfterDelayOnce()
{
    const QSignalSpy saves(m_notes.get(), &NoteService::noteUpdated);
    for (int i = 0; i < 5; ++i) {
        m_text += QLatin1Char('x');
        m_autosave->markChanged();
        QTest::qWait(50);
    }
    QCOMPARE(saves.count(), 0);
    QVERIFY(m_autosave->isPending());
    QTRY_COMPARE(saves.count(), 1);
    QVERIFY(!m_autosave->isPending());
    QCOMPARE(textOf(m_note.id), QStringLiteral("Startxxxxx"));
}

void tst_AutosaveUnit::flushSavesAtOnce()
{
    const QSignalSpy saves(m_notes.get(), &NoteService::noteUpdated);
    m_text = QStringLiteral("Now");
    m_autosave->markChanged();
    m_autosave->flush();
    QCOMPARE(textOf(m_note.id), QStringLiteral("Now")); // queued before this read
    QCOMPARE(saves.count(), 1);
    QTest::qWait(static_cast<int>(Autosave::kDelay.count()) + 100);
    QCOMPARE(saves.count(), 1); // the timer was stopped
}

void tst_AutosaveUnit::unchangedContentIsNotSaved()
{
    const QSignalSpy saves(m_notes.get(), &NoteService::noteUpdated);
    m_autosave->markChanged(); // e.g. a format change that undid itself
    m_autosave->flush();
    m_text = QStringLiteral("Changed");
    m_autosave->markChanged();
    m_autosave->flush();
    m_autosave->markChanged(); // nothing new since the last save
    m_autosave->flush();
    QTRY_COMPARE(saves.count(), 1);
    QTest::qWait(50);
    QCOMPARE(saves.count(), 1);
}

void tst_AutosaveUnit::trackingAnotherNoteFlushesFirst()
{
    const Note other = await(m_notes->create(m_note.folderId, {}, QStringLiteral("Other"))).value();
    m_text = QStringLiteral("Start, edited");
    m_autosave->markChanged();
    // The old note's pending edit is saved before the new one is followed.
    m_autosave->track(other.id, reader());
    QCOMPARE(textOf(m_note.id), QStringLiteral("Start, edited"));
    QCOMPARE(m_autosave->trackedNote(), other.id);
    QVERIFY(!m_autosave->isPending());
}

void tst_AutosaveUnit::untrackedChangesAreIgnored()
{
    const QSignalSpy saves(m_notes.get(), &NoteService::noteUpdated);
    m_autosave->untrack();
    m_text = QStringLiteral("Nobody's");
    m_autosave->markChanged();
    QVERIFY(!m_autosave->isPending());
    QTest::qWait(static_cast<int>(Autosave::kDelay.count()) + 100);
    QCOMPARE(saves.count(), 0);
}

QTEST_GUILESS_MAIN(tst_AutosaveUnit)
#include "tst_autosave_unit.moc"
