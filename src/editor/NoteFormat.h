#pragma once

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
// Every note body is this JSON; there is no other stored format. Not stored
// (yet): nested tables, merged table cells, images other than attachment
// references. Unknown keys in stored JSON are ignored, so newer versions can
// add some without breaking older readers. A change older readers would
// misread instead raises kVersion, which the JSON carries as "v".

class NoteFormat
{
public:
    // The format's version, "v" in the JSON (omitted while it is 1). Bodies
    // from a newer version fail to load, and the note's plain text shows.
    static constexpr int kVersion = 1;

    // Our own block properties, kept by the format (Qt's HTML drops these).
    static constexpr int kCollapsedProperty = QTextFormat::UserProperty + 1; // bool, headings
    // bool, on a QTextListFormat: a dashed list ("dash"). Qt has no dash
    // marker, so such a list has no style (Qt draws no marker) and
    // NoteTextEdit draws the dash.
    static constexpr int kDashedListProperty = QTextFormat::UserProperty + 2;
    // bool, on a QTextListFormat: a checklist ("check"), also without a style;
    // NoteTextEdit draws its items' checkboxes.
    static constexpr int kChecklistProperty = QTextFormat::UserProperty + 3;
    // bool, on a checklist item's block format: checked ("check": true).
    // Not Qt's block marker: QTextEdit has its own click handling for those,
    // which toggled the item with the caret as well as the one clicked.
    static constexpr int kCheckedProperty = QTextFormat::UserProperty + 4;

    // The document as compact JSON.
    [[nodiscard]] static QString serialize(const QTextDocument& document);

    // Replaces `document`'s content. Fails, leaving it empty, on text that
    // isn't this format's JSON; the reason is returned. Never throws or
    // crashes on bad input (stored data may be corrupt or from a newer app).
    static std::optional<QString> deserialize(const QString& json, QTextDocument& document);

    // Loads a stored body into `document`. An empty body (notes from before
    // the format, notes created without one) or an unreadable one shows
    // `plainText` instead.
    static void load(const QString& body, const QString& plainText, QTextDocument& document);
};

} // namespace notes::editor
