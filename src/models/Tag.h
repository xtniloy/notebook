#pragma once

#include <QString>

namespace notes::models {

// A row of the `tags` table (Plan §7). Tags have no UUID: they are identified by
// name and travel inside each note's sync record (ADR 0005).
struct Tag
{
    qint64 id = 0; // 0 until stored
    QString name;  // without the leading '#'

    bool operator==(const Tag&) const = default;
};

} // namespace notes::models
