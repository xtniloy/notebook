// The note body format (task 3.1, ADR 0006): TC-EDT-001 round-trip harness,
// plus what the format keeps that Qt's HTML loses, bad input, and loading
// bodies of every format_version.

#include "EditorFixture.h"
#include "editor/NoteFormat.h"

#include <QTest>
#include <QTextDocument>

using notes::editor::NoteFormat;
using notes::test::buildFixture;
using notes::test::dumpDocument;

namespace {

// Deserializes `json` into a new document, failing the test on an error.
std::unique_ptr<QTextDocument> load(const QString& json)
{
    auto document = std::make_unique<QTextDocument>();
    if (const std::optional<QString> failure = NoteFormat::deserialize(json, *document)) {
        qWarning() << "deserialize failed:" << *failure;
        return nullptr;
    }
    return document;
}

} // namespace

class tst_NoteFormat : public QObject
{
    Q_OBJECT

private slots:
    void roundTripIsExact();
    void roundTripKeepsEveryBlock();
    void keepsWhatQtHtmlLoses();
    void emptyDocument();
    void outputIsCompactAndStable();
    void rejectsBadInput_data();
    void rejectsBadInput();
    void clampsOutOfRangeValues();
    void ignoresUnknownKeys();
    void loadsEveryFormatVersion();
    void aiAnswerFromMarkdownSurvives();
};

// TC-EDT-001: serialize(deserialize(serialize(doc))) == serialize(doc).
void tst_NoteFormat::roundTripIsExact()
{
    QTextDocument fixture;
    buildFixture(fixture);
    const QString first = NoteFormat::serialize(fixture);
    const auto reloaded = load(first);
    QVERIFY(reloaded);
    QCOMPARE(NoteFormat::serialize(*reloaded), first);
    // And it stays that way, however often a note is saved.
    QString json = first;
    for (int round = 0; round < 5; ++round) {
        const auto again = load(json);
        QVERIFY(again);
        json = NoteFormat::serialize(*again);
    }
    QCOMPARE(json, first);
}

// Block by block, the reloaded note reads like the original.
void tst_NoteFormat::roundTripKeepsEveryBlock()
{
    QTextDocument fixture;
    buildFixture(fixture);
    const auto reloaded = load(NoteFormat::serialize(fixture));
    QVERIFY(reloaded);
    const QStringList expected = dumpDocument(fixture);
    const QStringList actual = dumpDocument(*reloaded);
    for (qsizetype i = 0; i < std::max(expected.size(), actual.size()); ++i) {
        QCOMPARE(actual.value(i), expected.value(i));
    }
}

// The spike's findings (ADR 0006): Qt's HTML loses these; the format keeps them.
void tst_NoteFormat::keepsWhatQtHtmlLoses()
{
    QTextDocument fixture;
    buildFixture(fixture);
    const QString json = NoteFormat::serialize(fixture);
    const auto reloaded = load(json);
    QVERIFY(reloaded);
    const QString dump = dumpDocument(*reloaded).join(QLatin1Char('\n'));
    QVERIFY(dump.contains(QStringLiteral("\"Packing\" h2 collapsed")));
    QVERIFY(dump.contains(QStringLiteral("\"let total = 3 * 4;\" nobreak")));
    QVERIFY(dump.contains(QStringLiteral("\"Tickets\" list#")) &&
            dump.contains(QStringLiteral("[x]")));
    QVERIFY(dump.contains(QStringLiteral("\"Visa\"")) && dump.contains(QStringLiteral("[ ]")));
    QVERIFY(dump.contains(
        QStringLiteral("image %1 240x120").arg(QLatin1StringView(notes::test::kFixtureImage))));
    // No size or font noise is stored: fonts come from the styles (task 3.2).
    QVERIFY(!json.contains(QStringLiteral("pt")) && !json.contains(QStringLiteral("font")));
}

void tst_NoteFormat::emptyDocument()
{
    QTextDocument empty;
    const QString json = NoteFormat::serialize(empty);
    QCOMPARE(json, QStringLiteral(R"({"blocks":[{}]})"));
    const auto reloaded = load(json);
    QVERIFY(reloaded);
    QCOMPARE(reloaded->blockCount(), 1);
    QVERIFY(reloaded->toPlainText().isEmpty());
}

void tst_NoteFormat::outputIsCompactAndStable()
{
    QTextDocument document;
    document.setPlainText(QStringLiteral("Groceries\nMilk"));
    QCOMPARE(NoteFormat::serialize(document),
             QStringLiteral(
                 R"({"blocks":[{"runs":[{"text":"Groceries"}]},{"runs":[{"text":"Milk"}]}]})"));
}

void tst_NoteFormat::rejectsBadInput_data()
{
    QTest::addColumn<QString>("json");
    QTest::newRow("not JSON") << QStringLiteral("{oops");
    QTest::newRow("HTML") << QStringLiteral("<p>hello</p>");
    QTest::newRow("array") << QStringLiteral("[1,2]");
    QTest::newRow("no blocks") << QStringLiteral(R"({"lists":[]})");
    QTest::newRow("blocks not an array") << QStringLiteral(R"({"blocks":{}})");
    QTest::newRow("table without cells")
        << QStringLiteral(R"({"blocks":[{"table":{"rows":2,"cols":2}}]})");
    QTest::newRow("huge table") << QStringLiteral(
        R"({"blocks":[{"table":{"rows":100000,"cols":100000,"cells":[]}}]})");
    QTest::newRow("negative table")
        << QStringLiteral(R"({"blocks":[{"table":{"rows":-1,"cols":2,"cells":[]}}]})");
}

