// Paragraph styles (task 3.2): StyleManager on a document. The window's side
// (shortcuts, the Format menu) is in tests/ui/tst_paragraphstyles.cpp.

#include "editor/NoteFormat.h"
#include "editor/StyleManager.h"

#include <QTest>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextList>

#include <array>

using notes::editor::NoteFormat;
using notes::editor::ParagraphStyle;
using notes::editor::StyleManager;

namespace {

constexpr std::array kAllStyles{ParagraphStyle::Title, ParagraphStyle::Heading,
                                ParagraphStyle::Subheading, ParagraphStyle::Body,
                                ParagraphStyle::Monospaced};

QTextBlock blockAt(const QTextDocument& document, int index)
{
    return document.findBlockByNumber(index);
}

// The char format in the middle of `block`'s text.
QTextCharFormat formatIn(const QTextBlock& block)
{
    QTextCursor cursor(block);
    cursor.setPosition(block.position() + (block.length() / 2));
    return cursor.charFormat();
}

// Applies `style` to paragraph `index`.
void applyTo(QTextDocument& document, int index, ParagraphStyle style)
{
    QTextCursor cursor(blockAt(document, index));
    StyleManager::apply(cursor, style);
}

} // namespace

class tst_StyleManager : public QObject
{
    Q_OBJECT

private slots:
    void eachStyleSetsLevelAndFont_data();
    void eachStyleSetsLevelAndFont();
    void styleChangeIsOneUndoStep();
    void appliesToEverySelectedParagraph();
    void leavingAStyleKeepsUserFormatting();
    void roundTripKeepsAllStyles();
    void headingBoldIsNotStored();
    void pastedHeadingLevelsCountAsSubheading();

    // Lists (task 3.5).
    void listStylesMakeLists();
    void listJoinsTheListAbove();
    void listChangesKindAsAWhole();
    void otherStyleLeavesTheList();
    void indentNestsAndRestartsNumbering();
    void outdentFromTopLeavesTheList();
    void listChangesAreOneUndoStep();
    void roundTripKeepsListKindsAndNesting();
};

// TC-EDT-002, on the document: level, weight, size and family per style.
void tst_StyleManager::eachStyleSetsLevelAndFont_data()
{
    QTest::addColumn<ParagraphStyle>("style");
    QTest::addColumn<int>("level");
    QTest::addColumn<qreal>("scale"); // 0: the default size
    QTest::addColumn<bool>("bold");
    QTest::addColumn<bool>("mono");
    QTest::newRow("title") << ParagraphStyle::Title << 1 << StyleManager::kTitleScale << true
                           << false;
    QTest::newRow("heading") << ParagraphStyle::Heading << 2 << StyleManager::kHeadingScale << true
                             << false;
    QTest::newRow("subheading") << ParagraphStyle::Subheading << 3 << StyleManager::kSubheadingScale
                                << true << false;
    QTest::newRow("body") << ParagraphStyle::Body << 0 << 0.0 << false << false;
    QTest::newRow("monospaced") << ParagraphStyle::Monospaced << 0 << 0.0 << false << true;
}

void tst_StyleManager::eachStyleSetsLevelAndFont()
{
    QFETCH(ParagraphStyle, style);
    QFETCH(int, level);
    QFETCH(qreal, scale);
    QFETCH(bool, bold);
    QFETCH(bool, mono);

    for (const ParagraphStyle from : kAllStyles) {
        QTextDocument document;
        QFont base(QStringLiteral("Inter"));
        base.setPointSizeF(11.0);
        document.setDefaultFont(base);
        document.setPlainText(QStringLiteral("Before\nStyled text\nAfter"));
        applyTo(document, 1, from);
        applyTo(document, 1, style);

        const QTextBlock block = blockAt(document, 1);
        const QString context = QStringLiteral("from style %1").arg(static_cast<int>(from));
        QCOMPARE(StyleManager::styleOf(block), style);
        QCOMPARE(block.blockFormat().headingLevel(), level);
        const QTextCharFormat format = formatIn(block);
        QVERIFY2((format.fontWeight() >= QFont::Bold) == bold, qPrintable(context));
        QVERIFY2(StyleManager::isMonospaced(format) == mono, qPrintable(context));
        if (scale > 0) {
            QCOMPARE(format.fontPointSize(), 11.0 * scale);
        } else {
            QVERIFY2(!format.hasProperty(QTextFormat::FontPointSize), qPrintable(context));
        }
        // An empty paragraph types in the style too.
        QCOMPARE(block.charFormat().fontWeight() >= QFont::Bold, bold);
        // The neighbours are untouched.
        QCOMPARE(StyleManager::styleOf(blockAt(document, 0)), ParagraphStyle::Body);
        QCOMPARE(StyleManager::styleOf(blockAt(document, 2)), ParagraphStyle::Body);
    }
}

