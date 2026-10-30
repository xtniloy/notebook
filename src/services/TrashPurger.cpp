#include "services/TrashPurger.h"

#include "services/NoteService.h"

#include <QLoggingCategory>

namespace notes::services {

namespace {

Q_LOGGING_CATEGORY(lcTrash, "notes.services.trash")

} // namespace

TrashPurger::TrashPurger(NoteService& notes, QObject* parent)
    : QObject(parent)
    , m_notes(notes)
{
    m_timer.setInterval(kInterval);
    connect(&m_timer, &QTimer::timeout, this, &TrashPurger::purgeNow);
}

TrashPurger::~TrashPurger() = default;

void TrashPurger::start()
{
    purgeNow();
    m_timer.start();
}

void TrashPurger::purgeNow()
{
    m_notes.purgeExpired().then(this, [](const storage::DbResult<QList<qint64>>& purged) {
        if (!purged) {
            qCWarning(lcTrash) << "Emptying Recently Deleted failed:" << purged.error().code;
        } else if (!purged.value().isEmpty()) {
            qCInfo(lcTrash) << "Purged" << purged.value().size() << "notes older than 30 days";
        }
    });
}

} // namespace notes::services
