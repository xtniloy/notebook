// Links in the editor (task 3.9): TC-EDT-030 typing an address and a space
// makes it a link; Ctrl+Click opens allowed links only (TC-EDT-031 in the
// editor); links are drawn as links.

#include "editor/StyleManager.h"
#include "ui/NoteTextEdit.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>
#include <QTextBlock>
#include <QTextCursor>
#include <QUrl>

#include <memory>

using notes::editor::ParagraphStyle;
using notes::ui::NoteTextEdit;

namespace {

// A body of one paragraph: "Go " then `text` linked to `href`, then " now".
QString bodyWithLink(const QString& text, const QString& href)
{
    const QJsonArray runs{
        QJsonObject{{QStringLiteral("text"), QStringLiteral("Go ")}},
        QJsonObject{{QStringLiteral("text"), text}, {QStringLiteral("href"), href}},
        QJsonObject{{QStringLiteral("text"), QStringLiteral(" now")}},
    };
    const QJsonObject body{
        {QStringLiteral("blocks"), QJsonArray{QJsonObject{{QStringLiteral("runs"), runs}}}}};
    return QString::fromUtf8(QJsonDocument(body).toJson(QJsonDocument::Compact));
}

} // namespace

class tst_LinkEditing : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void typedAddressBecomesLink();
    void enterAndTabEndAnAddressToo();
    void undoGivesThePlainTextBack();
    void spaceAfterALinkAddsNoLink();
    void codeStaysAsTyped();
    void ctrlClickOpensTheLink();
    void plainClickOnlyMovesTheCaret();
    void onlyAllowedLinksOpen_data();
    void onlyAllowedLinksOpen();
    void linksAreDrawnAsLinks();
    void linksFarDownAreDrawnInView();

private:
    // Types `text` into the editor, key by key.
    void type(const QString& text);
    // The link at `position` in the document; empty if none.
    [[nodiscard]] QString hrefAt(int position) const;
    // The middle of the character at `position`, in the viewport.
    [[nodiscard]] QPoint pointAt(int position) const;

    std::unique_ptr<NoteTextEdit> m_edit;
    QList<QUrl> m_opened;
};

void tst_LinkEditing::init()
{
    m_edit = std::make_unique<NoteTextEdit>();
    m_edit->resize(600, 400);
    m_opened.clear();
    m_edit->setLinkOpener([this](const QUrl& url) {
        m_opened.append(url);
        return true;
    });
    m_edit->setNoteBody({}, {});
    m_edit->show();
    QVERIFY(QTest::qWaitForWindowExposed(m_edit.get()));
}

void tst_LinkEditing::cleanup()
{
    m_edit.reset();
}

void tst_LinkEditing::type(const QString& text)
{
    for (const QChar character : text) {
        QTest::keyClicks(m_edit.get(), QString(character));
    }
}

QString tst_LinkEditing::hrefAt(int position) const
{
    QTextCursor cursor(m_edit->document());
    cursor.setPosition(position + 1); // the format of the character before
    const QTextCharFormat format = cursor.charFormat();
    return format.isAnchor() ? format.anchorHref() : QString();
}

QPoint tst_LinkEditing::pointAt(int position) const
{
    QTextCursor cursor(m_edit->document());
    cursor.setPosition(position);
    const QRect caret = m_edit->cursorRect(cursor);
    cursor.setPosition(position + 1);
    const QRect next = m_edit->cursorRect(cursor);
    return {(caret.left() + next.left()) / 2, caret.center().y()};
}

