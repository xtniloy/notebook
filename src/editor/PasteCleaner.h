#pragma once

#include <QString>
#include <QStringView>

namespace notes::editor {

// Rich text coming into a note from other apps (task 3.8): HTML from web pages
// and word processors, and Markdown (an AI chat answer's Copy button).
//
// The result is a note body in our own format (NoteFormat's JSON) holding only
// what a note can mean: headings, lists and checklists, code blocks, bold,
// italic, underline, strikethrough, monospaced text, tables, and links to
// http(s) and mailto addresses. Everything else (fonts, sizes, colors,
// backgrounds, alignment, margins, scripts, styles, event attributes, images)
// can't pass, because the format has no place for it: the input is read into
// a document, written out as JSON, and only the known keys are kept.
//
// Nothing is ever loaded while reading: images and style sheets the HTML
// names are neither fetched nor read from disk (tracking pixels), and pasted
// images are dropped. (A copied image still pastes as an image: that's the
// image on the clipboard, not HTML.)
class PasteCleaner
{
public:
    // The clean body for pasted HTML; empty when nothing is left.
    [[nodiscard]] static QString fromHtml(const QString& html);
    // The clean body for Markdown (CommonMark with GitHub's tables and task
    // lists), which goes through the same cleaning.
    [[nodiscard]] static QString fromMarkdown(const QString& markdown);

    // Whether plain text clearly is Markdown, so pasting converts it: several
    // different signs (a heading, a list of at least two items, **bold**, a
    // link, inline code, a quote), or one strong one (a fenced code block, a
    // table). Prose with a lone "*" or "-" isn't.
    [[nodiscard]] static bool looksLikeMarkdown(QStringView text);
};

} // namespace notes::editor
