#include "models/FakeNoteListModel.h"

#include "models/DateGrouping.h"
#include "models/NoteListRoles.h"

#include <QRandomGenerator>
#include <QStringList>

#include <algorithm>
#include <array>
#include <optional>

namespace notes::models {

namespace {

constexpr quint32 kSeed = 20261029;
constexpr qint64 kMaxAgeMs = 3LL * 365 * 24 * 60 * 60 * 1000;
// The first notes (some pinned) fall within the last hours, so "Today" has a few.
constexpr int kRecentCount = 6;
constexpr qint64 kRecentMaxAgeMs = 6LL * 60 * 60 * 1000;
constexpr int kLockedEvery = 13;
constexpr int kThumbnailEvery = 9;
constexpr int kMinPreviewWords = 5;
constexpr int kMaxPreviewWords = 14;
constexpr int kMinBodyLines = 2;
constexpr int kMaxBodyLines = 6;

// Sample content, not UI text, so not translated.
constexpr std::array kTitles{
    "Shopping List", "Meeting Notes", "Book Ideas",      "Travel Plans",    "Recipe: Dal",
    "Weekly Review", "Gift Ideas",    "Project Kickoff", "Reading List",    "Workout Log",
    "Bank Accounts", "Garden Plan",   "Packing List",    "Interview Prep",  "Quotes",
    "House Repairs", "Movie Night",   "Budget 2026",     "Conference Talk", "Birthday Party",
};
constexpr std::array kWords{
    "milk",   "eggs",    "bread",   "call",   "review", "draft",   "send",   "plan",
    "notes",  "ideas",   "follow",  "up",     "with",   "the",     "team",   "about",
    "budget", "trip",    "tickets", "hotel",  "book",   "chapter", "garden", "tomatoes",
    "paint",  "kitchen", "slides",  "agenda", "next",   "week",    "monday", "invoice",
};

template <std::size_t N>
QString pick(QRandomGenerator& rng, const std::array<const char*, N>& list)
{
    return QString::fromLatin1(list.at(rng.bounded(static_cast<quint32>(N))));
}

QString makePreview(QRandomGenerator& rng)
{
    const int words = rng.bounded(kMinPreviewWords, kMaxPreviewWords + 1);
    QStringList parts;
    parts.reserve(words);
    for (int i = 0; i < words; ++i) {
        parts.append(pick(rng, kWords));
    }
    QString preview = parts.join(QLatin1Char(' '));
    preview[0] = preview.at(0).toUpper();
    return preview;
}

} // namespace

FakeNoteListModel::FakeNoteListModel(int count, qint64 nowMs, const QTimeZone& zone,
                                     QObject* parent)
    : QAbstractListModel(parent)
    , m_noteCount(std::max(count, 0))
{
    QRandomGenerator rng(kSeed);
    std::vector<Row> notes(static_cast<std::size_t>(m_noteCount));
    for (int i = 0; i < m_noteCount; ++i) {
        Row& note = notes[static_cast<std::size_t>(i)];
        note.id = i + 1;
        const qint64 maxAge = i < kRecentCount ? kRecentMaxAgeMs : kMaxAgeMs;
        note.modifiedMs = nowMs - static_cast<qint64>(rng.bounded(static_cast<double>(maxAge)));
        note.title = pick(rng, kTitles);
        note.pinned = i < kPinnedCount;
        note.locked = i % kLockedEvery == kLockedEvery - 1;
        note.hasThumbnail = i % kThumbnailEvery == kThumbnailEvery - 1;
        const QString preview = makePreview(rng); // drawn for every note: same sequence
        if (!note.locked) {
            note.preview = preview;
        }
        note.timeText = DateGrouping::rowTimeText(note.modifiedMs, nowMs, zone);
    }

    std::ranges::sort(notes, [](const Row& a, const Row& b) {
        if (a.pinned != b.pinned) {
            return a.pinned;
        }
        return a.modifiedMs > b.modifiedMs;
    });

    m_rows.reserve(notes.size() + 64);
    std::optional<DateSection> current;
    for (Row& note : notes) {
        const DateSection section = note.pinned
                                        ? DateSection{.kind = DateSection::Kind::Pinned}
                                        : DateGrouping::sectionFor(note.modifiedMs, nowMs, zone);
        if (section != current) {
            current = section;
            Row header;
            header.isHeader = true;
            header.title = DateGrouping::title(section);
            m_rows.push_back(std::move(header));
        }
        m_rows.push_back(std::move(note));
    }

    m_rowById.reserve(m_noteCount);
    for (std::size_t row = 0; row < m_rows.size(); ++row) {
        if (!m_rows[row].isHeader) {
            m_rowById.insert(m_rows[row].id, static_cast<int>(row));
        }
    }
}

FakeNoteListModel::~FakeNoteListModel() = default;

std::optional<FakeNoteListModel::FakeNote> FakeNoteListModel::note(qint64 noteId) const
{
    const auto found = m_rowById.constFind(noteId);
    if (found == m_rowById.constEnd()) {
        return std::nullopt;
    }
    const Row& row = m_rows.at(static_cast<std::size_t>(*found));
    FakeNote note{.text = {}, .modifiedMs = row.modifiedMs, .locked = row.locked};
    if (row.locked) {
        return note;
    }

    // Generated on demand from the id, so 10,000 bodies never sit in memory.
    QRandomGenerator rng(kSeed ^ static_cast<quint32>(noteId));
    QStringList lines{row.title, row.preview};
    const int extra = rng.bounded(kMinBodyLines, kMaxBodyLines + 1);
    for (int i = 0; i < extra; ++i) {
        lines.append(makePreview(rng));
    }
    note.text = lines.join(QLatin1Char('\n'));
    return note;
}

int FakeNoteListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
}

QVariant FakeNoteListModel::data(const QModelIndex& index, int role) const
{
    // QAbstractItemModelTester passes invalid indexes on purpose; answer quietly.
    if (!index.isValid()) {
        return {};
    }
    Q_ASSERT(checkIndex(index));
    const Row& row = m_rows.at(static_cast<std::size_t>(index.row()));

    switch (role) {
    case Qt::DisplayRole:
        return row.title;
    case kIsSectionHeaderRole:
        return row.isHeader;
    default:
        break;
    }
    if (row.isHeader) {
        return {};
    }
    switch (role) {
    case kNoteIdRole:
        return row.id;
    case kPreviewRole:
        return row.preview;
    case kTimeTextRole:
        return row.timeText;
    case kModifiedRole:
        return row.modifiedMs;
    case kPinnedRole:
        return row.pinned;
    case kLockedRole:
        return row.locked;
    case kHasThumbnailRole:
        return row.hasThumbnail;
    default:
        return {};
    }
}

Qt::ItemFlags FakeNoteListModel::flags(const QModelIndex& index) const
{
    if (!index.isValid()) {
        return Qt::NoItemFlags;
    }
    Q_ASSERT(checkIndex(index));
    // Headers are disabled so keyboard navigation and clicks pass over them.
    return m_rows.at(static_cast<std::size_t>(index.row())).isHeader
               ? Qt::NoItemFlags
               : Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemNeverHasChildren;
}

} // namespace notes::models
