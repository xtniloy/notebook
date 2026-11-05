// Safe paste in the window (task 3.8): TC-EDT-027 pasted HTML never reaches
// the network, TC-EDT-029 Ctrl+Shift+V pastes plain text, TC-EDT-052 Markdown
// becomes styles, TC-EDT-054 Ctrl+Alt+V forces Markdown and Ctrl+Shift+V
// keeps its characters.

#include "TestHelpers.h"
#include "editor/StyleManager.h"
#include "models/Note.h"
#include "models/SidebarRoles.h"
#include "services/Autosave.h"
#include "services/FolderService.h"
#include "services/NoteService.h"
#include "ui/EditorView.h"
#include "ui/MainWindow.h"
#include "ui/NoteTextEdit.h"
#include "ui/SearchField.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QMenuBar>
#include <QMimeData>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QTextBlock>
#include <QTextList>

#include <memory>

using notes::editor::ParagraphStyle;
using notes::editor::StyleManager;
using notes::models::Note;
using notes::test::await;
using notes::ui::EditorView;
using notes::ui::MainWindow;
using notes::ui::NoteTextEdit;
namespace roles = notes::models;

class tst_Paste : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void pastedHtmlFetchesNothing();
    void htmlKeepsFormatsNotLooks();
    void shiftPastesPlainText();
    void markdownIsConverted();
    void altForcesMarkdownShiftKeepsIt();
    void htmlWinsOverMarkdown();
    void pasteIntoTextKeepsItsStyle();
    void listIntoAnEmptyFirstLine();
    void copiesWithinTheAppKeepEverything();
    void pasteActionsAreInTheEditMenu();
    void shortcutsOnlyActInTheEditor();

private:
    void openWindow();
    // Clipboard content: HTML and/or text.
    static void setClipboard(const QString& html, const QString& text);
    // The block after the note's text, empty, for pasting into.
    void newLine();
    // "Style: text" for the blocks from `from` on.
    [[nodiscard]] QStringList blocks(int from = 0) const;

    std::unique_ptr<notes::test::TempDb> m_dir;
    std::unique_ptr<notes::app::AppContext> m_context;
    std::unique_ptr<MainWindow> m_window;
    NoteTextEdit* m_edit = nullptr;
    Note m_note;
};

void tst_Paste::init()
{
    m_dir = std::make_unique<notes::test::TempDb>();
    m_context = notes::test::makeTestContext(m_dir->dirPath());
    (void)await(m_context->folders().ensureDefaultFolder());
    m_note = await(m_context->notes().create(roles::kAllNotesFolderId, {}, QStringLiteral("Notes")))
                 .value();
    QApplication::clipboard()->clear();
    openWindow();
}

void tst_Paste::cleanup()
{
    m_window = nullptr;
    m_context = nullptr;
    m_dir = nullptr;
}

void tst_Paste::openWindow()
{
    m_window = std::make_unique<MainWindow>(*m_context);
    m_window->show();
    m_window->activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(m_window.get()));
    m_edit = m_window->findChild<EditorView*>(QStringLiteral("editorPane"))->textEdit();
    QTRY_COMPARE(m_edit->toPlainText(), QStringLiteral("Notes"));
    m_edit->setFocus();
    QTRY_VERIFY(m_edit->hasFocus());
    m_edit->moveCursor(QTextCursor::End);
}

void tst_Paste::setClipboard(const QString& html, const QString& text)
{
    auto* mime = new QMimeData;
    if (!html.isEmpty()) {
        mime->setHtml(html);
    }
    if (!text.isEmpty()) {
        mime->setText(text);
    }
    QApplication::clipboard()->setMimeData(mime);
}

void tst_Paste::newLine()
{
    m_edit->moveCursor(QTextCursor::End);
    QTest::keyClick(m_edit, Qt::Key_Return);
}

QStringList tst_Paste::blocks(int from) const
{
    QStringList result;
    for (QTextBlock block = m_edit->document()->findBlockByNumber(from); block.isValid();
         block = block.next()) {
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
        result.append(style + QStringLiteral(": ") + block.text());
    }
    return result;
}

// TC-EDT-027
void tst_Paste::pastedHtmlFetchesNothing()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    int requests = 0;
    connect(&server, &QTcpServer::newConnection, &server, [&] {
        while (QTcpSocket* socket = server.nextPendingConnection()) {
            ++requests;
            socket->close();
        }
    });
    const QString base = QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort());
    setClipboard(QStringLiteral("<html><head><link rel=\"stylesheet\" href=\"%1/style.css\"></head>"
                                "<body style=\"background-image: url(%1/bg.png)\">"
                                "<p>Hello<img src=\"%1/pixel.png\" width=\"1\" height=\"1\"></p>"
                                "<p><img src=\"%1/photo.jpg\"></p></body></html>")
                     .arg(base),
                 QStringLiteral("Hello"));
    newLine();
    QTest::keyClick(m_edit, Qt::Key_V, Qt::ControlModifier);
    QCOMPARE(m_edit->notePlainText(), QStringLiteral("Notes\nHello"));

    // Shown, laid out and saved: still nothing asked for.
    m_edit->viewport()->repaint();
    (void)m_edit->grab();
    m_window->findChild<notes::services::Autosave*>()->flush();
    QTest::qWait(500);
    QCOMPARE(requests, 0);
    QVERIFY(!m_edit->noteBody().contains(QStringLiteral("127.0.0.1")));
}