// TC-EDT-005, on the document: one undo restores the previous style.
void tst_StyleManager::styleChangeIsOneUndoStep()
{
    QTextDocument document;
    document.setPlainText(QStringLiteral("One\nTwo\nThree"));
    applyTo(document, 0, ParagraphStyle::Heading);
    QTextCursor all(&document);
    all.select(QTextCursor::Document);
    StyleManager::apply(all, ParagraphStyle::Title);
    for (int i = 0; i < 3; ++i) {
        QCOMPARE(StyleManager::styleOf(blockAt(document, i)), ParagraphStyle::Title);
    }

    // One undo: the whole change, back to how it was.
    document.undo();
    QCOMPARE(StyleManager::styleOf(blockAt(document, 0)), ParagraphStyle::Heading);
    QCOMPARE(StyleManager::styleOf(blockAt(document, 1)), ParagraphStyle::Body);
    QCOMPARE(StyleManager::styleOf(blockAt(document, 2)), ParagraphStyle::Body);
    QCOMPARE(formatIn(blockAt(document, 0)).fontPointSize(),
             document.defaultFont().pointSizeF() * StyleManager::kHeadingScale);
    QVERIFY(!formatIn(blockAt(document, 1)).hasProperty(QTextFormat::FontPointSize));
    // A second one undoes the earlier Heading.
    document.undo();
    QCOMPARE(StyleManager::styleOf(blockAt(document, 0)), ParagraphStyle::Body);
    QVERIFY(!document.isUndoAvailable());
}

void tst_StyleManager::appliesToEverySelectedParagraph()
{
    QTextDocument document;
    document.setPlainText(QStringLiteral("One\nTwo\nThree\nFour"));
    QTextCursor cursor(blockAt(document, 1));
    cursor.setPosition(blockAt(document, 2).position() + 2, QTextCursor::KeepAnchor);
    StyleManager::apply(cursor, ParagraphStyle::Monospaced);
    QCOMPARE(StyleManager::styleOf(blockAt(document, 0)), ParagraphStyle::Body);
    QCOMPARE(StyleManager::styleOf(blockAt(document, 1)), ParagraphStyle::Monospaced);
    QCOMPARE(StyleManager::styleOf(blockAt(document, 2)), ParagraphStyle::Monospaced);
    QCOMPARE(StyleManager::styleOf(blockAt(document, 3)), ParagraphStyle::Body);
}

// A style takes back only what it put there: italic, underline and colors the
// user added survive Body -> Title -> Body.
void tst_StyleManager::leavingAStyleKeepsUserFormatting()
{
    QTextDocument document;
    QTextCursor cursor(&document);
    cursor.insertText(QStringLiteral("plain "));
    QTextCharFormat italic;
    italic.setFontItalic(true);
    italic.setForeground(QColor(0xD3, 0x2F, 0x2F));
    cursor.insertText(QStringLiteral("italic"), italic);

    for (const ParagraphStyle style : kAllStyles) {
        applyTo(document, 0, style);
        applyTo(document, 0, ParagraphStyle::Body);
        QTextCursor at(&document);
        at.movePosition(QTextCursor::End);
        QVERIFY(at.charFormat().fontItalic());
        QCOMPARE(at.charFormat().foreground().color(), QColor(0xD3, 0x2F, 0x2F));
        QVERIFY(!at.charFormat().hasProperty(QTextFormat::FontPointSize));
        QVERIFY(at.charFormat().fontWeight() < QFont::Bold);
        QVERIFY(!StyleManager::isMonospaced(at.charFormat()));
    }
}

