#pragma once

// A note with every editor feature, and a canonical text dump of any
// document, for the round-trip harness (TC-EDT-001) and every later editor
// test. Needs Qt Gui: tests that include it link notesapp_ui.

#include "editor/NoteFormat.h"
#include "editor/StyleManager.h"

#include <QColor>
#include <QImage>
#include <QString>
#include <QStringList>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFrame>
#include <QTextList>
#include <QTextTable>

#include <array>
#include <optional>
#include <utility>

namespace notes::test {

inline constexpr char kFixtureImage[] = "attachment://3f2a6c1e-0b5d-4c7e-9a41-6d2e8f1b7c90";

// Headings 1-3 (one collapsed), every inline format and a link, a monospaced
// line, bulleted / dashed / numbered / nested lists, a checklist
// with a checked and an open item, an image, a table directly after a list
// (the case Qt's HTML moves on every save), and text after the table.
inline void buildFixture(QTextDocument& document)
{
    document.clear();
    QTextCursor cursor(&document);
    bool first = true;
    const auto block = [&](int heading, const QString& text,
                           const QTextCharFormat& format = QTextCharFormat()) {
        QTextBlockFormat blockFormat;
        blockFormat.setHeadingLevel(heading);
        if (first) {
            cursor.setBlockFormat(blockFormat);
            first = false;
        } else {
            cursor.insertBlock(blockFormat, QTextCharFormat());
        }
        cursor.insertText(text, format);
    };
    QTextCharFormat bold;
    bold.setFontWeight(QFont::Bold);
    block(1, QStringLiteral("Trip plan"), bold);
    block(2, QStringLiteral("Packing"), bold);
    {
        QTextBlockFormat collapsed = cursor.blockFormat();
        collapsed.setProperty(editor::NoteFormat::kCollapsedProperty, true);
        cursor.setBlockFormat(collapsed);
    }
    block(3, QStringLiteral("Documents"), bold);

    block(0, QString());
    const auto run = [&](const QString& text, auto&& configure) {
        QTextCharFormat format;
        configure(format);
        cursor.insertText(text, format);
    };
    run(QStringLiteral("bold "), [](QTextCharFormat& f) { f.setFontWeight(QFont::Bold); });
    run(QStringLiteral("italic "), [](QTextCharFormat& f) { f.setFontItalic(true); });
    run(QStringLiteral("underline "), [](QTextCharFormat& f) { f.setFontUnderline(true); });
    run(QStringLiteral("strike "), [](QTextCharFormat& f) { f.setFontStrikeOut(true); });
    run(QStringLiteral("highlight "),
        [](QTextCharFormat& f) { f.setBackground(QColor(0xFF, 0xE0, 0x82)); });
    run(QStringLiteral("red "),
        [](QTextCharFormat& f) { f.setForeground(QColor(0xD3, 0x2F, 0x2F)); });
    run(QStringLiteral("link"), [](QTextCharFormat& f) {
        f.setAnchor(true);
        f.setAnchorHref(QStringLiteral("https://example.com/a?b=1&c=2"));
    });

    {
        QTextBlockFormat code;
        code.setNonBreakableLines(true);
        QTextCharFormat mono;
        mono.setFontFixedPitch(true);
        mono.setFontFamilies(QStringList{QStringLiteral("JetBrains Mono")});
        cursor.insertBlock(code, QTextCharFormat());
        cursor.insertText(QStringLiteral("let total = 3 * 4;"), mono);
    }

    const auto list = [&](QTextListFormat::Style style, int indent, const QStringList& items,
                          bool checklist = false) {
        QTextListFormat format;
        format.setStyle(style);
        format.setIndent(indent);
        QTextList* created = nullptr;
        for (qsizetype i = 0; i < items.size(); ++i) {
            QTextBlockFormat item;
            if (checklist && i == 0) {
                item.setProperty(editor::NoteFormat::kCheckedProperty, true);
            }
            cursor.insertBlock(item, QTextCharFormat());
            if (created == nullptr) {
                created = cursor.createList(format);
            } else {
                created->add(cursor.block());
            }
            cursor.insertText(items.at(i));
        }
    };
    list(QTextListFormat::ListDisc, 1, {QStringLiteral("Passport"), QStringLiteral("Charger")});
    list(QTextListFormat::ListStyleUndefined, 1, {QStringLiteral("Dashed item")});
    {
        // Qt has no dash style: our property marks the list (task 3.5).
        QTextListFormat dashed = cursor.currentList()->format();
        dashed.setProperty(editor::NoteFormat::kDashedListProperty, true);
        cursor.currentList()->setFormat(dashed);
    }
    list(QTextListFormat::ListDecimal, 1, {QStringLiteral("Book hotel"), QStringLiteral("Pack")});
    list(QTextListFormat::ListDecimal, 2, {QStringLiteral("Nested step")});

    cursor.insertBlock(QTextBlockFormat(), QTextCharFormat());
    QTextImageFormat image;
    image.setName(QString::fromLatin1(kFixtureImage));
    image.setWidth(240);
    image.setHeight(120);
    cursor.insertImage(image);

    list(QTextListFormat::ListStyleUndefined, 1,
         {QStringLiteral("Tickets"), QStringLiteral("Visa")},
         /*checklist=*/true);
    {
        // A checklist is our property too, and so is a checked item (task 3.6).
        QTextListFormat checklist = cursor.currentList()->format();
        checklist.setProperty(editor::NoteFormat::kChecklistProperty, true);
        cursor.currentList()->setFormat(checklist);
    }

    cursor.insertBlock(QTextBlockFormat(), QTextCharFormat());
    QTextTable* table = cursor.insertTable(2, 2);
    const QStringList cells{QStringLiteral("Day"), QStringLiteral("City"), QStringLiteral("1"),
                            QStringLiteral("Lisbon")};
    for (int i = 0; i < 4; ++i) {
        table->cellAt(i / 2, i % 2).firstCursorPosition().insertText(cells.at(i));
    }
    cursor.setPosition(table->lastPosition() + 1);
    cursor.insertText(QStringLiteral("Have a good trip."));
}

// One line per block, with everything the note format keeps: text, heading,
// list (style, indent, which list), checklist marker, indent, alignment,
// no-break, collapsed, and per run the inline formats, links and images.
// Two documents that read the same to a user dump the same.
inline QStringList dumpDocument(const QTextDocument& document)
{
    QStringList lines;
    QList<const QTextList*> lists;
    for (QTextBlock block = document.begin(); block.isValid(); block = block.next()) {
        const QTextBlockFormat format = block.blockFormat();
        QString line = QStringLiteral("\"%1\"").arg(block.text());
        if (format.headingLevel() > 0) {
            line += QStringLiteral(" h%1").arg(format.headingLevel());
        }
        if (const QTextList* list = block.textList()) {
            if (!lists.contains(list)) {
                lists.append(list);
            }
            QString kind; // our own list kinds, which have no Qt style
            if (list->format().boolProperty(editor::NoteFormat::kDashedListProperty)) {
                kind = QStringLiteral(", dash");
            } else if (list->format().boolProperty(editor::NoteFormat::kChecklistProperty)) {
                kind = QStringLiteral(", check");
            }
            line += QStringLiteral(" list#%1(style %2, indent %3%4)")
                        .arg(lists.indexOf(list))
                        .arg(static_cast<int>(list->format().style()))
                        .arg(list->format().indent())
                        .arg(kind);
        }
        if (editor::StyleManager::isChecklistItem(block)) {
            line += editor::StyleManager::isChecked(block) ? QStringLiteral(" [x]")
                                                           : QStringLiteral(" [ ]");
        }
        if (format.indent() > 0) {
            line += QStringLiteral(" indent %1").arg(format.indent());
        }
        if (const Qt::Alignment a = format.alignment() & Qt::AlignHorizontal_Mask;
            a != Qt::AlignLeft && a != 0) {
            line += QStringLiteral(" align %1").arg(static_cast<int>(a));
        }
        if (format.nonBreakableLines()) {
            line += QStringLiteral(" nobreak");
        }
        if (format.boolProperty(editor::NoteFormat::kCollapsedProperty)) {
            line += QStringLiteral(" collapsed");
        }
        if (format.hasProperty(QTextFormat::BlockCodeFence)) {
            line += QStringLiteral(" code(%1)")
                        .arg(format.stringProperty(QTextFormat::BlockCodeLanguage));
        }
        if (const auto* table =
                qobject_cast<const QTextTable*>(QTextCursor(block).currentTable())) {
            const QTextTableCell cell = table->cellAt(block.position());
            line += QStringLiteral(" cell(%1,%2 of %3x%4)")
                        .arg(cell.row())
                        .arg(cell.column())
                        .arg(table->rows())
                        .arg(table->columns());
        }
        for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
            const QTextCharFormat c = it.fragment().charFormat();
            QStringList props;
            if (c.isImageFormat()) {
                const QTextImageFormat image = c.toImageFormat();
                props << QStringLiteral("image %1 %2x%3")
                             .arg(image.name())
                             .arg(image.width())
                             .arg(image.height());
            } else {
                const std::array<std::pair<bool, QString>, 8> flags{{
                    {c.fontWeight() >= QFont::Bold, QStringLiteral("b")},
                    {c.fontItalic(), QStringLiteral("i")},
                    {c.fontUnderline(), QStringLiteral("u")},
                    {c.fontStrikeOut(), QStringLiteral("s")},
                    {c.fontFixedPitch() ||
                         c.fontFamilies().toStringList().contains(QStringLiteral("monospace")),
                     QStringLiteral("mono")},
                    {c.background().style() != Qt::NoBrush,
                     QStringLiteral("bg ") + c.background().color().name()},
                    {c.foreground().style() != Qt::NoBrush,
                     QStringLiteral("fg ") + c.foreground().color().name()},
                    {c.isAnchor(), QStringLiteral("href ") + c.anchorHref()},
                }};
                for (const auto& [set, name] : flags) {
                    if (set) {
                        props << name;
                    }
                }
            }
            if (!props.isEmpty()) {
                line += QStringLiteral(" {%1: %2}")
                            .arg(it.fragment().text(), props.join(QLatin1Char(',')));
            }
        }
        lines.append(line);
    }
    return lines;
}

} // namespace notes::test
