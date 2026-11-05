#include "services/Autosave.h"

#include "services/NoteService.h"

#include <QCryptographicHash>
#include <QLoggingCategory>

#include <utility>

namespace notes::services {

namespace {

Q_LOGGING_CATEGORY(lcAutosave, "notes.services.autosave")

} // namespace

Autosave::Autosave(NoteService& notes, QObject* parent)
    : QObject(parent)
    , m_notes(notes)
{
    m_timer.setSingleShot(true);
    m_timer.setInterval(kDelay);
    connect(&m_timer, &QTimer::timeout, this, &Autosave::flush);
}

Autosave::~Autosave()
{
    flush();
}

void Autosave::track(qint64 noteId, Reader read)
{
    flush();
    m_noteId = noteId;
    m_read = std::move(read);
    m_savedHash = hashOf(m_read());
}

void Autosave::untrack()
{
    flush();
    m_noteId = 0;
    m_read = nullptr;
    m_savedHash.clear();
}

void Autosave::markChanged()
{
    if (m_noteId != 0) {
        m_changed = true;
        m_timer.start(); // restarts: kDelay after the last change
    }
}

void Autosave::flush()
{
    m_timer.stop();
    if (!m_changed || m_noteId == 0) {
        return;
    }
    m_changed = false;
    const Content content = m_read();
    QByteArray hash = hashOf(content);
    if (hash == m_savedHash) {
        return; // e.g. typed, then undid it
    }
    m_savedHash = std::move(hash);
    m_notes.updateBody(m_noteId, content.body, content.text, content.attachments)
        .then(this, [](const storage::DbResult<models::Note>& result) {
            if (!result) {
                // Category and code only: never note content.
                qCWarning(lcAutosave) << "Saving a note failed:" << result.error().code;
            }
        });
}

QByteArray Autosave::hashOf(const Content& content)
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(content.body.toUtf8());
    hash.addData(QByteArrayView("\0", 1)); // so "ab"+"c" differs from "a"+"bc"
    hash.addData(content.text.toUtf8());
    return hash.result();
}

} // namespace notes::services