// TC-EDT-004: all five styles survive saving and loading, fonts included.
void tst_StyleManager::roundTripKeepsAllStyles()
{
    QTextDocument document;
    QFont base(QStringLiteral("Inter"));
    base.setPointSizeF(11.0);
    document.setDefaultFont(base);
    document.setPlainText(
        QStringLiteral("A title\nA heading\nA subheading\nSome body\nlet x = 1;"));
    for (std::size_t i = 0; i < kAllStyles.size(); ++i) {
        applyTo(document, static_cast<int>(i), kAllStyles.at(i));
    }
    const QString saved = NoteFormat::serialize(document);

    QTextDocument loaded;
    loaded.setDefaultFont(base);
    QVERIFY(!NoteFormat::deserialize(saved, loaded).has_value());
    for (std::size_t i = 0; i < kAllStyles.size(); ++i) {
        const QTextBlock block = blockAt(loaded, static_cast<int>(i));
        QCOMPARE(StyleManager::styleOf(block), kAllStyles.at(i));
        QCOMPARE(formatIn(block).fontWeight() >= QFont::Bold,
                 StyleManager::headingLevel(kAllStyles.at(i)) > 0);
        QCOMPARE(formatIn(block).fontPointSize(),
                 formatIn(blockAt(document, static_cast<int>(i))).fontPointSize());
    }
    QCOMPARE(NoteFormat::serialize(loaded), saved);
}

// A heading's bold is its style's, not the user's: changing it to Body later
// gives plain text, and the stored body has no "b" for it.
void tst_StyleManager::headingBoldIsNotStored()
{
    QTextDocument document;
    document.setPlainText(QStringLiteral("Trip plan"));
    applyTo(document, 0, ParagraphStyle::Title);
    const QString saved = NoteFormat::serialize(document);
    QVERIFY2(!saved.contains(QStringLiteral(R"("b")")), qPrintable(saved));
    QVERIFY(!saved.contains(QStringLiteral("size")));

    QTextDocument loaded;
    QVERIFY(!NoteFormat::deserialize(saved, loaded).has_value());
    applyTo(loaded, 0, ParagraphStyle::Body);
    QVERIFY(formatIn(blockAt(loaded, 0)).fontWeight() < QFont::Bold);
}

// Markdown can bring levels 4 to 6; they show as Subheading, and keep their
// level when saved.
void tst_StyleManager::pastedHeadingLevelsCountAsSubheading()
{
    QTextDocument document;
    document.setMarkdown(QStringLiteral("#### Deep\n\ntext\n"));
    QCOMPARE(StyleManager::styleOf(blockAt(document, 0)), ParagraphStyle::Subheading);
    QTextDocument loaded;
    QVERIFY(!NoteFormat::deserialize(NoteFormat::serialize(document), loaded).has_value());
    QCOMPARE(blockAt(loaded, 0).blockFormat().headingLevel(), 4);
}

namespace {

// Paragraphs `from`..`to` selected.
QTextCursor paragraphs(QTextDocument& document, int from, int to)
{
    QTextCursor cursor(blockAt(document, from));
    cursor.setPosition(blockAt(document, to).position(), QTextCursor::KeepAnchor);
    return cursor;
}

int indentOf(const QTextDocument& document, int index)
{
    const QTextList* list = blockAt(document, index).textList();
    return list == nullptr ? 0 : list->format().indent();
}

QString markerOf(const QTextDocument& document, int index)
{
    const QTextBlock block = blockAt(document, index);
    return block.textList() == nullptr ? QString() : block.textList()->itemText(block);
}

} // namespace

void tst_StyleManager::listStylesMakeLists()
{
    for (const ParagraphStyle style :
         {ParagraphStyle::BulletedList, ParagraphStyle::DashedList, ParagraphStyle::NumberedList}) {
        QTextDocument document;
        document.setPlainText(QStringLiteral("Milk\nEggs\nBread"));
        applyTo(document, 0, ParagraphStyle::Title); // a heading becomes a Body item
        QTextCursor all = paragraphs(document, 0, 2);
        StyleManager::apply(all, style);
        for (int i = 0; i < 3; ++i) {
            QCOMPARE(StyleManager::styleOf(blockAt(document, i)), style);
            QCOMPARE(blockAt(document, i).blockFormat().headingLevel(), 0);
            QCOMPARE(blockAt(document, i).textList(), blockAt(document, 0).textList()); // one list
            QCOMPARE(indentOf(document, i), 1);
        }
        QVERIFY(formatIn(blockAt(document, 0)).fontWeight() < QFont::Bold);
        QCOMPARE(StyleManager::isList(style), true);
    }
    QTextDocument numbered;
    numbered.setPlainText(QStringLiteral("a\nb"));
    QTextCursor both = paragraphs(numbered, 0, 1);
    StyleManager::apply(both, ParagraphStyle::NumberedList);
    QCOMPARE(markerOf(numbered, 0), QStringLiteral("1."));
    QCOMPARE(markerOf(numbered, 1), QStringLiteral("2."));
}

