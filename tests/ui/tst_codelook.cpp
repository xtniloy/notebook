// Code drawn on a gray background in the editor: a pill behind inline
// monospaced text, one panel behind each run of Monospaced paragraphs with
// space above and below it; drawn, not stored.

#include "editor/StyleManager.h"
#include "ui/NoteTextEdit.h"

#include <QAbstractTextDocumentLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextFrame>
#include <QTextLayout>

#include <memory>

using notes::editor::ParagraphStyle;
using notes::ui::NoteTextEdit;

namespace {

QJsonObject run(const QString& text, bool mono = false)
{
    QJsonObject run{{QStringLiteral("text"), text}};
    if (mono) {
        run.insert(QStringLiteral("mono"), true);
    }
    return run;
}

QJsonObject paragraph(const QJsonArray& runs)
{
    return QJsonObject{{QStringLiteral("runs"), runs}};
}

// 0 "Run make now" ("make" inline code), 1 "Before", 2..4 a code block of
// three lines, 5 "After".
QString body()
{
    const QJsonArray blocks{
        paragraph({run(QStringLiteral("Run ")), run(QStringLiteral("make"), true),
                   run(QStringLiteral(" now"))}),
        paragraph({run(QStringLiteral("Before"))}),
        paragraph({run(QStringLiteral("one"), true)}),
        paragraph({run(QStringLiteral("two"), true)}),
        paragraph({run(QStringLiteral("three"), true)}),
        paragraph({run(QStringLiteral("After"))}),
    };
    return QString::fromUtf8(QJsonDocument(QJsonObject{{QStringLiteral("blocks"), blocks}})
                                 .toJson(QJsonDocument::Compact));
}

} // namespace

class tst_CodeLook : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void inlineCodeHasAPill();
    void codeBlockHasOnePanel();
    void spaceAroundTheBlockOnly();
    void spacingIsNotStored();
    void spacingFollowsStyleChangesAndUndo();

private:
    [[nodiscard]] QTextBlock block(int index) const;
    // The viewport's pixel at `x` (viewport coordinates) in the middle of
    // paragraph `index`'s first line.
    [[nodiscard]] QColor pixelAt(int index, qreal x) const;
    [[nodiscard]] qreal textLeft(int index) const;
    [[nodiscard]] QColor codeBackground() const;
    [[nodiscard]] QColor noteBackground() const;

    std::unique_ptr<NoteTextEdit> m_edit;
};

void tst_CodeLook::init()
{
    m_edit = std::make_unique<NoteTextEdit>();
    QPalette palette = m_edit->palette();
    palette.setColor(QPalette::Base, Qt::white);
    palette.setColor(QPalette::AlternateBase, QColor(0xEB, 0xEB, 0xE9));
    m_edit->setPalette(palette);
    m_edit->resize(500, 400);
    m_edit->setTextPadding(24, 24);
    m_edit->setNoteBody(body(), {});
    m_edit->show();
    QVERIFY(QTest::qWaitForWindowExposed(m_edit.get()));
}

void tst_CodeLook::cleanup()
{
    m_edit.reset();
}

QTextBlock tst_CodeLook::block(int index) const
{
    return m_edit->document()->findBlockByNumber(index);
}

qreal tst_CodeLook::textLeft(int index) const
{
    return m_edit->document()->documentLayout()->blockBoundingRect(block(index)).left();
}

QColor tst_CodeLook::pixelAt(int index, qreal x) const
{
    const QTextBlock paragraph = block(index);
    const QRectF bounds = m_edit->document()->documentLayout()->blockBoundingRect(paragraph);
    const QTextLine line = paragraph.layout()->lineAt(0);
    const QImage image = m_edit->viewport()->grab().toImage();
    const qreal ratio = image.devicePixelRatio();
    const QPointF point(x, bounds.top() + line.y() + (line.height() / 2));
    return image.pixelColor((point * ratio).toPoint());
}

QColor tst_CodeLook::codeBackground() const
{
    return m_edit->palette().color(QPalette::AlternateBase);
}

QColor tst_CodeLook::noteBackground() const
{
    return m_edit->palette().color(QPalette::Base);
}

