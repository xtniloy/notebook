// Cleaning pasted HTML and Markdown (task 3.8): TC-EDT-028 scripts, styles
// and event attributes are removed, TC-EDT-053 prose isn't taken for
// Markdown; and what is kept: headings, lists, checklists, code, inline
// formats and safe links.

#include "editor/NoteFormat.h"
#include "editor/PasteCleaner.h"
#include "editor/StyleManager.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextList>

using notes::editor::NoteFormat;
using notes::editor::ParagraphStyle;
using notes::editor::PasteCleaner;
using notes::editor::StyleManager;

namespace {

QJsonArray blocksOf(const QString& body)
{
    return QJsonDocument::fromJson(body.toUtf8())
        .object()
        .value(QStringLiteral("blocks"))
        .toArray();
}

// "Style: text" per paragraph, as the editor would show the body.
QStringList shown(const QString& body)
{
    QTextDocument document;
    if (NoteFormat::deserialize(body, document)) {
        return {QStringLiteral("unreadable")};
    }
    QStringList lines;
    for (QTextBlock block = document.begin(); block.isValid(); block = block.next()) {
        QString style;
        switch (StyleManager::styleOf(block)) {
        case ParagraphStyle::Title:
            style = QStringLiteral("Title");
            break;
        case ParagraphStyle::Heading:
            style = QStringLiteral("Heading");
            break;
        case ParagraphStyle::Subheading:
            style = QStringLiteral("Subheading");
            break;
        case ParagraphStyle::Body:
            style = QStringLiteral("Body");
            break;
        case ParagraphStyle::Monospaced:
            style = QStringLiteral("Mono");
            break;
        case ParagraphStyle::BulletedList:
            style = QStringLiteral("Bullet");
            break;
        case ParagraphStyle::DashedList:
            style = QStringLiteral("Dash");
            break;
        case ParagraphStyle::NumberedList:
            style = QStringLiteral("Number");
            break;
        case ParagraphStyle::Checklist:
            style = StyleManager::isChecked(block) ? QStringLiteral("[x]") : QStringLiteral("[ ]");
            break;
        }
        if (const QTextList* list = block.textList();
            list != nullptr && list->format().indent() > 1) {
            style += QStringLiteral("+%1").arg(list->format().indent() - 1);
        }
        lines.append(style + QStringLiteral(": ") + block.text());
    }
    return lines;
}

// Every text run's formats, as "text[b,i]".
QStringList runsOf(const QString& body)
{
    QStringList runs;
    for (const auto& block : blocksOf(body)) {
        for (const auto& value : block.toObject().value(QStringLiteral("runs")).toArray()) {
            const QJsonObject run = value.toObject();
            QStringList keys = run.keys();
            keys.removeAll(QStringLiteral("text"));
            if (run.contains(QStringLiteral("href"))) {
                keys.replace(keys.indexOf(QStringLiteral("href")),
                             QStringLiteral("href=") +
                                 run.value(QStringLiteral("href")).toString());
            }
            runs.append(run.value(QStringLiteral("text")).toString() + QLatin1Char('[') +
                        keys.join(QLatin1Char(',')) + QLatin1Char(']'));
        }
    }
    return runs;
}

} // namespace

class tst_PasteCleaner : public QObject
{
    Q_OBJECT

private slots:
    void scriptsStylesAndEventsAreRemoved();
    void looksAreStrippedFormatsKept();
    void onlySafeLinksStay();
    void imagesAreDropped();
    void headingsBecomeOurs();
    void listsBecomeOurs();
    void emptyEdgesAreTrimmed();
    void nothingLeftIsEmpty();
    void markdownBecomesStyles();
    void markdownTaskListsBecomeChecklists();
    void markdownCodeAndTables();
    void detectsMarkdown_data();
    void detectsMarkdown();
};

// TC-EDT-028
void tst_PasteCleaner::scriptsStylesAndEventsAreRemoved()
{
    const QString body = PasteCleaner::fromHtml(
        QStringLiteral("<html><head><title>Page</title><style>p { color: red }</style>"
                       "<script>alert('head')</script></head><body>"
                       "<p onclick=\"steal()\" onmouseover='steal()'>Hello</p>"
                       "<script type=\"text/javascript\">document.cookie = 'x';</script>"
                       "<SCRIPT>alert(1)</SCRIPT><noscript>Enable JS</noscript>"
                       "<iframe src=\"https://ads.example\">frame</iframe>"
                       "<p>World</p></body></html>"));
    QCOMPARE(shown(body),
             (QStringList{QStringLiteral("Body: Hello"), QStringLiteral("Body: World")}));
    for (const char* gone : {"alert", "cookie", "steal", "onclick", "color", "Page", "Enable JS",
                             "frame", "ads.example"}) {
        QVERIFY2(!body.contains(QLatin1StringView(gone)), gone);
    }
}

