// Links (task 3.9): TC-EDT-031 only http, https and mailto links are ever
// opened; and finding the address just typed.

#include "editor/Links.h"

#include <QTest>

using notes::editor::Links;

class tst_Links : public QObject
{
    Q_OBJECT

private slots:
    void allowedSchemes_data();
    void allowedSchemes();
    void addressEndingTheText_data();
    void addressEndingTheText();
};

// TC-EDT-031
void tst_Links::allowedSchemes_data()
{
    QTest::addColumn<QString>("href");
    QTest::addColumn<bool>("allowed");
    QTest::newRow("https") << QStringLiteral("https://example.com/a?b=c#d") << true;
    QTest::newRow("http") << QStringLiteral("http://example.com") << true;
    QTest::newRow("upper case scheme") << QStringLiteral("HTTPS://EXAMPLE.COM") << true;
    QTest::newRow("mailto") << QStringLiteral("mailto:me@example.com") << true;
    QTest::newRow("file") << QStringLiteral("file:///etc/passwd") << false;
    QTest::newRow("javascript") << QStringLiteral("javascript:alert(1)") << false;
    QTest::newRow("smb") << QStringLiteral("smb://x") << false;
    QTest::newRow("data") << QStringLiteral("data:text/html,<script>alert(1)</script>") << false;
    QTest::newRow("ftp") << QStringLiteral("ftp://example.com") << false;
    QTest::newRow("app scheme") << QStringLiteral("vscode://file/etc/passwd") << false;
    QTest::newRow("relative path") << QStringLiteral("/etc/passwd") << false;
    QTest::newRow("no host") << QStringLiteral("https://") << false;
    QTest::newRow("https without slashes") << QStringLiteral("https:/etc/passwd") << false;
    QTest::newRow("mailto without address") << QStringLiteral("mailto:") << false;
    QTest::newRow("empty") << QString() << false;
}

void tst_Links::allowedSchemes()
{
    QFETCH(QString, href);
    QFETCH(bool, allowed);
    QCOMPARE(Links::isAllowed(href), allowed);
    QCOMPARE(Links::isAllowed(QUrl(href)), allowed);
}

void tst_Links::addressEndingTheText_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<QString>("link"); // the linked text; empty: none
    QTest::addColumn<QString>("href");
    const auto row = [](const char* name, const QString& text, const QString& link,
                        const QString& href) { QTest::newRow(name) << text << link << href; };
    row("alone", QStringLiteral("https://example.com"), QStringLiteral("https://example.com"),
        QStringLiteral("https://example.com"));
    row("after words", QStringLiteral("see http://example.com/a_b"),
        QStringLiteral("http://example.com/a_b"), QStringLiteral("http://example.com/a_b"));
    row("www", QStringLiteral("www.example.com"), QStringLiteral("www.example.com"),
        QStringLiteral("https://www.example.com"));
    row("e-mail", QStringLiteral("write to me@example.co.uk"), QStringLiteral("me@example.co.uk"),
        QStringLiteral("mailto:me@example.co.uk"));
    row("mailto", QStringLiteral("mailto:me@example.com"), QStringLiteral("mailto:me@example.com"),
        QStringLiteral("mailto:me@example.com"));
    row("sentence's period", QStringLiteral("at https://example.com."),
        QStringLiteral("https://example.com"), QStringLiteral("https://example.com"));
    row("in brackets", QStringLiteral("(https://example.com)"),
        QStringLiteral("https://example.com"), QStringLiteral("https://example.com"));
    row("its own brackets", QStringLiteral("https://en.wikipedia.org/wiki/Qt_(software)"),
        QStringLiteral("https://en.wikipedia.org/wiki/Qt_(software)"),
        QStringLiteral("https://en.wikipedia.org/wiki/Qt_(software)"));
    row("in quotes", QStringLiteral("“https://example.com”"), QStringLiteral("https://example.com"),
        QStringLiteral("https://example.com"));
    row("after an image", QStringLiteral("￼https://example.com"),
        QStringLiteral("https://example.com"), QStringLiteral("https://example.com"));

    row("a word", QStringLiteral("hello"), {}, {});
    row("not at the end", QStringLiteral("https://example.com and more"), {}, {});
    row("scheme only", QStringLiteral("https://"), {}, {});
    row("www only", QStringLiteral("www."), {}, {});
    row("file", QStringLiteral("file:///etc/passwd"), {}, {});
    row("javascript", QStringLiteral("javascript:alert(1)"), {}, {});
    row("smb", QStringLiteral("smb://x"), {}, {});
    row("handle", QStringLiteral("@someone"), {}, {});
    row("not an address", QStringLiteral("a@b"), {}, {});
    row("empty", QString(), {}, {});
}

void tst_Links::addressEndingTheText()
{
    QFETCH(QString, text);
    QFETCH(QString, link);
    QFETCH(QString, href);
    const std::optional<Links::Match> match = Links::endingAt(text);
    if (link.isEmpty()) {
        QVERIFY(!match.has_value());
        return;
    }
    if (!match) {
        QFAIL("no link found");
    }
    QCOMPARE(text.sliced(match->start, match->length), link);
    QCOMPARE(match->href, href);
}

QTEST_GUILESS_MAIN(tst_Links)
#include "tst_links.moc"
