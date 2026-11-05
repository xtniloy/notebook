#pragma once

#include <QString>
#include <QStringList>
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

    // The attachments a note body (editor::NoteFormat's JSON) shows as images,
    // as uuids in body order, each once (task 3.7). Read from the JSON text,
    // so the core needs no editor: an image run is
    // `"image":"attachment://<uuid>"` there.
    [[nodiscard]] static QStringList attachmentUuids(const QString& body);
};

} // namespace notes::services