// A new item right below a list of its kind continues it (and its numbering).
void tst_StyleManager::listJoinsTheListAbove()
{
    QTextDocument document;
    document.setPlainText(QStringLiteral("one\ntwo\nthree"));
    QTextCursor first = paragraphs(document, 0, 1);
    StyleManager::apply(first, ParagraphStyle::NumberedList);
    applyTo(document, 2, ParagraphStyle::NumberedList);
    QCOMPARE(blockAt(document, 2).textList(), blockAt(document, 0).textList());
    QCOMPARE(markerOf(document, 2), QStringLiteral("3."));

    // A different kind starts its own list.
    QTextDocument mixed;
    mixed.setPlainText(QStringLiteral("one\ntwo"));
    applyTo(mixed, 0, ParagraphStyle::NumberedList);
    applyTo(mixed, 1, ParagraphStyle::BulletedList);
    QVERIFY(blockAt(mixed, 1).textList() != blockAt(mixed, 0).textList());
}

void tst_StyleManager::listChangesKindAsAWhole()
{
    QTextDocument document;
    document.setPlainText(QStringLiteral("one\ntwo\nthree"));
    QTextCursor all = paragraphs(document, 0, 2);
    StyleManager::apply(all, ParagraphStyle::BulletedList);
    applyTo(document, 1, ParagraphStyle::DashedList);
    for (int i = 0; i < 3; ++i) {
        QCOMPARE(StyleManager::styleOf(blockAt(document, i)), ParagraphStyle::DashedList);
    }
}

void tst_StyleManager::otherStyleLeavesTheList()
{
    QTextDocument document;
    document.setPlainText(QStringLiteral("one\ntwo\nthree"));
    QTextCursor all = paragraphs(document, 0, 2);
    StyleManager::apply(all, ParagraphStyle::NumberedList);
    applyTo(document, 1, ParagraphStyle::Body);
    QCOMPARE(StyleManager::styleOf(blockAt(document, 1)), ParagraphStyle::Body);
    QCOMPARE(blockAt(document, 1).blockFormat().indent(), 0); // not pushed right
    QCOMPARE(markerOf(document, 2), QStringLiteral("2."));
    applyTo(document, 2, ParagraphStyle::Heading);
    QCOMPARE(StyleManager::styleOf(blockAt(document, 2)), ParagraphStyle::Heading);
    QVERIFY(blockAt(document, 2).textList() == nullptr);
}

// TC-EDT-013 on the document: a nested level is its own list, numbered from 1.
void tst_StyleManager::indentNestsAndRestartsNumbering()
{
    QTextDocument document;
    document.setPlainText(QStringLiteral("one\ntwo\nthree\nfour"));
    QTextCursor all = paragraphs(document, 0, 3);
    StyleManager::apply(all, ParagraphStyle::NumberedList);

    QTextCursor second = paragraphs(document, 1, 2); // two and three
    QVERIFY(StyleManager::indentList(second, 1));
    QCOMPARE(indentOf(document, 0), 1);
    QCOMPARE(indentOf(document, 1), 2);
    QCOMPARE(indentOf(document, 2), 2);
    QCOMPARE(markerOf(document, 1), QStringLiteral("1."));
    QCOMPARE(markerOf(document, 2), QStringLiteral("2."));
    QCOMPARE(markerOf(document, 3), QStringLiteral("2.")); // "four" continues level 1
    QCOMPARE(StyleManager::styleOf(blockAt(document, 1)), ParagraphStyle::NumberedList);

    // Back out: it rejoins level 1 where it was.
    QTextCursor three(blockAt(document, 2));
    QVERIFY(StyleManager::indentList(three, -1));
    QCOMPARE(indentOf(document, 2), 1);
    QCOMPARE(markerOf(document, 2), QStringLiteral("2."));
    QCOMPARE(markerOf(document, 3), QStringLiteral("3."));

    // No deeper than the limit; nothing to do outside a list.
    QTextCursor deep(blockAt(document, 0));
    for (int i = 0; i < 20; ++i) {
        StyleManager::indentList(deep, 1);
    }
    QCOMPARE(indentOf(document, 0), StyleManager::kMaxListIndent);
    QTextDocument plain;
    plain.setPlainText(QStringLiteral("text"));
    QTextCursor text(&plain);
    QVERIFY(!StyleManager::indentList(text, 1));
}

