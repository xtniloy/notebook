#include "editor/NoteFormat.h"

#include "editor/StyleManager.h"

#include <QColor>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFrame>
#include <QTextList>
#include <QTextTable>

#include <algorithm>
#include <array>
#include <utility>

namespace notes::editor {

namespace {

// JSON shape (NoteFormat::kVersion 1; "v" is written from version 2 on):
//
//   { "v": <version>,
//     "lists":  [ { "style": "disc", "indent": 1 }, ... ],   // "dash", "check": ours
//     "blocks": [ <paragraph> | { "table": { "rows": 2, "cols": 2,
//                                            "cells": [ [<paragraph>...], ... ] } } ] }
//
//   <paragraph> = { "runs": [ <run>... ],          // omitted when empty
//                   "h": 1..6, "list": <index into "lists">, "check": true/false,
//                   "indent": n, "align": "center"|"right"|"justify",
//                   "nobreak": true, "collapsed": true,
//                   "code": "<language>" }   // a fenced code block; each only when set
//   <run> = { "text": "...", "b", "i", "u", "s", "mono": true,
//             "bg": "#rrggbb", "fg": "#rrggbb", "href": "..." }
//         | { "image": "attachment://<uuid>", "w": 120, "h": 60 }
//
// Cells are row-major. Keys appear only when they differ from the default, so
// the output stays small and every document has exactly one serialization.

constexpr std::array kListStyles{
    std::pair{QTextListFormat::ListDisc, "disc"},
    std::pair{QTextListFormat::ListCircle, "circle"},
    std::pair{QTextListFormat::ListSquare, "square"},
    std::pair{QTextListFormat::ListDecimal, "decimal"},
    std::pair{QTextListFormat::ListLowerAlpha, "lower-alpha"},
    std::pair{QTextListFormat::ListUpperAlpha, "upper-alpha"},
    std::pair{QTextListFormat::ListLowerRoman, "lower-roman"},
    std::pair{QTextListFormat::ListUpperRoman, "upper-roman"},
};

constexpr std::array kAlignments{
    std::pair{Qt::AlignHCenter, "center"},
    std::pair{Qt::AlignRight, "right"},
    std::pair{Qt::AlignJustify, "justify"},
};

// Sanity limits for stored data: far beyond any real note, small enough that
// corrupt input can't make the editor allocate absurd amounts.
constexpr int kMaxHeading = 6;
constexpr int kMaxIndent = 16;
constexpr int kMaxTableSide = 1000;

QString colorName(const QColor& color)
{
    return color.alpha() == 255 ? color.name(QColor::HexRgb) : color.name(QColor::HexArgb);
}

// ---- Serializing

// `inHeading`: a heading's bold comes from its style (StyleManager), so it
// isn't the user's bold and isn't stored.
QJsonObject runFor(const QTextCharFormat& format, const QString& text, bool inHeading)
{
    QJsonObject run{{QStringLiteral("text"), text}};
    if (!inHeading && format.fontWeight() >= QFont::Bold) {
        run.insert(QStringLiteral("b"), true);
    }
    if (format.fontItalic()) {
        run.insert(QStringLiteral("i"), true);
    }
    if (format.fontUnderline()) {
        run.insert(QStringLiteral("u"), true);
    }
    if (format.fontStrikeOut()) {
        run.insert(QStringLiteral("s"), true);
    }
    if (StyleManager::isMonospaced(format)) {
        run.insert(QStringLiteral("mono"), true);
    }
    if (format.background().style() != Qt::NoBrush) {
        run.insert(QStringLiteral("bg"), colorName(format.background().color()));
    }
    if (format.foreground().style() != Qt::NoBrush) {
        run.insert(QStringLiteral("fg"), colorName(format.foreground().color()));
    }
    if (format.isAnchor() && !format.anchorHref().isEmpty()) {
        run.insert(QStringLiteral("href"), format.anchorHref());
    }
    return run;
}

// Whether two text runs differ only in their text, so they can be one run.
bool sameStyle(QJsonObject a, QJsonObject b)
{
    if (!a.contains(QStringLiteral("text")) || !b.contains(QStringLiteral("text"))) {
        return false;
    }
    a.remove(QStringLiteral("text"));
    b.remove(QStringLiteral("text"));
    return a == b;
}

class Writer
{
public:
    QJsonObject paragraph(const QTextBlock& block)
    {
        QJsonObject node;
        const QTextBlockFormat format = block.blockFormat();
        if (const int level = format.headingLevel(); level > 0) {
            node.insert(QStringLiteral("h"), level);
        }
        if (QTextList* list = block.textList()) {
            node.insert(QStringLiteral("list"), listIndex(list));
        }
        if (StyleManager::isChecklistItem(block)) {
            node.insert(QStringLiteral("check"), StyleManager::isChecked(block));
        }
        if (format.indent() > 0) {
            node.insert(QStringLiteral("indent"), format.indent());
        }
        const Qt::Alignment horizontal = format.alignment() & Qt::AlignHorizontal_Mask;
        for (const auto& [alignment, name] : kAlignments) {
            if (horizontal == alignment) {
                node.insert(QStringLiteral("align"), QLatin1StringView(name));
            }
        }
        if (format.nonBreakableLines()) {
            node.insert(QStringLiteral("nobreak"), true);
        }
        if (format.boolProperty(NoteFormat::kCollapsedProperty)) {
            node.insert(QStringLiteral("collapsed"), true);
        }
        if (format.hasProperty(QTextFormat::BlockCodeFence)) {
            node.insert(QStringLiteral("code"),
                        format.stringProperty(QTextFormat::BlockCodeLanguage));
        }
        if (const QJsonArray runs = runsOf(block); !runs.isEmpty()) {
            node.insert(QStringLiteral("runs"), runs);
        }
        return node;
    }