void tst_Paste::htmlKeepsFormatsNotLooks()
{
    setClipboard(QStringLiteral("<h2 style=\"color:red\">Agenda</h2>"
                                "<ul><li><b>Bold</b> item</li><li>Second</li></ul>"
                                "<p style=\"font-size:40px;font-family:Impact\">Plain</p>"),
                 QStringLiteral("Agenda\nBold item\nSecond\nPlain"));
    newLine();
    QTest::keyClick(m_edit, Qt::Key_V, Qt::ControlModifier);
    QCOMPARE(blocks(1),
             (QStringList{QStringLiteral("Heading: Agenda"), QStringLiteral("Bullet: Bold item"),
                          QStringLiteral("Bullet: Second"), QStringLiteral("Body: Plain")}));
    const QTextBlock bullet = m_edit->document()->findBlockByNumber(2);
    QCOMPARE(bullet.begin().fragment().charFormat().fontWeight(), int{QFont::Bold});
    const QTextBlock plain = m_edit->document()->findBlockByNumber(4);
    const QTextCharFormat format = plain.begin().fragment().charFormat();
    QVERIFY(!format.hasProperty(QTextFormat::FontPointSize));
    QVERIFY(!format.hasProperty(QTextFormat::FontPixelSize));
    QVERIFY(!format.hasProperty(QTextFormat::ForegroundBrush));
    QVERIFY(!format.fontFamilies().toStringList().contains(QStringLiteral("Impact")));

    // One step to undo it all.
    QTest::keyClick(m_edit, Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(m_edit->toPlainText(), QStringLiteral("Notes\n"));
}

// TC-EDT-029
void tst_Paste::shiftPastesPlainText()
{
    setClipboard(QStringLiteral("<h1>Big</h1><p><b>bold</b> <i>text</i></p>"),
                 QStringLiteral("Big\nbold text"));
    newLine();
    QTest::keyClick(m_edit, Qt::Key_V, Qt::ControlModifier | Qt::ShiftModifier);
    QCOMPARE(blocks(1),
             (QStringList{QStringLiteral("Body: Big"), QStringLiteral("Body: bold text")}));
    for (QTextBlock block = m_edit->document()->findBlockByNumber(1); block.isValid();
         block = block.next()) {
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            QVERIFY(it.fragment().charFormat().fontWeight() < QFont::Bold);
            QVERIFY(!it.fragment().charFormat().fontItalic());
        }
    }
}

// TC-EDT-052
void tst_Paste::markdownIsConverted()
{
    setClipboard({}, QStringLiteral("## Plan\n- one\n- two\n\n**Done**"));
    newLine();
    QTest::keyClick(m_edit, Qt::Key_V, Qt::ControlModifier);
    QCOMPARE(blocks(1), (QStringList{QStringLiteral("Heading: Plan"), QStringLiteral("Bullet: one"),
                                     QStringLiteral("Bullet: two"), QStringLiteral("Body: Done")}));
    const QTextBlock done = m_edit->document()->lastBlock();
    QCOMPARE(done.begin().fragment().charFormat().fontWeight(), int{QFont::Bold});
    QCOMPARE(m_edit->document()->findBlockByNumber(2).textList(),
             m_edit->document()->findBlockByNumber(3).textList()); // one list
}

// TC-EDT-054
void tst_Paste::altForcesMarkdownShiftKeepsIt()
{
    // Only a list: not enough to look like Markdown.
    setClipboard({}, QStringLiteral("- milk\n- eggs"));
    newLine();
    QTest::keyClick(m_edit, Qt::Key_V, Qt::ControlModifier);
    QCOMPARE(blocks(1),
             (QStringList{QStringLiteral("Body: - milk"), QStringLiteral("Body: - eggs")}));

    newLine();
    QTest::keyClick(m_edit, Qt::Key_V, Qt::ControlModifier | Qt::AltModifier);
    QCOMPARE(blocks(3),
             (QStringList{QStringLiteral("Bullet: milk"), QStringLiteral("Bullet: eggs")}));

    // Markdown, as it is.
    setClipboard({}, QStringLiteral("## Plan\n**Done**"));
    m_edit->moveCursor(QTextCursor::End);
    QTest::keyClick(m_edit, Qt::Key_Return);
    QTest::keyClick(m_edit, Qt::Key_Return); // out of the list
    QTest::keyClick(m_edit, Qt::Key_V, Qt::ControlModifier | Qt::ShiftModifier);
    QVERIFY2(m_edit->toPlainText().endsWith(QStringLiteral("## Plan\n**Done**")),
             qPrintable(m_edit->toPlainText()));
}

