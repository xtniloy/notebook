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
    StyleManager::clearDefaults(format);
    QTextCursor(block).setBlockFormat(format);
}

// Only a checklist item can be checked: a paragraph that leaves a checklist
// loses its check, so it joins one again unchecked.
void fitCheck(const QTextBlock& block)
{
    QTextBlockFormat format = block.blockFormat();
    if (StyleManager::isChecklistItem(block) || !format.hasProperty(NoteFormat::kCheckedProperty)) {
        return;
    }
    format.clearProperty(NoteFormat::kCheckedProperty);
    StyleManager::clearDefaults(format);
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

// The last of `item` and the list items nested under it.
QTextBlock lastNestedUnder(const QTextBlock& item)
{
    const int level = item.textList()->format().indent();
    QTextBlock last = item;
    for (QTextBlock next = item.next();
         next.isValid() && next.textList() != nullptr && next.textList()->format().indent() > level;
         next = next.next()) {
        last = next;
    }
    return last;
}

// The last paragraph of the run of `item`'s list: its items from `item` on
// and the items nested under them.
QTextBlock endOfList(const QTextBlock& item)
{
    const QTextList* list = item.textList();
    const int level = list->format().indent();
    QTextBlock last = item;
    for (QTextBlock next = item.next(); next.isValid(); next = next.next()) {
        const QTextList* nextList = next.textList();
        if (nextList != list && (nextList == nullptr || nextList->format().indent() <= level)) {
            break;
        }
        last = next;
    }
    return last;
}

// Adds a copy of `block` (its formats, text and images) as a new paragraph
// after `at`'s, leaving `at` at its end.
void copyBlock(QTextCursor& at, const QTextBlock& block)
{
    at.insertBlock(block.blockFormat(), block.charFormat());
    for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
        const QTextFragment fragment = it.fragment();
        const QTextCharFormat format = fragment.charFormat();
        if (format.isImageFormat()) {
            for (int i = 0; i < fragment.length(); ++i) {
                at.insertImage(format.toImageFormat());
            }
        } else {
            at.insertText(fragment.text(), format);
        }
    }
}

// Moves `first` to `last` (whole paragraphs) to after `after`, which comes
// later in the document.
void moveBlocks(const QTextBlock& first, const QTextBlock& last, const QTextBlock& after)
{
    QTextCursor at(after);
    at.movePosition(QTextCursor::EndOfBlock);
    for (QTextBlock block = first;; block = block.next()) {
        copyBlock(at, block);
        if (block == last) {
            break;
        }
    }
    QTextCursor remove(first);
    if (const QTextBlock before = first.previous(); before.isValid()) {
        // From the end of the paragraph before: what follows keeps its format.
        remove.setPosition(first.position() - 1);
        remove.setPosition(last.position() + last.length() - 1, QTextCursor::KeepAnchor);
        remove.removeSelectedText();
        return;
    }
    // The document's first paragraph: the one after takes its place (and
    // would take its format), so it gets its own back. Both are in the same
    // list: the paragraph after is an item of the run moved past.
    const QTextBlock next = last.next();
    const QTextBlockFormat format = next.blockFormat();
    const QTextCharFormat charFormat = next.charFormat();
    remove.setPosition(first.position());
    remove.setPosition(next.position(), QTextCursor::KeepAnchor);
    remove.removeSelectedText();
    remove.setBlockFormat(format);
    remove.setBlockCharFormat(charFormat);
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
    case ParagraphStyle::Checklist:
        break;
    }
    return 0;
}