// TC-EDT-030
void tst_LinkEditing::typedAddressBecomesLink()
{
    type(QStringLiteral("see https://example.com"));
    QVERIFY(hrefAt(10).isEmpty()); // not until the address ends
    QTest::keyClick(m_edit.get(), Qt::Key_Space);

    QCOMPARE(m_edit->toPlainText(), QStringLiteral("see https://example.com "));
    QVERIFY(hrefAt(0).isEmpty());
    QVERIFY(hrefAt(3).isEmpty()); // the space before
    for (const int position : {4, 15, 22}) {
        QCOMPARE(hrefAt(position), QStringLiteral("https://example.com"));
    }
    QVERIFY(hrefAt(23).isEmpty()); // the space after
    // Typing on isn't part of the link.
    type(QStringLiteral("ok"));
    QVERIFY(hrefAt(24).isEmpty());
    // Saved with the note.
    QVERIFY2(m_edit->noteBody().contains(QStringLiteral("\"href\":\"https://example.com\"")),
             qPrintable(m_edit->noteBody()));

    // A sentence's period isn't part of it; www and e-mail addresses link too.
    type(QStringLiteral(" www.example.org. me@example.com "));
    const QString text = m_edit->toPlainText();
    const int www = static_cast<int>(text.indexOf(QStringLiteral("www.")));
    QCOMPARE(hrefAt(www), QStringLiteral("https://www.example.org"));
    QVERIFY(hrefAt(www + 15).isEmpty()); // the period
    QCOMPARE(hrefAt(static_cast<int>(text.indexOf(u'@'))), QStringLiteral("mailto:me@example.com"));
}

void tst_LinkEditing::enterAndTabEndAnAddressToo()
{
    type(QStringLiteral("https://a.example"));
    QTest::keyClick(m_edit.get(), Qt::Key_Return);
    QCOMPARE(hrefAt(0), QStringLiteral("https://a.example"));
    type(QStringLiteral("http://b.example"));
    QTest::keyClick(m_edit.get(), Qt::Key_Tab);
    const int second = m_edit->document()->findBlockByNumber(1).position();
    QCOMPARE(hrefAt(second), QStringLiteral("http://b.example"));
}

void tst_LinkEditing::undoGivesThePlainTextBack()
{
    type(QStringLiteral("https://example.com"));
    QTest::keyClick(m_edit.get(), Qt::Key_Space);
    QCOMPARE(hrefAt(0), QStringLiteral("https://example.com"));
    QTest::keyClick(m_edit.get(), Qt::Key_Z, Qt::ControlModifier);
    // The link goes first; the text and the space stay.
    QCOMPARE(m_edit->toPlainText(), QStringLiteral("https://example.com "));
    QVERIFY(hrefAt(0).isEmpty());
    QVERIFY(!m_edit->noteBody().contains(QStringLiteral("href")));
}

void tst_LinkEditing::spaceAfterALinkAddsNoLink()
{
    // A disallowed scheme typed: never a link.
    type(QStringLiteral("file:///etc/passwd javascript:alert(1) smb://x "));
    QVERIFY(!m_edit->noteBody().contains(QStringLiteral("href")));

    // Plain words and a lone "www." neither.
    type(QStringLiteral("hello www. "));
    QVERIFY(!m_edit->noteBody().contains(QStringLiteral("href")));
}

void tst_LinkEditing::codeStaysAsTyped()
{
    m_edit->setParagraphStyle(ParagraphStyle::Monospaced);
    type(QStringLiteral("curl https://example.com "));
    QVERIFY(!m_edit->noteBody().contains(QStringLiteral("href")));
}

void tst_LinkEditing::ctrlClickOpensTheLink()
{
    m_edit->setNoteBody(
        bodyWithLink(QStringLiteral("there"), QStringLiteral("https://example.com/x")),
        QStringLiteral("Go there now"));
    QTextCursor caret(m_edit->document());
    m_edit->setTextCursor(caret);
    QTest::mouseClick(m_edit->viewport(), Qt::LeftButton, Qt::ControlModifier, pointAt(5));
    QCOMPARE(m_opened, QList<QUrl>{QUrl(QStringLiteral("https://example.com/x"))});
    QCOMPARE(m_edit->textCursor().position(), 0); // the caret stays

    // Read-only notes (Recently Deleted) open links too.
    m_edit->setReadOnly(true);
    QTest::mouseClick(m_edit->viewport(), Qt::LeftButton, Qt::ControlModifier, pointAt(5));
    QCOMPARE(m_opened.size(), 2);

    // Ctrl+Click beside the link opens nothing.
    QTest::mouseClick(m_edit->viewport(), Qt::LeftButton, Qt::ControlModifier, pointAt(1));
    QCOMPARE(m_opened.size(), 2);
}

