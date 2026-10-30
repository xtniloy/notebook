#pragma once

#include <QString>
#include <QStringView>

namespace notes::services {

// How a note's title and list snippet come from its plain text (task 2.7). The
// title is not stored separately by the user: like macOS Notes, the first line
// is the name.
class NoteText
{
public:
    static constexpr qsizetype kMaxTitleLength = 200;
    static constexpr qsizetype kMaxSnippetLength = 120;

    // The first non-empty line, trimmed, at most kMaxTitleLength characters.
    // Empty for an empty note (the UI shows "New Note").
    [[nodiscard]] static QString title(QStringView plainText);

    // The non-empty lines after the title, trimmed and joined with spaces, at
    // most kMaxSnippetLength characters.
    [[nodiscard]] static QString snippet(QStringView plainText);
};

} // namespace notes::services