void tst_Paste::htmlWinsOverMarkdown()
{
    setClipboard(QStringLiteral("<p>From <i>HTML</i></p>"), QStringLiteral("## Plan\n**Done**"));
    newLine();
    QTest::keyClick(m_edit, Qt::Key_V, Qt::ControlModifier);
    QCOMPARE(blocks(1), QStringList{QStringLiteral("Body: From HTML")});
}

void tst_Paste::pasteIntoTextKeepsItsStyle()
{
    // Into a line with text: it keeps its style, and the lines after it
    // have theirs.
    setClipboard(QStringLiteral("<h3>Sub</h3><h2>Next</h2>"), QStringLiteral("Sub\nNext"));
    m_edit->moveCursor(QTextCursor::End);
    QTest::keyClick(m_edit, Qt::Key_V, Qt::ControlModifier);
    QCOMPARE(blocks(),
             (QStringList{QStringLiteral("Body: NotesSub"), QStringLiteral("Heading: Next")}));
}

void tst_Paste::listIntoAnEmptyFirstLine()
{
    m_edit->setNoteBody({}, {}); // a new note: one empty Title line
    QCOMPARE(blocks(), QStringList{QStringLiteral("Title: ")});
    setClipboard({}, QStringLiteral("- [ ] Pack\n- [x] Book"));
    QTest::keyClick(m_edit, Qt::Key_V, Qt::ControlModifier);
    QCOMPARE(blocks(), (QStringList{QStringLiteral("[ ]: Pack"), QStringLiteral("[x]: Book")}));
    QCOMPARE(m_edit->document()->firstBlock().textList(),
             m_edit->document()->lastBlock().textList());
    QTest::keyClick(m_edit, Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(blocks(), QStringList{QStringLiteral("Title: ")});
}

void tst_Paste::copiesWithinTheAppKeepEverything()
{
    newLine();
    QTest::keyClicks(m_edit, QStringLiteral("Buy"));
    m_edit->toggleChecklist();
    QTest::keyClick(m_edit, Qt::Key_Return);
    QTest::keyClicks(m_edit, QStringLiteral("Cook"));
    QTextCursor all = m_edit->textCursor();
    all.setPosition(m_edit->document()->findBlockByNumber(1).position());
    all.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
    m_edit->setTextCursor(all);
    m_edit->copy();
    QVERIFY(QApplication::clipboard()->mimeData()->hasFormat(
        QLatin1StringView(NoteTextEdit::kFragmentMime)));

    m_edit->moveCursor(QTextCursor::End);
    QTest::keyClick(m_edit, Qt::Key_Return);
    QTest::keyClick(m_edit, Qt::Key_Return); // out of the checklist
    QTest::keyClick(m_edit, Qt::Key_V, Qt::ControlModifier);
    QCOMPARE(blocks(1), (QStringList{QStringLiteral("[ ]: Buy"), QStringLiteral("[ ]: Cook"),
                                     QStringLiteral("[ ]: Buy"), QStringLiteral("[ ]: Cook")}));
}

void tst_Paste::pasteActionsAreInTheEditMenu()
{
    QMenu* edit = nullptr;
    for (QAction* menu : m_window->menuBar()->actions()) {
        if (menu->text() == u"&Edit") {
            edit = menu->menu();
        }
    }
    if (edit == nullptr) {
        QFAIL("no Edit menu");
    }
    QAction* plain = nullptr;
    for (QAction* action : edit->actions()) {
        if (action->objectName() == u"pasteAsPlainTextAction") {
            plain = action;
        }
    }
    if (plain == nullptr) {
        QFAIL("Paste as Plain Text isn't in the Edit menu");
    }
    QCOMPARE(plain->shortcut(), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_V));
    QCOMPARE(m_edit->pasteAsMarkdownAction()->shortcut(),
             QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_V));
    QVERIFY(plain->isEnabled());

    // Triggered from the menu: into the note.
    setClipboard(QStringLiteral("<b>x</b>"), QStringLiteral("menu text"));
    newLine();
    plain->trigger();
    QCOMPARE(m_edit->document()->lastBlock().text(), QStringLiteral("menu text"));
}

void tst_Paste::shortcutsOnlyActInTheEditor()
{
    setClipboard({}, QStringLiteral("typed elsewhere"));
    auto* search = m_window->findChild<notes::ui::SearchField*>();
    QVERIFY(search);
    auto* field = search->findChild<QLineEdit*>();
    QWidget* target = field != nullptr ? static_cast<QWidget*>(field) : search;
    target->setFocus();
    QTRY_VERIFY(target->hasFocus());
    QTest::keyClick(target, Qt::Key_V, Qt::ControlModifier | Qt::ShiftModifier);
    QTest::keyClick(target, Qt::Key_V, Qt::ControlModifier | Qt::AltModifier);
    QCOMPARE(m_edit->toPlainText(), QStringLiteral("Notes"));
}

QTEST_MAIN(tst_Paste)
#include "tst_paste.moc"
