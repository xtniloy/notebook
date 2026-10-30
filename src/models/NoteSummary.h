#pragma once

#include <QString>

#include <optional>

namespace notes::models {

// A note as the note list shows it: everything but the body (Plan §10: "load
// only note titles/snippets for the list; full body on click"; TC-STO-014).
// Having no body fields makes it impossible for a list query to carry one.
struct NoteSummary
{
    qint64 id = 0;
    QString uuid;
    qint64 folderId = 0;
    QString title;
    QString snippet; // empty for locked notes
    bool pinned = false;
    bool locked = false;
    qint64 createdAt = 0;
    qint64 updatedAt = 0;
    std::optional<qint64> trashedAt;

    bool operator==(const NoteSummary&) const = default;
};

} // namespace notes::models
