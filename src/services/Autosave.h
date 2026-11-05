#pragma once

#include "models/Attachment.h"

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QString>
#include <QTimer>

#include <chrono>
#include <functional>

namespace notes::services {

class NoteService;

// Saves the note being edited without a Save button (task 2.9): kDelay after
// the last change, and at once on flush() (switching notes, closing the
// window, quitting). Saves go through NoteService::updateBody(), which runs on
// the DB thread; nothing here waits for them. The DB thread finishes queued
// jobs before it stops, so a save flushed while quitting still lands.
//
// Content is read only when a save is due, and compared by hash with what was
// last loaded or saved, so selecting a note, or typing and undoing, saves
// nothing (TC-STO-028).
class Autosave : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(Autosave)

public:
    static constexpr std::chrono::milliseconds kDelay{500};

    struct Content
    {
        QString body; // in the current note format (editor::NoteFormat)
        QString text;
        // The editor's copies of the images in `body`, so new ones get stored
        // (NoteService::updateBody()). Not part of the comparison: the body
        // names every image by its uuid.
        QList<models::Attachment> attachments;
    };
    using Reader = std::function<Content()>;

    explicit Autosave(NoteService& notes, QObject* parent = nullptr);
    ~Autosave() override;

    // Flushes the previous note, then follows `noteId`, whose content (as
    // `read` returns it now) counts as saved.
    void track(qint64 noteId, Reader read);
    // Flushes, then follows nothing (no note shown, or a locked one).
    void untrack();
    [[nodiscard]] qint64 trackedNote() const { return m_noteId; }

    // The content changed: saves kDelay after the last call.
    void markChanged();
    // Saves now if a save is pending and the content differs.
    void flush();
    [[nodiscard]] bool isPending() const { return m_changed; }

private:
    [[nodiscard]] static QByteArray hashOf(const Content& content);

    NoteService& m_notes;
    QTimer m_timer;
    qint64 m_noteId = 0; // 0: nothing tracked
    Reader m_read;
    QByteArray m_savedHash;
    // Not m_timer.isActive(): a single-shot timer is already inactive when its
    // timeout() runs the save.
    bool m_changed = false;
};

} // namespace notes::services
