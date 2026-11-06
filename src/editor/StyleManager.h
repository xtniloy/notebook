#pragma once

#include <QtGlobal>

#include <cstdint>

class QTextBlock;
class QTextBlockFormat;
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
    // A checklist (task 3.6): items with a checkbox. Not in the Aa menu; the
    // Checklist action (Ctrl+Shift+L) turns it on and off.
    Checklist,
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
    // The kind of list: BulletedList, DashedList, NumberedList or Checklist.
    [[nodiscard]] static ParagraphStyle listStyleOf(const QTextList& list);
    // A list of `style` (a list style) at nesting level `indent` (1 = top).
    [[nodiscard]] static QTextListFormat listFormat(ParagraphStyle style, int indent);

    // Gives every paragraph the cursor's selection touches `style`, as one
    // undo step. Typing then continues in the style: new text takes the
    // format of the text before it, or an empty paragraph's own.
    //
    // A list style makes the paragraphs list items: they join the list just
    // above when it is the same kind, and a list they are already in changes
    // kind as a whole. Any other style takes them out of their list. Items
    // that become checklist items start unchecked; others lose their checkbox.
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

    // Checklists (task 3.6). Whether an item is checked is our own block
    // property (NoteFormat::kCheckedProperty); NoteTextEdit draws the boxes.
    [[nodiscard]] static bool isChecklistItem(const QTextBlock& block);
    [[nodiscard]] static bool isChecked(const QTextBlock& block);
    // Checks or unchecks the checklist item `block`, as one undo step. With
    // `toBottom`, a checked item, with the items nested under it, then moves
    // below the last item of its checklist.
    static void setChecked(const QTextBlock& block, bool checked, bool toBottom);
    // Turns Markdown task lists ("- [x] ..." read by QTextDocument::setMarkdown,
    // which marks them with Qt's block markers) into our checklists, and
    // removes the markers. For Markdown coming into a note; not an undo step
    // of its own.
    static void adoptTaskLists(QTextDocument& document);

    // Removes block properties that only repeat Qt's defaults (heading level
    // 0, indent 0, no-break off, unchecked, not collapsed), so a plain
    // paragraph's format is the default one. Qt's Enter on an empty paragraph
    // whose format isn't the default resets it instead of starting a new
    // line: such leftovers made Enter do nothing once on empty lines.
    static void clearDefaults(QTextBlockFormat& format);

    // Heading fonts for every heading in `document`, after loading (not an
    // edit: the caller decides about undo).
    static void styleHeadings(QTextDocument& document);
    // Gives the text of the selected paragraphs their style's font, as part of
    // the current edit: a heading's size and bold, and no heading size or bold
    // elsewhere. For inserted text, which comes in the cursor's font.
    static void fitText(QTextCursor& cursor);

    // Whether text in `format` is monospaced: ours (JetBrains Mono, fixed
    // pitch) or Qt's Markdown code ("monospace" family).
    [[nodiscard]] static bool isMonospaced(const QTextCharFormat& format);

    // Inline formats (task 3.4).
    [[nodiscard]] static bool hasInline(const QTextCharFormat& format, InlineStyle style);
    // Whether every character the cursor's selection covers has `style`
    // (a heading's bold counts). False when it covers no text. This is the
    // state Bold and the others show for a selection, as in other editors:
    // pressing one then removes it from fully formatted text and adds it to
    // a mixed selection, whichever end the selection was made from.
    [[nodiscard]] static bool selectionHas(const QTextCursor& cursor, InlineStyle style);
    // The format to merge to turn `style` on or off.
    [[nodiscard]] static QTextCharFormat inlineFormat(InlineStyle style, bool on);
    // Turns `style` on or off for the cursor's selection, as one undo step.
    // Bold skips headings: their bold is the style's (and isn't stored), so
    // the user's bold or not-bold wouldn't survive saving there.
    static void setInline(QTextCursor& cursor, InlineStyle style, bool on);

    // Inline code. Whether Monospaced, chosen for the cursor's selection,
    // means its words rather than its paragraph: the selection is part of one
    // paragraph's text, and that paragraph isn't code already.
    [[nodiscard]] static bool selectsWords(const QTextCursor& cursor);
    // Makes the selected text monospaced, or plain again when all of it
    // already is, as one undo step.
    static void toggleInlineMonospace(QTextCursor& cursor);
    // `format` without monospace: what typing after inline code goes on in
    // once Monospaced is turned off there.
    [[nodiscard]] static QTextCharFormat withoutMonospace(QTextCharFormat format);
};

} // namespace notes::editor