    QJsonObject table(const QTextTable& table)
    {
        QJsonArray cells;
        for (int row = 0; row < table.rows(); ++row) {
            for (int column = 0; column < table.columns(); ++column) {
                const QTextTableCell cell = table.cellAt(row, column);
                QJsonArray content;
                // Paragraphs only: nested tables aren't part of the format.
                for (QTextFrame::iterator it = cell.begin(); !it.atEnd(); ++it) {
                    if (it.currentFrame() == nullptr) {
                        content.append(paragraph(it.currentBlock()));
                    }
                }
                cells.append(content);
            }
        }
        return {{QStringLiteral("table"), QJsonObject{{QStringLiteral("rows"), table.rows()},
                                                      {QStringLiteral("cols"), table.columns()},
                                                      {QStringLiteral("cells"), cells}}}};
    }

    [[nodiscard]] QJsonArray lists() const { return m_lists; }

private:
    int listIndex(QTextList* list)
    {
        if (const auto known = m_listIndex.constFind(list); known != m_listIndex.constEnd()) {
            return known.value();
        }
        const QTextListFormat format = list->format();
        QJsonObject entry{{QStringLiteral("indent"), format.indent()}};
        if (format.boolProperty(NoteFormat::kDashedListProperty)) {
            entry.insert(QStringLiteral("style"), QStringLiteral("dash"));
        } else if (format.boolProperty(NoteFormat::kChecklistProperty)) {
            entry.insert(QStringLiteral("style"), QStringLiteral("check"));
        }
        for (const auto& [style, name] : kListStyles) {
            if (!entry.contains(QStringLiteral("style")) && format.style() == style) {
                entry.insert(QStringLiteral("style"), QLatin1StringView(name));
            }
        }
        const auto index = static_cast<int>(m_lists.size());
        m_lists.append(entry);
        m_listIndex.insert(list, index);
        return index;
    }

    static QJsonArray runsOf(const QTextBlock& block)
    {
        QJsonArray runs;
        for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            const QTextCharFormat format = fragment.charFormat();
            if (format.isImageFormat()) {
                const QTextImageFormat image = format.toImageFormat();
                // One run per image: a fragment can hold several identical ones.
                for (qsizetype i = 0; i < fragment.length(); ++i) {
                    QJsonObject run{{QStringLiteral("image"), image.name()}};
                    if (image.width() > 0) {
                        run.insert(QStringLiteral("w"), image.width());
                    }
                    if (image.height() > 0) {
                        run.insert(QStringLiteral("h"), image.height());
                    }
                    runs.append(run);
                }
                continue;
            }
            const QJsonObject run =
                runFor(format, fragment.text(), block.blockFormat().headingLevel() > 0);
            // Fragments can differ in properties the format doesn't keep (a
            // font size): merge them, so a document has one serialization.
            if (!runs.isEmpty() && sameStyle(runs.last().toObject(), run)) {
                QJsonObject merged = runs.last().toObject();
                merged.insert(QStringLiteral("text"),
                              merged.value(QStringLiteral("text")).toString() + fragment.text());
                runs.replace(runs.size() - 1, merged);
            } else {
                runs.append(run);
            }
        }
        return runs;
    }

