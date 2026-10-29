#pragma once

#include <QAbstractListModel>
#include <QString>
#include <QTimeZone>

#include <vector>

namespace notes::models {

// Generated notes for the Phase 1 UI and the scrolling benchmark (TC-PERF-001),
// sorted newest first under date section headers, with pinned notes on top.
// Replaced by the database-backed NoteListModel in task 2.8.
class FakeNoteListModel : public QAbstractListModel
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(FakeNoteListModel)

public:
    static constexpr int kPinnedCount = 3;

    // `count` notes edited over the three years before `nowMs`. The same
    // arguments always produce the same notes.
    FakeNoteListModel(int count, qint64 nowMs, const QTimeZone& zone = QTimeZone::LocalTime,
                      QObject* parent = nullptr);
    ~FakeNoteListModel() override;

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] Qt::ItemFlags flags(const QModelIndex& index) const override;

    [[nodiscard]] int noteCount() const { return m_noteCount; }

private:
    struct Row
    {
        bool isHeader = false;
        QString title;
        QString preview;
        QString timeText;
        qint64 id = 0;
        qint64 modifiedMs = 0;
        bool pinned = false;
        bool locked = false;
        bool hasThumbnail = false;
    };

    std::vector<Row> m_rows;
    int m_noteCount = 0;
};

} // namespace notes::models