void tst_PasteCleaner::looksAreStrippedFormatsKept()
{
    const QString body = PasteCleaner::fromHtml(
        QStringLiteral("<p style=\"font-family: 'Comic Sans MS'; font-size: 30px; color: #ff0000;"
                       " text-align: center; margin-left: 80px\">"
                       "<b>bold</b> <i>italic</i> <u>under</u> <s>struck</s> <code>code</code>"
                       " <span style=\"background-color: yellow; color: blue\">marked</span>"
                       "<font face=\"Times\" size=\"7\" color=\"green\">old</font></p>"));
    QCOMPARE(
        runsOf(body),
        (QStringList{QStringLiteral("bold[b]"), QStringLiteral(" []"), QStringLiteral("italic[i]"),
                     QStringLiteral(" []"), QStringLiteral("under[u]"), QStringLiteral(" []"),
                     QStringLiteral("struck[s]"), QStringLiteral(" []"),
                     QStringLiteral("code[mono]"), QStringLiteral(" markedold[]")}));
    const QJsonObject paragraph = blocksOf(body).first().toObject();
    QCOMPARE(paragraph.keys(), QStringList{QStringLiteral("runs")}); // no align, indent, ...
    // Non-breaking spaces from web pages become plain ones.
    QCOMPARE(runsOf(PasteCleaner::fromHtml(QStringLiteral("<p>a&nbsp;b</p>"))),
             QStringList{QStringLiteral("a b[]")});
}

void tst_PasteCleaner::onlySafeLinksStay()
{
    const QString body = PasteCleaner::fromHtml(QStringLiteral(
        "<p><a href=\"https://example.com/a\">web</a> <a href=\"mailto:me@example.com\">mail</a>"
        " <a href=\"javascript:alert(1)\">script</a> <a href=\"file:///etc/passwd\">file</a>"
        " <a href=\"data:text/html,x\">data</a></p>"));
    QCOMPARE(runsOf(body),
             (QStringList{QStringLiteral("web[href=https://example.com/a]"), QStringLiteral(" []"),
                          QStringLiteral("mail[href=mailto:me@example.com]"),
                          QStringLiteral(" script file data[]")}));
}

void tst_PasteCleaner::imagesAreDropped()
{
    const QString body = PasteCleaner::fromHtml(QStringLiteral(
        "<p>Before<img src=\"http://127.0.0.1:1/pixel.png\" width=\"1\" height=\"1\">"
        "<img src=\"attachment://3f2a6c1e-0b5d-4c7e-9a41-6d2e8f1b7c90\">After</p>"
        "<p><img src=\"https://example.com/big.png\"></p>"));
    QVERIFY(!body.contains(QStringLiteral("image")));
    QVERIFY(!body.contains(QStringLiteral("127.0.0.1")));
    QCOMPARE(shown(body), QStringList{QStringLiteral("Body: BeforeAfter")});
}

void tst_PasteCleaner::headingsBecomeOurs()
{
    const QString body = PasteCleaner::fromHtml(
        QStringLiteral("<h1>One</h1><h2>Two</h2><h3>Three</h3><h6>Six</h6><p>Text</p>"));
    QCOMPARE(shown(body),
             (QStringList{QStringLiteral("Heading: One"), QStringLiteral("Heading: Two"),
                          QStringLiteral("Subheading: Three"), QStringLiteral("Subheading: Six"),
                          QStringLiteral("Body: Text")}));
}

void tst_PasteCleaner::listsBecomeOurs()
{
    const QString body = PasteCleaner::fromHtml(QStringLiteral(
        "<ul><li>a</li><li>b<ul style=\"list-style-type: square\"><li>c</li></ul></li></ul>"
        "<ol type=\"i\"><li>one</li><li>two</li></ol>"));
    QCOMPARE(shown(body), (QStringList{QStringLiteral("Bullet: a"), QStringLiteral("Bullet: b"),
                                       QStringLiteral("Bullet+1: c"), QStringLiteral("Number: one"),
                                       QStringLiteral("Number: two")}));
}

void tst_PasteCleaner::emptyEdgesAreTrimmed()
{
    const QString body = PasteCleaner::fromHtml(
        QStringLiteral("<html><body><!--StartFragment--><p></p><br><p>Only</p><p></p>"
                       "<!--EndFragment--></body></html>"));
    QCOMPARE(shown(body), QStringList{QStringLiteral("Body: Only")});
}

