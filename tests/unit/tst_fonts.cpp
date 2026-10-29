// TC-UI-003: both bundled font families are available after loading, and each
// weight/style maps to its real face rather than a synthesized one.

#include "ui/Fonts.h"

#include <QFontDatabase>
#include <QFontInfo>
#include <QTest>

using notes::ui::kMonospaceFontFamily;
using notes::ui::kUiFontFamily;

class tst_Fonts : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void familyIsAvailable_data();
    void familyIsAvailable();
    void realFaceIsUsed_data();
    void realFaceIsUsed();
};

void tst_Fonts::initTestCase()
{
    const QStringList failed = notes::ui::loadBundledFonts();
    QVERIFY2(failed.isEmpty(), qPrintable(failed.join(u", ")));
}

void tst_Fonts::familyIsAvailable_data()
{
    QTest::addColumn<QString>("family");
    QTest::newRow("ui") << QString::fromLatin1(kUiFontFamily);
    QTest::newRow("monospace") << QString::fromLatin1(kMonospaceFontFamily);
}

void tst_Fonts::familyIsAvailable()
{
    QFETCH(QString, family);
    QVERIFY(QFontDatabase::families().contains(family));
    // Resolves to the bundled family, not a system fallback.
    QCOMPARE(QFontInfo(QFont(family)).family(), family);
}

void tst_Fonts::realFaceIsUsed_data()
{
    QTest::addColumn<QString>("family");
    QTest::addColumn<int>("weight");
    QTest::addColumn<bool>("italic");
    QTest::addColumn<QString>("expectedStyle");

    const QString ui = QString::fromLatin1(kUiFontFamily);
    const QString mono = QString::fromLatin1(kMonospaceFontFamily);
    QTest::newRow("Inter Regular") << ui << int(QFont::Normal) << false << "Regular";
    QTest::newRow("Inter Italic") << ui << int(QFont::Normal) << true << "Italic";
    QTest::newRow("Inter SemiBold") << ui << int(QFont::DemiBold) << false << "SemiBold";
    QTest::newRow("Inter Bold") << ui << int(QFont::Bold) << false << "Bold";
    QTest::newRow("Inter Bold Italic") << ui << int(QFont::Bold) << true << "Bold Italic";
    QTest::newRow("Mono Regular") << mono << int(QFont::Normal) << false << "Regular";
    QTest::newRow("Mono Bold") << mono << int(QFont::Bold) << false << "Bold";
}

void tst_Fonts::realFaceIsUsed()
{
    QFETCH(QString, family);
    QFETCH(int, weight);
    QFETCH(bool, italic);
    QFETCH(QString, expectedStyle);

    QFont font(family);
    font.setWeight(static_cast<QFont::Weight>(weight));
    font.setItalic(italic);
    const QFontInfo info(font);
    QCOMPARE(info.family(), family);
    QCOMPARE(info.styleName(), expectedStyle);
}

QTEST_MAIN(tst_Fonts)
#include "tst_fonts.moc"