    QJsonArray m_lists;
    QHash<QTextList*, int> m_listIndex;
};

// ---- Deserializing

QTextCharFormat charFormatFor(const QJsonObject& run)
{
    QTextCharFormat format;
    if (run.value(QStringLiteral("b")).toBool()) {
        format.setFontWeight(QFont::Bold);
    }
    if (run.value(QStringLiteral("i")).toBool()) {
        format.setFontItalic(true);
    }
    if (run.value(QStringLiteral("u")).toBool()) {
        format.setFontUnderline(true);
    }
    if (run.value(QStringLiteral("s")).toBool()) {
        format.setFontStrikeOut(true);
    }
    if (run.value(QStringLiteral("mono")).toBool()) {
        format.setFontFixedPitch(true);
        format.setFontFamilies(QStringList{QStringLiteral("JetBrains Mono")});
    }
    if (const QColor bg = QColor::fromString(run.value(QStringLiteral("bg")).toString());
        bg.isValid()) {
        format.setBackground(bg);
    }
    if (const QColor fg = QColor::fromString(run.value(QStringLiteral("fg")).toString());
        fg.isValid()) {
        format.setForeground(fg);
    }
    if (const QString href = run.value(QStringLiteral("href")).toString(); !href.isEmpty()) {
        format.setAnchor(true);
        format.setAnchorHref(href);
    }
    return format;
}

QTextBlockFormat blockFormatFor(const QJsonObject& node)
{
    QTextBlockFormat format;
    format.setHeadingLevel(std::clamp(node.value(QStringLiteral("h")).toInt(), 0, kMaxHeading));
    // Only a checklist item's: on any other paragraph it's ignored.
    if (node.value(QStringLiteral("check")).toBool()) {
        format.setProperty(NoteFormat::kCheckedProperty, true);
    }
    format.setIndent(std::clamp(node.value(QStringLiteral("indent")).toInt(), 0, kMaxIndent));
    const QString align = node.value(QStringLiteral("align")).toString();
    for (const auto& [alignment, name] : kAlignments) {
        if (align == QLatin1StringView(name)) {
            format.setAlignment(alignment);
        }
    }
    if (node.value(QStringLiteral("nobreak")).toBool()) {
        format.setNonBreakableLines(true);
    }
    if (node.value(QStringLiteral("collapsed")).toBool()) {
        format.setProperty(NoteFormat::kCollapsedProperty, true);
    }
    if (const QJsonValue code = node.value(QStringLiteral("code")); code.isString()) {
        format.setProperty(QTextFormat::BlockCodeFence, QStringLiteral("`"));
        format.setProperty(QTextFormat::BlockCodeLanguage, code.toString());
    }
    StyleManager::clearDefaults(format); // a plain paragraph's is Qt's default
    return format;
}

QTextListFormat listFormatFor(const QJsonObject& entry)
{
    QTextListFormat format;
    format.setStyle(QTextListFormat::ListDisc);
    const QString style = entry.value(QStringLiteral("style")).toString();
    if (style == u"dash") {
        // Qt has no dash marker: no style (Qt then draws none) and our
        // property, and the editor draws the dash.
        format.setStyle(QTextListFormat::ListStyleUndefined);
        format.setProperty(NoteFormat::kDashedListProperty, true);
    } else if (style == u"check") {
        format.setStyle(QTextListFormat::ListStyleUndefined);
        format.setProperty(NoteFormat::kChecklistProperty, true);
    }
    for (const auto& [listStyle, name] : kListStyles) {
        if (style == QLatin1StringView(name)) {
            format.setStyle(listStyle);
        }
    }
    format.setIndent(std::clamp(entry.value(QStringLiteral("indent")).toInt(1), 1, kMaxIndent));
    return format;
}

class Reader
{
public:
    explicit Reader(const QJsonArray& lists)
    {
        for (const auto& entry : lists) {
            m_listFormats.append(listFormatFor(entry.toObject()));
        }
        m_lists.fill(nullptr, m_listFormats.size());
    }

    // Writes `node` at `cursor`. `reuse`: the cursor's block is empty and
    // unclaimed (the document's first block, a fresh cell or the block Qt keeps
    // after a table), so the paragraph takes it instead of adding one.
    void paragraph(QTextCursor& cursor, bool& reuse, const QJsonObject& node)
    {
        const QTextBlockFormat format = blockFormatFor(node);
        if (reuse) {
            cursor.setBlockFormat(format);
            cursor.setBlockCharFormat(QTextCharFormat());
            reuse = false;
        } else {
            cursor.insertBlock(format, QTextCharFormat());
        }
        if (const QJsonValue list = node.value(QStringLiteral("list")); list.isDouble()) {
            joinList(cursor, list.toInt());
        }
        for (const auto& value : node.value(QStringLiteral("runs")).toArray()) {
            const QJsonObject run = value.toObject();
            if (run.contains(QStringLiteral("image"))) {
                QTextImageFormat image;
                image.setName(run.value(QStringLiteral("image")).toString());
                if (const double w = run.value(QStringLiteral("w")).toDouble(); w > 0) {
                    image.setWidth(w);
                }
                if (const double h = run.value(QStringLiteral("h")).toDouble(); h > 0) {
                    image.setHeight(h);
                }
                cursor.insertImage(image);
            } else {
                cursor.insertText(run.value(QStringLiteral("text")).toString(), charFormatFor(run));
            }
        }
    }

