#pragma once

#include "models/Note.h"

#include <QString>
#include <QTextFormat>

#include <optional>

class QTextDocument;

namespace notes::editor {

// How a note body is stored (task 3.1, ADR 0006): our own versioned JSON
// describing what the note means (blocks, their style, lists, checklist
// state, inline formats, images, tables), not how Qt draws it. Qt's HTML was
// tried first and rejected: it drops our own properties and isn't stable
// (a table after a list moves right on every save).
//
// The same document always serializes to the same text, so
// serialize(deserialize(serialize(doc))) == serialize(doc) (TC-EDT-001).
// Only meaning is stored: fonts and sizes come from the paragraph styles when
// a note is shown (task 3.2), so changing a style changes every note.
//
// Not stored (yet): nested tables, merged table cells, images other than
// attachment references. Unknown keys in stored JSON are ignored, so newer
// versions can add some without breaking older readers.
class NoteFormat
{
public:
    // notes.format_version values (models/Note.h).
    static constexpr int kLegacyHtml = models::kBodyFormatLegacyHtml;
    static constexpr int kJson = models::kBodyFormatJson; // this format
    static constexpr int kCurrent = models::kBodyFormatCurrent;

    // Our own block properties, kept by the format (Qt's HTML drops these).
    static constexpr int kCollapsedProperty = QTextFormat::UserProperty + 1; // bool, headings

    // The document as compact JSON.
    [[nodiscard]] static QString serialize(const QTextDocument& document);

    // Replaces `document`'s content. Fails, leaving it empty, on text that
    // isn't this format's JSON; the reason is returned. Never throws or
    // crashes on bad input (stored data may be corrupt or from a newer app).
    static std::optional<QString> deserialize(const QString& json, QTextDocument& document);

    // Loads a stored body of any format_version into `document`. An empty body
    // falls back to `plainText` (notes created without a body, e.g. in tests).
    static void load(const QString& body, int formatVersion, const QString& plainText,
                     QTextDocument& document);
};

} // namespace notes::editor