bool StyleManager::isList(ParagraphStyle style)
{
    return style == ParagraphStyle::BulletedList || style == ParagraphStyle::DashedList ||
           style == ParagraphStyle::NumberedList || style == ParagraphStyle::Checklist;
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
    if (format.boolProperty(NoteFormat::kChecklistProperty)) {
        return ParagraphStyle::Checklist;
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
    case ParagraphStyle::Checklist:
        // No style either: NoteTextEdit draws the checkboxes.
        format.setStyle(QTextListFormat::ListStyleUndefined);
        format.setProperty(NoteFormat::kChecklistProperty, true);
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
        clearDefaults(format);
        QTextCursor(block).setBlockFormat(format);
        editRuns(block, [&](QTextCharFormat& run) { restyleRun(run, from, text, base); });

        QTextList* list = block.textList();
        if (!isList(style)) {
            leaveList(block);
        } else if (list != nullptr) {
            // Already a list item: the whole list changes kind.
            if (listStyleOf(*list) != style) {
                list->setFormat(listFormat(style, list->format().indent()));
                for (int i = 0; i < list->count(); ++i) {
                    fitCheck(list->item(i));
                }
            }
        } else if (joined != nullptr) {
            joined->add(block);
        } else {
            joinList(block, style, 1);
            joined = block.textList();
        }
        fitCheck(block);
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
        fitCheck(block); // a checked item stays checked
    }
    cursor.endEditBlock();
    return true;
}

void StyleManager::removeFromList(QTextCursor& cursor)
{
    cursor.beginEditBlock();
    for (const QTextBlock& block : selectedBlocks(cursor)) {
        leaveList(block);
        fitCheck(block);
    }
    cursor.endEditBlock();
}

bool StyleManager::isChecklistItem(const QTextBlock& block)
{
    const QTextList* list = block.textList();
    return list != nullptr && listStyleOf(*list) == ParagraphStyle::Checklist;
}

bool StyleManager::isChecked(const QTextBlock& block)
{
    return isChecklistItem(block) && block.blockFormat().boolProperty(NoteFormat::kCheckedProperty);
}

void StyleManager::setChecked(const QTextBlock& block, bool checked, bool toBottom)
{
    if (!isChecklistItem(block) || isChecked(block) == checked) {
        return;
    }
    QTextCursor cursor(block);
    cursor.beginEditBlock();
    QTextBlockFormat format = block.blockFormat();
    if (checked) {
        format.setProperty(NoteFormat::kCheckedProperty, true);
    } else {
        format.clearProperty(NoteFormat::kCheckedProperty);
    }
    cursor.setBlockFormat(format);
    if (checked && toBottom) {
        const QTextBlock last = lastNestedUnder(block);
        if (const QTextBlock end = endOfList(block); end != last) {
            moveBlocks(block, last, end);
        }
    }
    cursor.endEditBlock();
}

void StyleManager::clearDefaults(QTextBlockFormat& format)
{
    const auto clearIf = [&](int property, bool isDefault) {
        if (format.hasProperty(property) && isDefault) {
            format.clearProperty(property);
        }
    };
    clearIf(QTextFormat::HeadingLevel, format.headingLevel() == 0);
    clearIf(QTextFormat::BlockIndent, format.indent() == 0);
    clearIf(QTextFormat::BlockNonBreakableLines, !format.nonBreakableLines());
    clearIf(NoteFormat::kCheckedProperty, !format.boolProperty(NoteFormat::kCheckedProperty));
    clearIf(NoteFormat::kCollapsedProperty, !format.boolProperty(NoteFormat::kCollapsedProperty));
}

