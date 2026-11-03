#include "editor/StyleManager.h"

#include <QFont>
#include <QList>
#include <QStringList>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>

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
        break;
    }
    return 0;
}

ParagraphStyle StyleManager::styleOf(const QTextBlock& block)
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
        if (!isMonospaced(format)) {
            return ParagraphStyle::Body;
        }
        hasText = true;
    }
    if (!hasText && !isMonospaced(block.charFormat())) {
        return ParagraphStyle::Body;
    }
    return ParagraphStyle::Monospaced;
}

void StyleManager::apply(QTextCursor& cursor, ParagraphStyle style)
{
    const QTextDocument* document = cursor.document();
    if (document == nullptr) {
        return;
    }
    const QFont base = document->defaultFont();
    const QTextBlock first = document->findBlock(cursor.selectionStart());
    const QTextBlock last = document->findBlock(cursor.selectionEnd());

    cursor.beginEditBlock();
    for (QTextBlock block = first; block.isValid(); block = block.next()) {
        const ParagraphStyle from = styleOf(block);
        QTextBlockFormat format = block.blockFormat();
        format.setHeadingLevel(headingLevel(style));
        if (style != ParagraphStyle::Monospaced) {
            // A code block (from pasted Markdown) becomes ordinary text.
            format.clearProperty(QTextFormat::BlockCodeFence);
            format.clearProperty(QTextFormat::BlockCodeLanguage);
            format.setNonBreakableLines(false);
        }
        QTextCursor(block).setBlockFormat(format);
        editRuns(block, [&](QTextCharFormat& run) { restyleRun(run, from, style, base); });
        if (block == last) {
            break;
        }
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