void tst_LinkEditing::plainClickOnlyMovesTheCaret()
{
    m_edit->setNoteBody(
        bodyWithLink(QStringLiteral("there"), QStringLiteral("https://example.com")),
        QStringLiteral("Go there now"));
    QTest::mouseClick(m_edit->viewport(), Qt::LeftButton, Qt::NoModifier, pointAt(5));
    QVERIFY(m_opened.isEmpty());
    QVERIFY(m_edit->textCursor().position() >= 5 && m_edit->textCursor().position() <= 6);
}

// TC-EDT-031, in the editor: a note can hold any link (pasted from another
// note, imported, synced), and those are never opened.
void tst_LinkEditing::onlyAllowedLinksOpen_data()
{
    QTest::addColumn<QString>("href");
    QTest::addColumn<bool>("opens");
    QTest::newRow("https") << QStringLiteral("https://example.com") << true;
    QTest::newRow("mailto") << QStringLiteral("mailto:me@example.com") << true;
    QTest::newRow("file") << QStringLiteral("file:///etc/passwd") << false;
    QTest::newRow("javascript") << QStringLiteral("javascript:alert(1)") << false;
    QTest::newRow("smb") << QStringLiteral("smb://x") << false;
}

void tst_LinkEditing::onlyAllowedLinksOpen()
{
    QFETCH(QString, href);
    QFETCH(bool, opens);
    m_edit->setNoteBody(bodyWithLink(QStringLiteral("there"), href),
                        QStringLiteral("Go there now"));
    QCOMPARE(hrefAt(5), href);
    QTest::mouseClick(m_edit->viewport(), Qt::LeftButton, Qt::ControlModifier, pointAt(5));
    QCOMPARE(m_opened.size(), opens ? 1 : 0);
    QCOMPARE(m_edit->openLink(href), opens);
    QCOMPARE(m_opened.size(), opens ? 2 : 0);
}

void tst_LinkEditing::linksAreDrawnAsLinks()
{
    m_edit->setNoteBody(
        bodyWithLink(QStringLiteral("there"), QStringLiteral("https://example.com")),
        QStringLiteral("Go there now"));
    QTRY_COMPARE(m_edit->extraSelections().size(), 1);
    const QTextEdit::ExtraSelection link = m_edit->extraSelections().first();
    QCOMPARE(link.cursor.selectionStart(), 3);
    QCOMPARE(link.cursor.selectionEnd(), 8);
    QCOMPARE(link.format.foreground().color(), m_edit->palette().color(QPalette::Link));
    QVERIFY(link.format.fontUnderline());
    // Drawn so, not stored: the body has no color or underline.
    QVERIFY(!m_edit->noteBody().contains(QStringLiteral("\"u\"")));
    QVERIFY(!m_edit->noteBody().contains(QStringLiteral("\"fg\"")));
}

// The link look is drawn for the paragraphs in view (task 3.11: a long note
// opens without Qt laying all of it out at once): a link far down gets it
// once scrolled to.
void tst_LinkEditing::linksFarDownAreDrawnInView()
{
    QJsonArray blocks;
    for (int i = 0; i < 400; ++i) {
        blocks.append(QJsonObject{
            {QStringLiteral("runs"),
             QJsonArray{QJsonObject{{QStringLiteral("text"), QStringLiteral("Line %1").arg(i)}}}}});
    }
    const QJsonArray link{
        QJsonObject{{QStringLiteral("text"), QStringLiteral("there")},
                    {QStringLiteral("href"), QStringLiteral("https://example.com")}}};
    blocks.append(QJsonObject{{QStringLiteral("runs"), link}});
    m_edit->setNoteBody(
        QString::fromUtf8(QJsonDocument(QJsonObject{{QStringLiteral("blocks"), blocks}})
                              .toJson(QJsonDocument::Compact)),
        {});
    QCoreApplication::processEvents();
    QVERIFY(m_edit->extraSelections().isEmpty()); // far out of view

    m_edit->moveCursor(QTextCursor::End);
    m_edit->ensureCursorVisible();
    QTRY_COMPARE(m_edit->extraSelections().size(), 1);
    QCOMPARE(m_edit->extraSelections().first().cursor.selectedText(), QStringLiteral("there"));
}

QTEST_MAIN(tst_LinkEditing)
#include "tst_linkediting.moc"
