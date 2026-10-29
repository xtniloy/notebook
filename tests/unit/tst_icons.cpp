// TC-UI-004: every icon referenced in code resolves and renders, and every bundled
// icon file is referenced (no dead weight in the binary).

#include "ui/Icons.h"

#include <QDirIterator>
#include <QFile>
#include <QImage>
#include <QSet>
#include <QTest>

using notes::ui::Icon;

namespace {

QList<Icon> allIcons()
{
    QList<Icon> icons;
    for (int i = 0; i < static_cast<int>(Icon::Count); ++i) {
        icons.append(static_cast<Icon>(i));
    }
    return icons;
}

bool hasVisiblePixels(const QImage& image)
{
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (qAlpha(image.pixel(x, y)) > 0) {
                return true;
            }
        }
    }
    return false;
}

} // namespace

class tst_Icons : public QObject
{
    Q_OBJECT

private slots:
    void iconRenders_data();
    void iconRenders();
    void pathsAreUnique();
    void everyBundledIconIsReferenced();
};

void tst_Icons::iconRenders_data()
{
    QTest::addColumn<Icon>("icon");
    for (const Icon icon : allIcons()) {
        QTest::newRow(qPrintable(notes::ui::iconPath(icon))) << icon;
    }
}

void tst_Icons::iconRenders()
{
    QFETCH(Icon, icon);

    const QString path = notes::ui::iconPath(icon);
    QVERIFY(path.startsWith(u":/icons/"));
    QVERIFY2(QFile::exists(path), qPrintable(path));

    const QIcon qicon = notes::ui::icon(icon);
    QVERIFY(!qicon.isNull());
    const QPixmap pixmap = qicon.pixmap(24, 24);
    QVERIFY(!pixmap.isNull());
    QVERIFY(hasVisiblePixels(pixmap.toImage()));
}

void tst_Icons::pathsAreUnique()
{
    QSet<QString> paths;
    for (const Icon icon : allIcons()) {
        paths.insert(notes::ui::iconPath(icon));
    }
    QCOMPARE(paths.size(), static_cast<qsizetype>(Icon::Count));
}

void tst_Icons::everyBundledIconIsReferenced()
{
    QSet<QString> referenced;
    for (const Icon icon : allIcons()) {
        referenced.insert(notes::ui::iconPath(icon));
    }

    QSet<QString> bundled;
    QDirIterator it(QStringLiteral(":/icons"), {QStringLiteral("*.svg")}, QDir::Files);
    while (it.hasNext()) {
        bundled.insert(it.next());
    }

    QVERIFY(!bundled.isEmpty());
    QCOMPARE(bundled - referenced, QSet<QString>());
}

QTEST_MAIN(tst_Icons)
#include "tst_icons.moc"
