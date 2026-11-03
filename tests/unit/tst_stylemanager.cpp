// Paragraph styles (task 3.2): StyleManager on a document. The window's side
// (shortcuts, the Format menu) is in tests/ui/tst_paragraphstyles.cpp.

#include "editor/NoteFormat.h"
#include "editor/StyleManager.h"

#include <QTest>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>

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

QTEST_MAIN(tst_StyleManager)
#include "tst_stylemanager.moc"