void StyleManager::adoptTaskLists(QTextDocument& document)
{
    for (QTextBlock block = document.begin(); block.isValid(); block = block.next()) {
        QTextBlockFormat format = block.blockFormat();
        if (!format.hasProperty(QTextFormat::BlockMarker)) {
            continue;
        }
        if (QTextList* list = block.textList(); list != nullptr && !isChecklistItem(block)) {
            list->setFormat(listFormat(ParagraphStyle::Checklist, list->format().indent()));
        }
        const bool checked = format.marker() == QTextBlockFormat::MarkerType::Checked;
        format.clearProperty(QTextFormat::BlockMarker);
        if (checked && block.textList() != nullptr) {
            format.setProperty(NoteFormat::kCheckedProperty, true);
        }
        QTextCursor(block).setBlockFormat(format);
    }
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

void StyleManager::fitText(QTextCursor& cursor)
{
    const QTextDocument* document = cursor.document();
    if (document == nullptr) {
        return;
    }
    const QFont base = document->defaultFont();
    cursor.beginEditBlock();
    for (const QTextBlock& block : selectedBlocks(cursor)) {
        if (const int level = block.blockFormat().headingLevel(); level > 0) {
            editRuns(block, [&](QTextCharFormat& run) { styleHeadingRun(run, level, base); });
        } else {
            // Only headings set a size, so a sized run here has a heading's
            // font, bold included.
            editRuns(block, [](QTextCharFormat& run) {
                if (run.hasProperty(QTextFormat::FontPointSize) ||
                    run.hasProperty(QTextFormat::FontPixelSize)) {
                    clearSize(run);
                    run.clearProperty(QTextFormat::FontWeight);
                }
            });
        }
    }
    cursor.endEditBlock();
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

bool StyleManager::selectionHas(const QTextCursor& cursor, InlineStyle style)
{
    const QTextDocument* document = cursor.document();
    if (document == nullptr || !cursor.hasSelection()) {
        return false;
    }
    const int start = cursor.selectionStart();
    const int end = cursor.selectionEnd();
    bool anyText = false;
    for (QTextBlock block = document->findBlock(start); block.isValid() && block.position() < end;
         block = block.next()) {
        for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            const bool overlaps =
                fragment.position() < end && fragment.position() + fragment.length() > start;
            if (!overlaps || fragment.charFormat().isImageFormat()) {
                continue;
            }
            if (!hasInline(fragment.charFormat(), style)) {
                return false;
            }
            anyText = true;
        }
    }
    return anyText;
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

bool StyleManager::selectsWords(const QTextCursor& cursor)
{
    const QTextDocument* document = cursor.document();
    if (document == nullptr || !cursor.hasSelection()) {
        return false;
    }
    const QTextBlock block = document->findBlock(cursor.selectionStart());
    const int textEnd = block.position() + block.length() - 1;
    if (cursor.selectionEnd() > textEnd || styleOf(block) == ParagraphStyle::Monospaced) {
        return false; // more than one paragraph, or code already
    }
    return cursor.selectionStart() > block.position() || cursor.selectionEnd() < textEnd;
}

void StyleManager::toggleInlineMonospace(QTextCursor& cursor)
{
    const QTextDocument* document = cursor.document();
    if (document == nullptr || !cursor.hasSelection()) {
        return;
    }
    const int start = cursor.selectionStart();
    const int end = cursor.selectionEnd();
    struct Run
    {
        int start = 0;
        int end = 0;
        QTextCharFormat format;
    };
    QList<Run> runs; // the selected part of each fragment, collected first
    bool allMonospaced = true;
    for (QTextBlock block = document->findBlock(start); block.isValid() && block.position() < end;
         block = block.next()) {
        for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            const int from = std::max(start, fragment.position());
            const int to = std::min(end, fragment.position() + fragment.length());
            if (from >= to || fragment.charFormat().isImageFormat()) {
                continue;
            }
            runs.append({.start = from, .end = to, .format = fragment.charFormat()});
            allMonospaced = allMonospaced && isMonospaced(fragment.charFormat());
        }
    }
    cursor.beginEditBlock();
    for (Run& run : runs) {
        clearMonospace(run.format);
        if (!allMonospaced) {
            run.format.setFontFixedPitch(true);
            run.format.setFontFamilies(QStringList{QString(kMonospaceFamily)});
        }
        QTextCursor part(cursor.document());
        part.setPosition(run.start);
        part.setPosition(run.end, QTextCursor::KeepAnchor);
        part.setCharFormat(run.format);
    }
    cursor.endEditBlock();
}

QTextCharFormat StyleManager::withoutMonospace(QTextCharFormat format)
{
    clearMonospace(format);
    return format;
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