// Stored data may be corrupt or from a newer app: fail cleanly, never crash.
void tst_NoteFormat::rejectsBadInput()
{
    QFETCH(QString, json);
    QTextDocument document;
    document.setPlainText(QStringLiteral("previous"));
    QVERIFY(NoteFormat::deserialize(json, document).has_value());
    QVERIFY(document.toPlainText().isEmpty());
}

void tst_NoteFormat::clampsOutOfRangeValues()
{
    const auto document = load(QStringLiteral(
        R"({"lists":[{"style":"nonsense","indent":999}],)"
        R"("blocks":[{"h":42,"indent":-5,"list":7,"runs":[{"text":"x","bg":"not a color"}]},)"
        R"({"list":0,"runs":[{"text":"y"}]}]})"));
    QVERIFY(document);
    const QTextBlock first = document->begin();
    QCOMPARE(first.blockFormat().headingLevel(), 6);
    QCOMPARE(first.blockFormat().indent(), 0);
    QVERIFY(first.textList() == nullptr); // list 7 doesn't exist
    QCOMPARE(first.begin().fragment().charFormat().background().style(), Qt::NoBrush);
    const QTextList* list = first.next().textList();
    QVERIFY(list != nullptr);
    QCOMPARE(list->format().style(), QTextListFormat::ListDisc);
    QCOMPARE(list->format().indent(), 16);
}

// A newer version may add keys: older readers ignore them.
void tst_NoteFormat::ignoresUnknownKeys()
{
    const auto document = load(QStringLiteral(
        R"({"future":true,"blocks":[{"sparkle":1,"runs":[{"text":"hi","glow":"x"}]}]})"));
    QVERIFY(document);
    QCOMPARE(document->toPlainText(), QStringLiteral("hi"));
}

void tst_NoteFormat::loadsEveryFormatVersion()
{
    QTextDocument document;
    // Phase 2 bodies: Qt HTML from the plain editor.
    QTextDocument legacy;
    legacy.setPlainText(QStringLiteral("Old note\nfrom Phase 2"));
    NoteFormat::load(legacy.toHtml(), NoteFormat::kLegacyHtml, {}, document);
    QCOMPARE(document.toPlainText(), QStringLiteral("Old note\nfrom Phase 2"));
    // This format.
    QTextDocument fixture;
    buildFixture(fixture);
    NoteFormat::load(NoteFormat::serialize(fixture), NoteFormat::kJson, {}, document);
    QCOMPARE(dumpDocument(document), dumpDocument(fixture));
    // No body yet: the plain text.
    NoteFormat::load({}, NoteFormat::kJson, QStringLiteral("Just text"), document);
    QCOMPARE(document.toPlainText(), QStringLiteral("Just text"));
    // Unreadable body: the plain text, rather than nothing.
    NoteFormat::load(QStringLiteral("{corrupt"), NoteFormat::kJson, QStringLiteral("Saved text"),
                     document);
    QCOMPARE(document.toPlainText(), QStringLiteral("Saved text"));
}

// Task 3.1's Markdown check: an AI chat answer, pasted as Markdown (task 3.8
// will do this on paste), keeps its structure through the note format.
void tst_NoteFormat::aiAnswerFromMarkdownSurvives()
{
    QTextDocument answer;
    answer.setMarkdown(
        QStringLiteral("## Packing list\n\n"
                       "Here is a **short** list for *three* days:\n\n"
                       "- Passport\n- Charger\n  1. USB-C cable\n\n"
                       "- [x] Book hotel\n- [ ] Buy tickets\n\n"
                       "```python\nprint(\"have a good trip\")\n```\n\n"
                       "| Day | City |\n|-----|------|\n| 1 | Lisbon |\n| 2 | Porto |\n\n"
                       "See [the guide](https://example.com/guide)."));
    const QString json = NoteFormat::serialize(answer);
    const auto reloaded = load(json);
    QVERIFY(reloaded);
    QCOMPARE(dumpDocument(*reloaded), dumpDocument(answer));
    QCOMPARE(NoteFormat::serialize(*reloaded), json);

    const QString dump = dumpDocument(*reloaded).join(QLatin1Char('\n'));
    QVERIFY(dump.contains(QStringLiteral("\"Packing list\" h2")));
    QVERIFY(dump.contains(QStringLiteral("{short: b}")));
    QVERIFY(dump.contains(QStringLiteral("{three: i}")));
    QVERIFY(dump.contains(QStringLiteral("\"Book hotel\"")) &&
            dump.contains(QStringLiteral("[x]")));
    QVERIFY(dump.contains(QStringLiteral("cell(1,1 of 3x2)")));
    QVERIFY(dump.contains(QStringLiteral("href https://example.com/guide")));
    // The code block stays a monospaced code block, with its language.
    QVERIFY2(
        dump.contains(QStringLiteral("nobreak code(python) {print(\"have a good trip\"): mono}")),
        qPrintable(dump));
}

QTEST_MAIN(tst_NoteFormat)
#include "tst_noteformat.moc"