void tst_PasteCleaner::nothingLeftIsEmpty()
{
    QVERIFY(PasteCleaner::fromHtml(QStringLiteral("<img src=\"https://x/y.png\">")).isEmpty());
    QVERIFY(PasteCleaner::fromHtml(QStringLiteral("<script>x()</script>")).isEmpty());
    QVERIFY(PasteCleaner::fromHtml({}).isEmpty());
}

void tst_PasteCleaner::markdownBecomesStyles()
{
    const QString body = PasteCleaner::fromMarkdown(
        QStringLiteral("# Trip\n\n## Plan\n\n- one\n- two\n  - nested\n\n1. first\n2. second\n\n"
                       "**Done** and *maybe* ~~not~~ [link](https://example.com)\n"));
    QCOMPARE(shown(body),
             (QStringList{QStringLiteral("Heading: Trip"), QStringLiteral("Heading: Plan"),
                          QStringLiteral("Bullet: one"), QStringLiteral("Bullet: two"),
                          QStringLiteral("Bullet+1: nested"), QStringLiteral("Number: first"),
                          QStringLiteral("Number: second"),
                          QStringLiteral("Body: Done and maybe not link")}));
    QCOMPARE(runsOf(body).mid(runsOf(body).size() - 7, 6),
             (QStringList{QStringLiteral("Done[b]"), QStringLiteral(" and []"),
                          QStringLiteral("maybe[i]"), QStringLiteral(" []"),
                          QStringLiteral("not[s]"), QStringLiteral(" []")}));
    QVERIFY(runsOf(body).last().startsWith(QStringLiteral("link[href=https://example.com")));
}

void tst_PasteCleaner::markdownTaskListsBecomeChecklists()
{
    const QString body =
        PasteCleaner::fromMarkdown(QStringLiteral("- [x] Book flights\n- [ ] Pack\n"));
    QCOMPARE(shown(body),
             (QStringList{QStringLiteral("[x]: Book flights"), QStringLiteral("[ ]: Pack")}));
}

void tst_PasteCleaner::markdownCodeAndTables()
{
    const QString body = PasteCleaner::fromMarkdown(QStringLiteral(
        "Run this:\n\n```bash\nmake && ./app\n```\n\n| Day | Plan |\n|---|---|\n| Sat | Hike |\n"));
    const QJsonArray blocks = blocksOf(body);
    QCOMPARE(blocks.size(), 3);
    const QJsonObject code = blocks.at(1).toObject();
    QCOMPARE(code.value(QStringLiteral("code")).toString(), QStringLiteral("bash"));
    QCOMPARE(runsOf(body).at(1), QStringLiteral("make && ./app[mono]"));
    const QJsonObject table = blocks.at(2).toObject().value(QStringLiteral("table")).toObject();
    QCOMPARE(table.value(QStringLiteral("rows")).toInt(), 2);
    QCOMPARE(table.value(QStringLiteral("cols")).toInt(), 2);
    QVERIFY(body.contains(QStringLiteral("Hike")));
}

// TC-EDT-053 and its positive side.
void tst_PasteCleaner::detectsMarkdown_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<bool>("markdown");
    const auto row = [](const char* name, const char* text, bool markdown) {
        QTest::newRow(name) << QString::fromUtf8(text) << markdown;
    };
    row("prose", "Meet at 5 - bring snacks. 2 * 3 is six, a *really* big deal.", false);
    row("one dash line", "Notes\n- remember the keys", false);
    row("a plain list", "Shopping\n- milk\n- eggs", false);
    row("hash tag", "#monday is busy", false);
    row("lone heading", "# Title only", false);
    row("emphasis alone", "This is **important** today.", false);
    row("math", "a * b * c = 6\n- x = 1", false);
    row("AI answer", "## Plan\n- one\n- two\n\n**Done**", true);
    row("heading and bold", "# Summary\nThe **key** point.", true);
    row("list and code", "Steps:\n1. Run `make`\n2. Run `./app`", true);
    row("link and bold", "See [docs](https://example.com) for **details**.", true);
    row("fenced code", "Try:\n```\nls -la\n```", true);
    row("table", "| a | b |\n|---|---|\n| 1 | 2 |", true);
    row("task list", "- [ ] one\n- [x] two", true);
}

void tst_PasteCleaner::detectsMarkdown()
{
    QFETCH(QString, text);
    QFETCH(bool, markdown);
    QCOMPARE(PasteCleaner::looksLikeMarkdown(text), markdown);
}

QTEST_MAIN(tst_PasteCleaner)
#include "tst_pastecleaner.moc"
