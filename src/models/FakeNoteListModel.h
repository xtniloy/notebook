#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QString>
#include <QTimeZone>

#include <optional>
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

    // What the editor shows for a note. Stands in for NoteService::get(id)
    // (task 2.7): the list rows themselves never carry note bodies (TC-STO-014).
    struct FakeNote
    {
        // Title on the first line, the list preview on the second, then more
        // generated text. Empty for locked notes.
        QString text;
        qint64 modifiedMs = 0;
        bool locked = false;
    };

    // `count` notes edited over the three years before `nowMs`. The same
    // arguments always produce the same notes.
    FakeNoteListModel(int count, qint64 nowMs, const QTimeZone& zone = QTimeZone::LocalTime,
                      QObject* parent = nullptr);
    ~FakeNoteListModel() override;

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] Qt::ItemFlags flags(const QModelIndex& index) const override;

    [[nodiscard]] int noteCount() const { return m_noteCount; }
    [[nodiscard]] std::optional<FakeNote> note(qint64 noteId) const;

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
    QHash<qint64, int> m_rowById;
    int m_noteCount = 0;
};

} // namespace notes::models
