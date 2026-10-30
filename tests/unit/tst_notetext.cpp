// NoteText (task 2.7): TC-STO-019 title derivation, TC-STO-020 snippet derivation.

#include "services/NoteText.h"

#include <QTest>

using notes::services::NoteText;

class tst_NoteText : public QObject
{
    Q_OBJECT

private slots:
    void title_data();
    void title();
    void titleNeverSplitsAnEmoji();
    void snippet_data();
    void snippet();
};

// TC-STO-019
void tst_NoteText::title_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<QString>("expected");

    QTest::newRow("first line") << QStringLiteral("Shopping\nMilk") << QStringLiteral("Shopping");
    QTest::newRow("blank lines and spaces")
        << QStringLiteral("\n\n  Hi  \n") << QStringLiteral("Hi");
    QTest::newRow("empty") << QString() << QString();
    QTest::newRow("only whitespace") << QStringLiteral(" \n\t\n ") << QString();
    QTest::newRow("500-char line") << QString(500, u'x') << QString(200, u'x');
    QTest::newRow("CRLF") << QStringLiteral("Title\r\nBody") << QStringLiteral("Title");
    QTest::newRow("paragraph separator")
        << (QStringLiteral("Title") + QChar(QChar::ParagraphSeparator) + QStringLiteral("Body"))
        << QStringLiteral("Title");
    QTest::newRow("unicode") << QStringLiteral("Élan 日本語 🗒️\nx")
                             << QStringLiteral("Élan 日本語 🗒️");
}

void tst_NoteText::title()
{
    QFETCH(QString, text);
    QFETCH(QString, expected);
    QCOMPARE(NoteText::title(text), expected);
}

void tst_NoteText::titleNeverSplitsAnEmoji()
{
    // 199 x's then an emoji (two UTF-16 units): cutting at 200 would leave half of it.
    const QString text = QString(199, u'x') + QStringLiteral("🗒") + QStringLiteral("more");
    const QString title = NoteText::title(text);
    QVERIFY(title.size() <= NoteText::kMaxTitleLength);
    QVERIFY(!title.back().isHighSurrogate());
    QCOMPARE(title, QString(199, u'x'));
}

// TC-STO-020
void tst_NoteText::snippet_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<QString>("expected");

    QTest::newRow("skips title") << QStringLiteral("Shopping\nMilk") << QStringLiteral("Milk");
    QTest::newRow("skips blank lines")
        << QStringLiteral("\nShopping\n\n  \nMilk\n\nEggs\n") << QStringLiteral("Milk Eggs");
    QTest::newRow("title only") << QStringLiteral("Just a title") << QString();
    QTest::newRow("empty") << QString() << QString();
    QTest::newRow("trimmed lines") << QStringLiteral("T\n   a   \n b ") << QStringLiteral("a b");
    QTest::newRow("max 120") << (QStringLiteral("T\n") + QString(300, u'y')) << QString(120, u'y');
}

void tst_NoteText::snippet()
{
    QFETCH(QString, text);
    QFETCH(QString, expected);
    QCOMPARE(NoteText::snippet(text), expected);
}

QTEST_APPLESS_MAIN(tst_NoteText)
#include "tst_notetext.moc"
