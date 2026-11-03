#include "editor/StyleManager.h"

#include "editor/NoteFormat.h"

#include <QFont>
#include <QList>
#include <QStringList>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextList>

#include <algorithm>

namespace notes::editor {

namespace {

constexpr QLatin1StringView kMonospaceFamily("JetBrains Mono");

qreal headingScale(int level)
{
    switch (level) {
    case 1:
        return StyleManager::kTitleScale;
    case 2:
        return StyleManager::kHeadingScale;
    default:
        return StyleManager::kSubheadingScale;
    }
}

void clearSize(QTextCharFormat& format)
{
    format.clearProperty(QTextFormat::FontPointSize);
    format.clearProperty(QTextFormat::FontPixelSize);
    format.clearProperty(QTextFormat::FontSizeAdjustment); // Qt's Markdown headings
}

void clearMonospace(QTextCharFormat& format)
{
    format.clearProperty(QTextFormat::FontFixedPitch);
    format.clearProperty(QTextFormat::FontFamilies);
    format.clearProperty(QTextFormat::FontFamily);
}

// A heading's font: bold, its size scaled from the document's default font.
void styleHeadingRun(QTextCharFormat& format, int level, const QFont& base)
{
    clearSize(format);
    format.setFontWeight(QFont::Bold);
    const qreal scale = headingScale(level);
    if (base.pointSizeF() > 0) {
        format.setFontPointSize(base.pointSizeF() * scale);
    } else { // a pixel-sized default font
        format.setProperty(QTextFormat::FontPixelSize, qRound(base.pixelSize() * scale));
    }
}

// The run's format in `style`, coming from `from`. What the old style put
// there goes (a heading's size and weight, Monospaced's font); what the user
// added stays (bold in Body, an inline monospaced word).
void restyleRun(QTextCharFormat& format, ParagraphStyle from, ParagraphStyle style,
                const QFont& base)
{
    const bool wasHeading = StyleManager::headingLevel(from) > 0;
    if (wasHeading) {
        clearSize(format);
        format.clearProperty(QTextFormat::FontWeight);
    }
    if (from == ParagraphStyle::Monospaced || StyleManager::headingLevel(style) > 0) {
        clearMonospace(format);
    }
    if (const int level = StyleManager::headingLevel(style); level > 0) {
        styleHeadingRun(format, level, base);
    } else if (style == ParagraphStyle::Monospaced) {
        format.setFontFixedPitch(true);
        format.setFontFamilies(QStringList{QString(kMonospaceFamily)});
    }
}

// Applies `edit` to every fragment's char format in `block`, and to the
// block's own char format (what an empty paragraph types in).
template <typename Edit>
void editRuns(const QTextBlock& block, Edit edit)
{
    struct Run
    {
        int position = 0;
        int length = 0;
        QTextCharFormat format;
    };
    QList<Run> runs; // collected first: editing splits and merges fragments
    for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
        const QTextFragment fragment = it.fragment();
        if (!fragment.charFormat().isImageFormat()) {
            runs.append({fragment.position(), fragment.length(), fragment.charFormat()});
        }
    }
    QTextCursor cursor(block);
    for (Run& run : runs) {
        edit(run.format);
        cursor.setPosition(run.position);
        cursor.setPosition(run.position + run.length, QTextCursor::KeepAnchor);
        cursor.setCharFormat(run.format);
    }
    QTextCharFormat blockFormat = block.charFormat();
    edit(blockFormat);
    QTextCursor(block).setBlockCharFormat(blockFormat);
}

// The text style of `block` regardless of any list: heading, Monospaced or
// Body. A list item's text is Body (or Monospaced).
ParagraphStyle textStyleOf(const QTextBlock& block)
{
    switch (block.blockFormat().headingLevel()) {
    case 0:
        break;
    case 1:
        return ParagraphStyle::Title;
    case 2:
        return ParagraphStyle::Heading;
    default:
        return ParagraphStyle::Subheading;
    }
    bool hasText = false;
    for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
        const QTextCharFormat format = it.fragment().charFormat();
        if (format.isImageFormat()) {
            continue;
        }
        if (!StyleManager::isMonospaced(format)) {
            return ParagraphStyle::Body;
        }
        hasText = true;
    }
    if (!hasText && !StyleManager::isMonospaced(block.charFormat())) {
        return ParagraphStyle::Body;
    }
    return ParagraphStyle::Monospaced;
}