    std::optional<QString> table(QTextCursor& cursor, bool& reuse, const QJsonObject& node)
    {
        const int rows = node.value(QStringLiteral("rows")).toInt();
        const int columns = node.value(QStringLiteral("cols")).toInt();
        const QJsonArray cells = node.value(QStringLiteral("cells")).toArray();
        if (rows < 1 || columns < 1 || rows > kMaxTableSide || columns > kMaxTableSide ||
            cells.size() != static_cast<qsizetype>(rows) * columns) {
            return QStringLiteral("bad table");
        }
        const QTextTable* table = cursor.insertTable(rows, columns);
        for (int row = 0; row < rows; ++row) {
            for (int column = 0; column < columns; ++column) {
                QTextCursor cell = table->cellAt(row, column).firstCursorPosition();
                bool fresh = true;
                for (const auto& content :
                     cells.at((static_cast<qsizetype>(row) * columns) + column).toArray()) {
                    paragraph(cell, fresh, content.toObject());
                }
            }
        }
        // Qt keeps a block after every table: the next paragraph takes it.
        cursor.setPosition(table->lastPosition() + 1);
        reuse = true;
        return std::nullopt;
    }

private:
    void joinList(QTextCursor& cursor, int index)
    {
        if (index < 0 || index >= m_lists.size()) {
            return; // not one of the lists: a plain paragraph
        }
        if (QTextList* list = m_lists.at(index)) {
            list->add(cursor.block());
        } else {
            m_lists[index] = cursor.createList(m_listFormats.at(index));
        }
    }

    QList<QTextListFormat> m_listFormats;
    QList<QTextList*> m_lists;
};

} // namespace

QString NoteFormat::serialize(const QTextDocument& document)
{
    Writer writer;
    QJsonArray blocks;
    const QTextFrame* root = document.rootFrame();
    for (QTextFrame::iterator it = root->begin(); !it.atEnd(); ++it) {
        if (QTextFrame* frame = it.currentFrame()) {
            if (const auto* table = qobject_cast<const QTextTable*>(frame)) {
                blocks.append(writer.table(*table));
            }
            continue; // other frames aren't part of the format
        }
        blocks.append(writer.paragraph(it.currentBlock()));
    }
    QJsonObject body{{QStringLiteral("blocks"), blocks}};
    if (const QJsonArray lists = writer.lists(); !lists.isEmpty()) {
        body.insert(QStringLiteral("lists"), lists);
    }
    return QString::fromUtf8(QJsonDocument(body).toJson(QJsonDocument::Compact));
}

std::optional<QString> NoteFormat::deserialize(const QString& json, QTextDocument& document)
{
    document.clear();
    QJsonParseError error{};
    const QJsonDocument parsed = QJsonDocument::fromJson(json.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError) {
        return error.errorString();
    }
    if (!parsed.isObject() || !parsed.object().value(QStringLiteral("blocks")).isArray()) {
        return QStringLiteral("not a note body");
    }
    const QJsonObject body = parsed.object();
    if (const QJsonValue version = body.value(QStringLiteral("v"));
        !version.isUndefined() && version.toInt(-1) != kVersion) {
        return QStringLiteral("note format version %1 is not supported")
            .arg(version.toVariant().toString());
    }
    Reader reader(body.value(QStringLiteral("lists")).toArray());
    // Loading isn't an edit: without undo recording it's much faster, and a
    // freshly opened note has no history anyway.
    const bool undo = document.isUndoRedoEnabled();
    document.setUndoRedoEnabled(false);
    QTextCursor cursor(&document);
    cursor.beginEditBlock();
    bool reuse = true;
    for (const auto& value : body.value(QStringLiteral("blocks")).toArray()) {
        const QJsonObject node = value.toObject();
        if (const QJsonValue table = node.value(QStringLiteral("table")); table.isObject()) {
            if (std::optional<QString> failure = reader.table(cursor, reuse, table.toObject())) {
                cursor.endEditBlock();
                document.clear();
                document.setUndoRedoEnabled(undo);
                return failure;
            }
        } else {
            reader.paragraph(cursor, reuse, node);
        }
    }
    // Fonts aren't stored: headings get theirs from their style.
    StyleManager::styleHeadings(document);
    cursor.endEditBlock();
    document.setUndoRedoEnabled(undo);
    return std::nullopt;
}

void NoteFormat::load(const QString& body, const QString& plainText, QTextDocument& document)
{
    if (body.isEmpty()) {
        document.setPlainText(plainText);
        return;
    }
    if (deserialize(body, document)) {
        // Unreadable (corrupt, or from a newer app): show the text, which is
        // stored separately, rather than nothing.
        document.setPlainText(plainText);
    }
}

} // namespace notes::editor
