// The note body format (task 3.1, ADR 0006): save and load time and stored
// size of our JSON next to Qt HTML and Markdown, for notes of about 5 KB,
// 100 KB and 1 MB of text: realistic prose, and a dense stress case built from
// the editor fixture. Opening a note must take < 50 ms (Plan §10) for prose up
// to 100 KB and the dense 5 KB note. Release flags only (preset `bench`).

#include "EditorFixture.h"
#include "editor/NoteFormat.h"

#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>
#include <QTextDocument>

#include <algorithm>
#include <functional>
#include <vector>

using notes::editor::NoteFormat;

namespace {

// The fixture's JSON repeated until the note has about `textBytes` of text.
QString bodyOfSize(qsizetype textBytes)
{
    QTextDocument fixture;
    notes::test::buildFixture(fixture);
    const QJsonObject one =
        QJsonDocument::fromJson(NoteFormat::serialize(fixture).toUtf8()).object();
    const QJsonArray blocks = one.value(QStringLiteral("blocks")).toArray();
    const QJsonArray lists = one.value(QStringLiteral("lists")).toArray();
    const qsizetype perCopy = fixture.toPlainText().toUtf8().size();

    QJsonArray allBlocks;
    QJsonArray allLists;
    for (qsizetype copy = 0; copy * perCopy < textBytes; ++copy) {
        const auto offset = static_cast<int>(allLists.size());
        for (const auto& list : lists) {
            allLists.append(list);
        }
        for (const auto& value : blocks) {
            QJsonObject block = value.toObject();
            if (block.contains(QStringLiteral("list"))) {
                block.insert(QStringLiteral("list"),
                             block.value(QStringLiteral("list")).toInt() + offset);
            }
            allBlocks.append(block);
        }
    }
    return QString::fromUtf8(QJsonDocument(QJsonObject{{QStringLiteral("blocks"), allBlocks},
                                                       {QStringLiteral("lists"), allLists}})
                                 .toJson(QJsonDocument::Compact));
}

// A realistic note of about `textBytes`: a heading every few paragraphs,
// paragraphs of ~150 characters with a bold and an italic phrase, now and
// then a short list.
QString proseOfSize(qsizetype textBytes)
{
    QTextDocument document;
    QTextCursor cursor(&document);
    QTextCharFormat bold;
    bold.setFontWeight(QFont::Bold);
    QTextCharFormat italic;
    italic.setFontItalic(true);
    const QString sentence = QStringLiteral("The quick brown fox jumps over the lazy dog near the "
                                            "river bank, while the evening light fades slowly. ");
    for (int paragraph = 0; document.characterCount() < textBytes; ++paragraph) {
        QTextBlockFormat format;
        if (paragraph % 6 == 0) {
            format.setHeadingLevel(2);
        }
        if (paragraph > 0) {
            cursor.insertBlock(format, QTextCharFormat());
        } else {
            cursor.setBlockFormat(format);
        }
        if (paragraph % 6 == 0) {
            cursor.insertText(QStringLiteral("Section %1").arg(paragraph / 6), bold);
            continue;
        }
        cursor.insertText(sentence.left(40));
        cursor.insertText(QStringLiteral("important point"), bold);
        cursor.insertText(sentence.mid(40, 50));
        cursor.insertText(QStringLiteral("aside"), italic);
        cursor.insertText(sentence.mid(90));
        if (paragraph % 9 == 4) {
            QTextListFormat list;
            list.setStyle(QTextListFormat::ListDisc);
            cursor.insertBlock();
            cursor.createList(list);
            cursor.insertText(QStringLiteral("first item of a short list"));
            cursor.insertBlock();
            cursor.insertText(QStringLiteral("second item"));
        }
    }
    return NoteFormat::serialize(document);
}

// Median of `runs` timings of `work`, in milliseconds.
double medianMs(int runs, const std::function<void()>& work)
{
    std::vector<double> times;
    for (int i = 0; i < runs; ++i) {
        QElapsedTimer timer;
        timer.start();
        work();
        times.push_back(static_cast<double>(timer.nsecsElapsed()) / 1e6);
    }
    std::ranges::sort(times);
    return times.at(times.size() / 2);
}

} // namespace

class bench_NoteFormat : public QObject
{
    Q_OBJECT

private slots:
    void compareFormats_data();
    void compareFormats();
};

// "prose": realistic notes, held to the 50 ms budget. "dense": the editor
// fixture repeated, ~10 characters per formatted block, a stress case.
void bench_NoteFormat::compareFormats_data()
{
    QTest::addColumn<QString>("body");
    QTest::addColumn<int>("runs");
    QTest::addColumn<bool>("budgeted");
    QTest::newRow("prose 5 KB") << proseOfSize(qsizetype{5} * 1024) << 21 << true;
    QTest::newRow("prose 100 KB") << proseOfSize(qsizetype{100} * 1024) << 11 << true;
    QTest::newRow("prose 1 MB") << proseOfSize(qsizetype{1024} * 1024) << 3 << false;
    QTest::newRow("dense 5 KB") << bodyOfSize(qsizetype{5} * 1024) << 21 << true;
    QTest::newRow("dense 100 KB") << bodyOfSize(qsizetype{100} * 1024) << 11 << false;
    QTest::newRow("dense 1 MB") << bodyOfSize(qsizetype{1024} * 1024) << 3 << false;
}

void bench_NoteFormat::compareFormats()
{
    QFETCH(QString, body);
    QFETCH(int, runs);
    QFETCH(bool, budgeted);
    QTextDocument note;
    QVERIFY(!NoteFormat::deserialize(body, note).has_value());

    const QString json = NoteFormat::serialize(note);
    const QString html = note.toHtml();
    const QString markdown = note.toMarkdown();

    struct Row
    {
        const char* name;
        qsizetype bytes;
        double saveMs;
        double loadMs;
    };
    const std::vector<Row> rows{
        {"JSON (ours)", json.toUtf8().size(),
         medianMs(runs, [&] { (void)NoteFormat::serialize(note); }),
         medianMs(runs,
                  [&] {
                      QTextDocument document;
                      (void)NoteFormat::deserialize(json, document);
                  })},
        {"Qt HTML", html.toUtf8().size(), medianMs(runs, [&] { (void)note.toHtml(); }),
         medianMs(runs,
                  [&] {
                      QTextDocument document;
                      document.setHtml(html);
                  })},
        {"Markdown", markdown.toUtf8().size(), medianMs(runs, [&] { (void)note.toMarkdown(); }),
         medianMs(runs,
                  [&] {
                      QTextDocument document;
                      document.setMarkdown(markdown);
                  })},
    };
    qInfo("%s note (%lld bytes of text, %d blocks):", QTest::currentDataTag(),
          static_cast<long long>(note.toPlainText().toUtf8().size()), note.blockCount());
    for (const Row& row : rows) {
        qInfo("  %-12s %9lld bytes  save %8.2f ms  load %8.2f ms", row.name,
              static_cast<long long>(row.bytes), row.saveMs, row.loadMs);
    }
    if (budgeted) {
        QVERIFY2(rows.front().loadMs < 50.0, "opening a note must take < 50 ms (Plan §10)");
    }
}

QTEST_MAIN(bench_NoteFormat)
#include "bench_noteformat.moc"