// Takes `block` out of its list. QTextList::remove() adds the list's indent to
// the paragraph's; the paragraph keeps the indent it had instead.
void leaveList(const QTextBlock& block)
{
    QTextList* list = block.textList();
    if (list == nullptr) {
        return;
    }
    const int indent = block.blockFormat().indent();
    list->remove(block);
    QTextBlockFormat format = block.blockFormat();
    format.setIndent(indent);
    QTextCursor(block).setBlockFormat(format);
}

// Puts `block` into a list of `style` at `indent`: the nearest list above at
// that level when it is the same kind (so its numbering continues), else a
// new one. `block` must not be in a list.
void joinList(const QTextBlock& block, ParagraphStyle style, int indent)
{
    for (QTextBlock above = block.previous(); above.isValid() && above.textList() != nullptr;
         above = above.previous()) {
        QTextList* list = above.textList();
        const int level = list->format().indent();
        if (level == indent && StyleManager::listStyleOf(*list) == style) {
            list->add(block);
            return;
        }
        if (level < indent) {
            break; // a shallower item: no list at this level to continue
        }
    }
    QTextCursor(block).createList(StyleManager::listFormat(style, indent));
}

// The selected paragraphs, first to last.
QList<QTextBlock> selectedBlocks(const QTextCursor& cursor)
{
    QList<QTextBlock> blocks;
    const QTextDocument* document = cursor.document();
    const QTextBlock last = document->findBlock(cursor.selectionEnd());
    for (QTextBlock block = document->findBlock(cursor.selectionStart()); block.isValid();
         block = block.next()) {
        blocks.append(block);
        if (block == last) {
            break;
        }
    }
    return blocks;
}

} // namespace

int StyleManager::headingLevel(ParagraphStyle style)
{
    switch (style) {
    case ParagraphStyle::Title:
        return 1;
    case ParagraphStyle::Heading:
        return 2;
    case ParagraphStyle::Subheading:
        return 3;
    case ParagraphStyle::Body:
    case ParagraphStyle::Monospaced:
    case ParagraphStyle::BulletedList:
    case ParagraphStyle::DashedList:
    case ParagraphStyle::NumberedList:
        break;
    }
    return 0;
}

bool StyleManager::isList(ParagraphStyle style)
{
    return style == ParagraphStyle::BulletedList || style == ParagraphStyle::DashedList ||
           style == ParagraphStyle::NumberedList;
}

ParagraphStyle StyleManager::styleOf(const QTextBlock& block)
{
    if (const QTextList* list = block.textList()) {
        return listStyleOf(*list);
    }
    return textStyleOf(block);
}

ParagraphStyle StyleManager::listStyleOf(const QTextList& list)
{
    const QTextListFormat format = list.format();
    if (format.boolProperty(NoteFormat::kDashedListProperty)) {
        return ParagraphStyle::DashedList;
    }
    switch (format.style()) {
    case QTextListFormat::ListDecimal:
    case QTextListFormat::ListLowerAlpha:
    case QTextListFormat::ListUpperAlpha:
    case QTextListFormat::ListLowerRoman:
    case QTextListFormat::ListUpperRoman:
        return ParagraphStyle::NumberedList;
    default:
        return ParagraphStyle::BulletedList;
    }
}

QTextListFormat StyleManager::listFormat(ParagraphStyle style, int indent)
{
    QTextListFormat format;
    format.setIndent(std::clamp(indent, 1, kMaxListIndent));
    switch (style) {
    case ParagraphStyle::DashedList:
        // Qt has no dash marker: no style (Qt draws none), and NoteTextEdit
        // draws the dash (NoteFormat::kDashedListProperty).
        format.setStyle(QTextListFormat::ListStyleUndefined);
        format.setProperty(NoteFormat::kDashedListProperty, true);
        break;
    case ParagraphStyle::NumberedList:
        format.setStyle(QTextListFormat::ListDecimal);
        break;
    default:
        format.setStyle(QTextListFormat::ListDisc);
        break;
    }
    return format;
}

