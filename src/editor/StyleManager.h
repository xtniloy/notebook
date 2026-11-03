#pragma once

#include <QtGlobal>

#include <cstdint>

class QTextBlock;
class QTextCharFormat;
class QTextCursor;
class QTextDocument;
class QTextList;
class QTextListFormat;

namespace notes::editor {

// The paragraph styles of Plan §3.5, in the Format menu's order.
enum class ParagraphStyle : std::uint8_t {
    Title,
    Heading,
    Subheading,
    Body,
    Monospaced,
    // Lists (task 3.5): Body text in a list.
    BulletedList,
    DashedList,
    NumberedList,
};

// Inline formats (task 3.4), in the Format menu's order.
enum class InlineStyle : std::uint8_t {
    Bold,
    Italic,
    Underline,
    Strikethrough,
};

// Paragraph styles (task 3.2). A style is stored as what it means: Title,
// Heading and Subheading are heading levels 1, 2 and 3, Monospaced is text in
// JetBrains Mono, Body is neither. Fonts and sizes are not stored (ADR 0006):
// they follow from the style whenever a note is shown, relative to the
// document's default font, so the note looks right at any system font size.
//
// Headings are bold because of their style, so their weight isn't the user's
// bold: NoteFormat doesn't save it, and styles() restores it on loading.
class StyleManager
{
public:
    // Heading sizes, relative to the body text (11 pt -> 19.25 / 15.4 / 12.65 pt).
    static constexpr qreal kTitleScale = 1.75;
    static constexpr qreal kHeadingScale = 1.4;
    static constexpr qreal kSubheadingScale = 1.15;

    // How deep lists nest.
    static constexpr int kMaxListIndent = 8;

    // 1, 2, 3 for the headings; 0 for the others.
    [[nodiscard]] static int headingLevel(ParagraphStyle style);
    [[nodiscard]] static bool isList(ParagraphStyle style);

    // The style of `block`: its list's kind when it is in a list (bulleted
    // covers Qt's discs, circles and squares; numbered its numbers, letters
    // and Roman numerals), else by heading level (4 to 6, from pasted
    // Markdown, count as Subheading), else Monospaced when all its text is,
    // else Body.
    [[nodiscard]] static ParagraphStyle styleOf(const QTextBlock& block);
    // The kind of list: BulletedList, DashedList or NumberedList.
    [[nodiscard]] static ParagraphStyle listStyleOf(const QTextList& list);
    // A list of `style` (a list style) at nesting level `indent` (1 = top).
    [[nodiscard]] static QTextListFormat listFormat(ParagraphStyle style, int indent);

    // Gives every paragraph the cursor's selection touches `style`, as one
    // undo step. Typing then continues in the style: new text takes the
    // format of the text before it, or an empty paragraph's own.
    //
    // A list style makes the paragraphs list items: they join the list just
    // above when it is the same kind, and a list they are already in changes
    // kind as a whole. Any other style takes them out of their list.
    static void apply(QTextCursor& cursor, ParagraphStyle style);

    // Moves the selected list items `levels` deeper (Tab: 1) or shallower
    // (Shift+Tab: -1), as one undo step. An item joins the nearest list above
    // at its new level, so a new nested level starts its numbering at 1; out
    // of the top level, an item leaves its list. False if no list item is
    // selected.
    static bool indentList(QTextCursor& cursor, int levels);
    // Takes the selected paragraphs out of their lists, as one undo step;
    // their text and style stay.
    static void removeFromList(QTextCursor& cursor);

    // Heading fonts for every heading in `document`, after loading (not an
    // edit: the caller decides about undo).
    static void styleHeadings(QTextDocument& document);

    // Whether text in `format` is monospaced: ours (JetBrains Mono, fixed
    // pitch) or Qt's Markdown code ("monospace" family).
    [[nodiscard]] static bool isMonospaced(const QTextCharFormat& format);

    // Inline formats (task 3.4).
    [[nodiscard]] static bool hasInline(const QTextCharFormat& format, InlineStyle style);
    // The format to merge to turn `style` on or off.
    [[nodiscard]] static QTextCharFormat inlineFormat(InlineStyle style, bool on);
    // Turns `style` on or off for the cursor's selection, as one undo step.
    // Bold skips headings: their bold is the style's (and isn't stored), so
    // the user's bold or not-bold wouldn't survive saving there.
    static void setInline(QTextCursor& cursor, InlineStyle style, bool on);
};

} // namespace notes::editor
