#pragma once

#include <QtGlobal>

#include <cstdint>

class QTextBlock;
class QTextCharFormat;
class QTextCursor;
class QTextDocument;

namespace notes::editor {

// The paragraph styles of Plan §3.5, in the Format menu's order.
enum class ParagraphStyle : std::uint8_t {
    Title,
    Heading,
    Subheading,
    Body,
    Monospaced,
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

    // 1, 2, 3 for the headings; 0 for Body and Monospaced.
    [[nodiscard]] static int headingLevel(ParagraphStyle style);

    // The style of `block`: by heading level (4 to 6, from pasted Markdown,
    // count as Subheading), else Monospaced when all its text is, else Body.
    [[nodiscard]] static ParagraphStyle styleOf(const QTextBlock& block);

    // Gives every paragraph the cursor's selection touches `style`, as one
    // undo step. Typing then continues in the style: new text takes the
    // format of the text before it, or an empty paragraph's own.
    static void apply(QTextCursor& cursor, ParagraphStyle style);

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
