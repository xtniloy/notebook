#include "models/NoteListModel.h"

#include "app/IClock.h"
#include "models/DateGrouping.h"
#include "models/Note.h"
#include "models/NoteListRoles.h"
#include "services/NoteService.h"

#include <QLoggingCategory>

#include <algorithm>

namespace notes::models {

namespace {

Q_LOGGING_CATEGORY(lcNoteList, "notes.models.notelist")

using storage::DbResult;
using storage::NoteSort;

NoteSummary summaryOf(const Note& note)
{
    NoteSummary summary;
    summary.id = note.id;
    summary.uuid = note.uuid;
    summary.folderId = note.folderId;
    summary.title = note.title;
    summary.snippet = note.locked ? QString() : note.snippet;
    summary.pinned = note.pinned;
    summary.locked = note.locked;
    summary.createdAt = note.createdAt;
    summary.updatedAt = note.updatedAt;
    summary.trashedAt = note.trashedAt;
    return summary;
}

} // namespace

bool NoteListModel::Row::sameKey(const Row& other) const
{
    if (isHeader != other.isHeader) {
        return false;
    }
    return isHeader ? section == other.section : note.id == other.note.id;
}

NoteListModel::NoteListModel(services::NoteService& notes, const app::IClock& clock,
                             QObject* parent)
    : QAbstractListModel(parent)
    , m_service(notes)
    , m_clock(clock)
    , m_collator(QLocale())
{
    // Same rules as the database's LOCALE collation, so loaded pages and
    // locally inserted notes agree on the order.
    m_collator.setCaseSensitivity(Qt::CaseInsensitive);
    m_collator.setNumericMode(true);

    using services::NoteService;
    connect(&m_service, &NoteService::noteCreated, this,
            [this](const Note& note) { upsert(summaryOf(note)); });
    connect(&m_service, &NoteService::noteUpdated, this,
            [this](const Note& note) { upsert(summaryOf(note)); });
    // These only carry ids: reload the note to see where it belongs now.
    connect(&m_service, &NoteService::notePinnedChanged, this,
            [this](qint64 id) { refreshNote(id); });
    connect(&m_service, &NoteService::noteMoved, this, [this](qint64 id) { refreshNote(id); });
    connect(&m_service, &NoteService::noteTrashed, this, [this](qint64 id) { refreshNote(id); });
    connect(&m_service, &NoteService::noteRestored, this, [this](qint64 id) { refreshNote(id); });
    connect(&m_service, &NoteService::notePurged, this, [this](qint64 id) { remove(id); });

    setFolder(kAllNotesFolderId);
}

NoteListModel::~NoteListModel() = default;

void NoteListModel::setFolder(qint64 folderId, NoteSort sort)
{
    ++m_generation;
    beginResetModel();
    m_folderId = folderId;
    m_sort = sort;
    m_now = m_clock.nowMs();
    m_notes.clear();
    m_rows.clear();
    m_offset = 0;
    m_exhausted = false;
    m_loading = false;
    endResetModel();
    fetchPage();
}

void NoteListModel::refreshDates()
{
    m_now = m_clock.nowMs();
    applyNotes(m_notes);
}

bool NoteListModel::canFetchMore(const QModelIndex& parent) const
{
    return !parent.isValid() && !m_exhausted && !m_loading;
}

void NoteListModel::fetchMore(const QModelIndex& parent)
{
    if (canFetchMore(parent)) {
        fetchPage();
    }
}

void NoteListModel::fetchPage()
{
    m_loading = true;
    emit loadingChanged(true);
    const quint64 generation = m_generation;
    m_service.list(m_folderId, m_sort, kPageSize, m_offset)
        .then(this, [this, generation](const DbResult<QList<NoteSummary>>& page) {
            if (generation != m_generation) {
                return; // the folder changed meanwhile
            }
            m_loading = false;
            if (!page) {
                // Logged by category and code only: never note content.
                qCWarning(lcNoteList) << "Loading notes failed:" << page.error().code;
                m_exhausted = true;
                emit loadingChanged(false);
                return;
            }
            m_offset += static_cast<int>(page.value().size());
            m_exhausted = page.value().size() < kPageSize;
            QList<NoteSummary> notes = m_notes;
            for (const NoteSummary& note : page.value()) {
                if (indexOf(note.id) < 0) { // already added by a signal
                    notes.append(note);
                }
            }
            applyNotes(std::move(notes));
            emit loadingChanged(false);
        });
}

void NoteListModel::applyNotes(QList<NoteSummary> notes)
{
    m_notes = std::move(notes);
    const QList<Row> target = buildRows(m_notes);

    // Rows matching at both ends stay; the block between is replaced.
    qsizetype prefix = 0;
    while (prefix < m_rows.size() && prefix < target.size() &&
           m_rows.at(prefix).sameKey(target.at(prefix))) {
        ++prefix;
    }
    qsizetype suffix = 0;
    while (suffix < m_rows.size() - prefix && suffix < target.size() - prefix &&
           m_rows.at(m_rows.size() - 1 - suffix).sameKey(target.at(target.size() - 1 - suffix))) {
        ++suffix;
    }
    const qsizetype removeCount = m_rows.size() - prefix - suffix;
    const qsizetype insertCount = target.size() - prefix - suffix;
    if (removeCount > 0) {
        beginRemoveRows({}, static_cast<int>(prefix), static_cast<int>(prefix + removeCount - 1));
        m_rows.remove(prefix, removeCount);
        endRemoveRows();
    }
    if (insertCount > 0) {
        beginInsertRows({}, static_cast<int>(prefix), static_cast<int>(prefix + insertCount - 1));
        for (qsizetype i = 0; i < insertCount; ++i) {
            m_rows.insert(prefix + i, target.at(prefix + i));
        }
        endInsertRows();
    }
    // Same rows, maybe new content (an edited title, a new time).
    for (qsizetype i = 0; i < m_rows.size(); ++i) {
        if (!(m_rows.at(i) == target.at(i))) {
            m_rows[i] = target.at(i);
            const QModelIndex changed = index(static_cast<int>(i));
            emit dataChanged(changed, changed);
        }
    }
}

QList<NoteListModel::Row> NoteListModel::buildRows(const QList<NoteSummary>& notes) const
{
    QList<Row> rows;
    rows.reserve(notes.size() + 16);
    QString current;
    bool first = true;
    for (const NoteSummary& note : notes) {
        const QString section = sectionOf(note);
        if (first || section != current) {
            rows.append(Row{.isHeader = true, .section = section, .timeText = {}, .note = {}});
            current = section;
            first = false;
        }
        rows.append(Row{.isHeader = false,
                        .section = section,
                        .timeText = DateGrouping::rowTimeText(timestampOf(note), m_now),
                        .note = note});
    }
    return rows;
}

bool NoteListModel::belongsHere(const NoteSummary& note) const
{
    if (m_folderId == kRecentlyDeletedFolderId) {
        return note.trashedAt.has_value();
    }
    if (note.trashedAt.has_value()) {
        return false;
    }
    return m_folderId == kAllNotesFolderId || note.folderId == m_folderId;
}

// The order NoteRepository's queries return (NoteRepository::list*()).
bool NoteListModel::before(const NoteSummary& a, const NoteSummary& b) const
{
    if (m_folderId == kRecentlyDeletedFolderId) {
        const qint64 ta = a.trashedAt.value_or(0);
        const qint64 tb = b.trashedAt.value_or(0);
        return ta != tb ? ta > tb : a.id > b.id;
    }
    if (a.pinned != b.pinned) {
        return a.pinned;
    }
    switch (m_sort) {
    case NoteSort::DateEdited:
        return a.updatedAt != b.updatedAt ? a.updatedAt > b.updatedAt : a.id > b.id;
    case NoteSort::DateCreated:
        return a.createdAt != b.createdAt ? a.createdAt > b.createdAt : a.id > b.id;
    case NoteSort::Title: {
        const int order = m_collator.compare(a.title, b.title);
        return order != 0 ? order < 0 : a.id < b.id;
    }
    }
    return a.id > b.id;
}

qsizetype NoteListModel::indexOf(qint64 noteId) const
{
    const auto found = std::ranges::find_if(
        m_notes, [noteId](const NoteSummary& note) { return note.id == noteId; });
    return found == m_notes.end() ? -1 : std::distance(m_notes.begin(), found);
}

void NoteListModel::upsert(const NoteSummary& note)
{
    QList<NoteSummary> notes = m_notes;
    if (const qsizetype old = indexOf(note.id); old >= 0) {
        notes.remove(old);
        --m_offset; // it came from the query's loaded part
    }
    if (belongsHere(note)) {
        const auto position = std::ranges::find_if(
            notes, [&](const NoteSummary& other) { return before(note, other); });
        // Past the loaded part (more pages to come): the next page brings it.
        if (position != notes.end() || m_exhausted) {
            notes.insert(position, note);
            ++m_offset;
        }
    }
    applyNotes(std::move(notes));
}

void NoteListModel::remove(qint64 noteId)
{
    const qsizetype found = indexOf(noteId);
    if (found < 0) {
        return;
    }
    QList<NoteSummary> notes = m_notes;
    notes.remove(found);
    --m_offset;
    applyNotes(std::move(notes));
}

void NoteListModel::refreshNote(qint64 noteId)
{
    const quint64 generation = m_generation;
    m_service.get(noteId).then(this, [this, generation, noteId](const DbResult<Note>& note) {
        if (generation != m_generation) {
            return;
        }
        if (note && !note.value().deletedAt) {
            upsert(summaryOf(note.value()));
        } else {
            remove(noteId);
        }
    });
}

QString NoteListModel::sectionOf(const NoteSummary& note) const
{
    if (m_folderId == kRecentlyDeletedFolderId) {
        return DateGrouping::title(DateGrouping::sectionFor(timestampOf(note), m_now));
    }
    if (note.pinned) {
        return DateGrouping::title({.kind = DateSection::Kind::Pinned});
    }
    if (m_sort == NoteSort::Title) {
        return tr("Notes");
    }
    return DateGrouping::title(DateGrouping::sectionFor(timestampOf(note), m_now));
}

qint64 NoteListModel::timestampOf(const NoteSummary& note) const
{
    if (m_folderId == kRecentlyDeletedFolderId) {
        return note.trashedAt.value_or(note.updatedAt);
    }
    return m_sort == NoteSort::DateCreated ? note.createdAt : note.updatedAt;
}

int NoteListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
}