void tst_CodeLook::inlineCodeHasAPill()
{
    // "Run make now": just before "make" (in the pill's reach past the
    // text), and just before "Run" (plain text) at the same distance.
    const QTextLine line = block(0).layout()->lineAt(0);
    const qreal makeLeft = textLeft(0) + line.cursorToX(4);
    QCOMPARE(pixelAt(0, makeLeft - 1.5), codeBackground());
    QCOMPARE(pixelAt(0, textLeft(0) - 1.5), noteBackground());
    // Room after it: its last letter takes more space than the others (all
    // the same width in a monospaced font), so "now" starts clear of the pill.
    const qreal k = line.cursorToX(7) - line.cursorToX(6);
    const qreal e = line.cursorToX(8) - line.cursorToX(7);
    QVERIFY2(e - k > 5, qPrintable(QStringLiteral("%1 vs %2").arg(e).arg(k)));
    // The room isn't stored either.
    QVERIFY(!m_edit->noteBody().contains(QStringLiteral("pacing")));
}

void tst_CodeLook::codeBlockHasOnePanel()
{
    // Near the column's right edge, where no line here has text: the panel
    // spans the column.
    const QTextFrameFormat frame = m_edit->document()->rootFrame()->frameFormat();
    const qreal x = m_edit->document()->textWidth() - frame.rightMargin() - 4;
    for (const int index : {2, 3, 4}) {
        QCOMPARE(pixelAt(index, x), codeBackground());
    }
    QCOMPARE(pixelAt(1, x), noteBackground());
    QCOMPARE(pixelAt(5, x), noteBackground());
    // One panel: no gap between its lines.
    const QAbstractTextDocumentLayout* layout = m_edit->document()->documentLayout();
    const qreal between = layout->blockBoundingRect(block(3)).top();
    const QImage image = m_edit->viewport()->grab().toImage();
    const qreal ratio = image.devicePixelRatio();
    QCOMPARE(image.pixelColor((QPointF(x, between) * ratio).toPoint()), codeBackground());
    // The text is inside it, with room at the left, the top and the bottom.
    const QTextLine first = block(2).layout()->lineAt(0);
    const QRectF firstLine = layout->blockBoundingRect(block(2));
    QCOMPARE(image.pixelColor((QPointF(x, firstLine.top() + first.y() - 4) * ratio).toPoint()),
             codeBackground());
    const QRectF lastLine = layout->blockBoundingRect(block(4));
    QCOMPARE(image.pixelColor((QPointF(x, lastLine.bottom() + 4) * ratio).toPoint()),
             codeBackground());
    QVERIFY(first.x() > 0); // inset from the panel's left edge
}

void tst_CodeLook::spaceAroundTheBlockOnly()
{
    QVERIFY(block(2).blockFormat().topMargin() > 0); // first line of the block
    QCOMPARE(block(2).blockFormat().bottomMargin(), 0.0);
    QCOMPARE(block(3).blockFormat().topMargin(), 0.0);
    QCOMPARE(block(3).blockFormat().bottomMargin(), 0.0);
    QVERIFY(block(4).blockFormat().bottomMargin() > 0); // last line
    for (const int index : {0, 1, 5}) {
        QCOMPARE(block(index).blockFormat().topMargin(), 0.0);
        QCOMPARE(block(index).blockFormat().bottomMargin(), 0.0);
    }
    // Loading isn't an edit.
    QVERIFY(!m_edit->document()->isUndoAvailable());
}

void tst_CodeLook::spacingIsNotStored()
{
    QCOMPARE(m_edit->noteBody(), body());
}

void tst_CodeLook::spacingFollowsStyleChangesAndUndo()
{
    // "After" becomes code: the block grows, and its space moves down.
    QTextCursor cursor(block(5));
    m_edit->setTextCursor(cursor);
    m_edit->setParagraphStyle(ParagraphStyle::Monospaced);
    QTRY_VERIFY(block(5).blockFormat().bottomMargin() > 0);
    QCOMPARE(block(4).blockFormat().bottomMargin(), 0.0);
    QCOMPARE(m_edit->noteBody().contains(QStringLiteral("Margin")), false);

    // Undo takes back the style and the space together.
    m_edit->undo();
    QCoreApplication::processEvents();
    QCOMPARE(notes::editor::StyleManager::styleOf(block(5)), ParagraphStyle::Body);
    QCOMPARE(block(5).blockFormat().bottomMargin(), 0.0);
    QVERIFY(block(4).blockFormat().bottomMargin() > 0);
    // And redo still works afterwards.
    QVERIFY(m_edit->document()->isRedoAvailable());
    m_edit->redo();
    QCoreApplication::processEvents();
    QCOMPARE(notes::editor::StyleManager::styleOf(block(5)), ParagraphStyle::Monospaced);
    QVERIFY(block(5).blockFormat().bottomMargin() > 0);
    QCOMPARE(block(4).blockFormat().bottomMargin(), 0.0);
}

QTEST_MAIN(tst_CodeLook)
#include "tst_codelook.moc"