void tst_StyleManager::outdentFromTopLeavesTheList()
{
    QTextDocument document;
    document.setPlainText(QStringLiteral("one\ntwo"));
    QTextCursor all = paragraphs(document, 0, 1);
    StyleManager::apply(all, ParagraphStyle::BulletedList);
    QTextCursor first(blockAt(document, 0));
    QVERIFY(StyleManager::indentList(first, -1));
    QCOMPARE(StyleManager::styleOf(blockAt(document, 0)), ParagraphStyle::Body);
    QCOMPARE(blockAt(document, 0).blockFormat().indent(), 0);
    QCOMPARE(StyleManager::styleOf(blockAt(document, 1)), ParagraphStyle::BulletedList);

    QTextCursor second(blockAt(document, 1));
    StyleManager::removeFromList(second);
    QCOMPARE(StyleManager::styleOf(blockAt(document, 1)), ParagraphStyle::Body);
    QCOMPARE(document.toPlainText(), QStringLiteral("one\ntwo")); // text untouched
}

void tst_StyleManager::listChangesAreOneUndoStep()
{
    QTextDocument document;
    document.setPlainText(QStringLiteral("one\ntwo\nthree"));
    QTextCursor all = paragraphs(document, 0, 2);
    StyleManager::apply(all, ParagraphStyle::BulletedList);
    QTextCursor lower = paragraphs(document, 1, 2);
    StyleManager::indentList(lower, 1);

    document.undo(); // the indent
    QCOMPARE(indentOf(document, 1), 1);
    QCOMPARE(indentOf(document, 2), 1);
    document.undo(); // the list
    for (int i = 0; i < 3; ++i) {
        QCOMPARE(StyleManager::styleOf(blockAt(document, i)), ParagraphStyle::Body);
    }
    QVERIFY(!document.isUndoAvailable());
}

// TC-EDT-014
void tst_StyleManager::roundTripKeepsListKindsAndNesting()
{
    QTextDocument document;
    document.setPlainText(QStringLiteral("a\nb\nc\nd\ne\nf"));
    QTextCursor bullets = paragraphs(document, 0, 1);
    StyleManager::apply(bullets, ParagraphStyle::BulletedList);
    QTextCursor nested(blockAt(document, 1));
    StyleManager::indentList(nested, 1);
    QTextCursor dashes = paragraphs(document, 2, 3);
    StyleManager::apply(dashes, ParagraphStyle::DashedList);
    QTextCursor numbers = paragraphs(document, 4, 5);
    StyleManager::apply(numbers, ParagraphStyle::NumberedList);
    QTextCursor deeper(blockAt(document, 5));
    StyleManager::indentList(deeper, 2);

    const QString saved = NoteFormat::serialize(document);
    QVERIFY(saved.contains(QStringLiteral(R"("style":"dash")")));
    QTextDocument loaded;
    QVERIFY(!NoteFormat::deserialize(saved, loaded).has_value());
    for (int i = 0; i < 6; ++i) {
        QCOMPARE(StyleManager::styleOf(blockAt(loaded, i)),
                 StyleManager::styleOf(blockAt(document, i)));
        QCOMPARE(indentOf(loaded, i), indentOf(document, i));
        QCOMPARE(markerOf(loaded, i), markerOf(document, i));
    }
    QCOMPARE(indentOf(loaded, 5), 3);
    QCOMPARE(NoteFormat::serialize(loaded), saved);
}

QTEST_MAIN(tst_StyleManager)
#include "tst_stylemanager.moc"
