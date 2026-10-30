#pragma once

#include <QObject>
#include <QTimer>

#include <chrono>

namespace notes::services {

class NoteService;

// Empties Recently Deleted of notes older than 30 days (task 2.10, Plan §3.3):
// once at start(), then every kInterval while the app runs.
class TrashPurger : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(TrashPurger)

public:
    static constexpr std::chrono::milliseconds kInterval = std::chrono::hours(24);

    explicit TrashPurger(NoteService& notes, QObject* parent = nullptr);
    ~TrashPurger() override;

    void start();
    // One purge now; start() and the timer call this.
    void purgeNow();

private:
    NoteService& m_notes;
    QTimer m_timer;
};

} // namespace notes::services