QVariant NoteListModel::data(const QModelIndex& index, int role) const
{
    // QAbstractItemModelTester passes invalid indexes on purpose; answer quietly.
    if (!index.isValid()) {
        return {};
    }
    Q_ASSERT(checkIndex(index));
    const Row& row = m_rows.at(index.row());
    if (role == kIsSectionHeaderRole) {
        return row.isHeader;
    }
    if (row.isHeader) {
        return role == Qt::DisplayRole ? QVariant(row.section) : QVariant();
    }
    const NoteSummary& note = row.note;
    switch (role) {
    case Qt::DisplayRole:
        return note.title.isEmpty() ? tr("New Note") : note.title; // TC-STO-019
    case kNoteIdRole:
        return note.id;
    case kPreviewRole:
        return note.locked ? QString() : note.snippet;
    case kTimeTextRole:
        return row.timeText;
    case kModifiedRole:
        return note.updatedAt;
    case kPinnedRole:
        return note.pinned;
    case kLockedRole:
        return note.locked;
    case kHasThumbnailRole:
        return false; // attachments arrive in Phase 3
    case kSectionRole:
        return row.section;
    default:
        return {};
    }
}

Qt::ItemFlags NoteListModel::flags(const QModelIndex& index) const
{
    if (!index.isValid()) {
        return Qt::NoItemFlags;
    }
    Q_ASSERT(checkIndex(index));
    // Headers are disabled so clicks and arrow keys pass over them (as before).
    return m_rows.at(index.row()).isHeader
               ? Qt::NoItemFlags
               : Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemNeverHasChildren;
}

} // namespace notes::models