void StyleManager::apply(QTextCursor& cursor, ParagraphStyle style)
{
    const QTextDocument* document = cursor.document();
    if (document == nullptr) {
        return;
    }
    const QFont base = document->defaultFont();
    const QList<QTextBlock> blocks = selectedBlocks(cursor);
    // A list item's text is Body.
    const ParagraphStyle text = isList(style) ? ParagraphStyle::Body : style;

    cursor.beginEditBlock();
    QTextList* joined = nullptr; // the list the selection's plain paragraphs went into
    for (const QTextBlock& block : blocks) {
        const ParagraphStyle from = textStyleOf(block);
        QTextBlockFormat format = block.blockFormat();
        format.setHeadingLevel(headingLevel(text));
        if (text != ParagraphStyle::Monospaced) {
            // A code block (from pasted Markdown) becomes ordinary text.
            format.clearProperty(QTextFormat::BlockCodeFence);
            format.clearProperty(QTextFormat::BlockCodeLanguage);
            format.setNonBreakableLines(false);
        }
        QTextCursor(block).setBlockFormat(format);
        editRuns(block, [&](QTextCharFormat& run) { restyleRun(run, from, text, base); });

        QTextList* list = block.textList();
        if (!isList(style)) {
            leaveList(block);
        } else if (list != nullptr) {
            // Already a list item: the whole list changes kind.
            if (listStyleOf(*list) != style) {
                list->setFormat(listFormat(style, list->format().indent()));
            }
        } else if (joined != nullptr) {
            joined->add(block);
        } else {
            joinList(block, style, 1);
            joined = block.textList();
        }
    }
    cursor.endEditBlock();
}

bool StyleManager::indentList(QTextCursor& cursor, int levels)
{
    QList<QTextBlock> items = selectedBlocks(cursor);
    items.removeIf([](const QTextBlock& block) { return block.textList() == nullptr; });
    if (items.isEmpty()) {
        return false;
    }
    cursor.beginEditBlock();
    for (const QTextBlock& block : items) {
        const QTextList* list = block.textList();
        const ParagraphStyle style = listStyleOf(*list);
        const int from = list->format().indent();
        const int to = std::min(from + levels, kMaxListIndent);
        if (to == from) {
            continue;
        }
        leaveList(block);
        if (to >= 1) {
            joinList(block, style, to);
        }
    }
    cursor.endEditBlock();
    return true;
}

void StyleManager::removeFromList(QTextCursor& cursor)
{
    cursor.beginEditBlock();
    for (const QTextBlock& block : selectedBlocks(cursor)) {
        leaveList(block);
    }
    cursor.endEditBlock();
}

void StyleManager::styleHeadings(QTextDocument& document)
{
    const QFont base = document.defaultFont();
    for (QTextBlock block = document.begin(); block.isValid(); block = block.next()) {
        if (const int level = block.blockFormat().headingLevel(); level > 0) {
            editRuns(block, [&](QTextCharFormat& run) { styleHeadingRun(run, level, base); });
        }
    }
}

bool StyleManager::hasInline(const QTextCharFormat& format, InlineStyle style)
{
    switch (style) {
    case InlineStyle::Bold:
        return format.fontWeight() >= QFont::Bold;
    case InlineStyle::Italic:
        return format.fontItalic();
    case InlineStyle::Underline:
        return format.fontUnderline();
    case InlineStyle::Strikethrough:
        return format.fontStrikeOut();
    }
    return false;
}

QTextCharFormat StyleManager::inlineFormat(InlineStyle style, bool on)
{
    QTextCharFormat format;
    switch (style) {
    case InlineStyle::Bold:
        format.setFontWeight(on ? QFont::Bold : QFont::Normal);
        break;
    case InlineStyle::Italic:
        format.setFontItalic(on);
        break;
    case InlineStyle::Underline:
        format.setFontUnderline(on);
        break;
    case InlineStyle::Strikethrough:
        format.setFontStrikeOut(on);
        break;
    }
    return format;
}

void StyleManager::setInline(QTextCursor& cursor, InlineStyle style, bool on)
{
    const QTextDocument* document = cursor.document();
    if (document == nullptr || !cursor.hasSelection()) {
        return;
    }
    const QTextCharFormat format = inlineFormat(style, on);
    const int start = cursor.selectionStart();
    const int end = cursor.selectionEnd();
    cursor.beginEditBlock();
    for (QTextBlock block = document->findBlock(start); block.isValid() && block.position() < end;
         block = block.next()) {
        if (style == InlineStyle::Bold && block.blockFormat().headingLevel() > 0) {
            continue;
        }
        QTextCursor part(cursor.document());
        part.setPosition(std::max(start, block.position()));
        part.setPosition(std::min(end, block.position() + block.length() - 1),
                         QTextCursor::KeepAnchor);
        if (part.hasSelection()) {
            part.mergeCharFormat(format);
        }
    }
    cursor.endEditBlock();
}

bool StyleManager::isMonospaced(const QTextCharFormat& format)
{
    if (format.fontFixedPitch()) {
        return true;
    }
    const QStringList families = format.fontFamilies().toStringList();
    return families.contains(QStringLiteral("monospace")) || families.contains(kMonospaceFamily);
}

} // namespace notes::editor
