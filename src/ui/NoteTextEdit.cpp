#include "ui/NoteTextEdit.h"

#include "editor/NoteFormat.h"

#include <QEvent>
#include <QKeyEvent>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFrame>

namespace notes::ui {

NoteTextEdit::NoteTextEdit(QWidget* parent)
    : QTextEdit(parent)
{
    // Pasted HTML would bring formatting the plain editor can't show or save
    // yet; rich text support comes with the Phase 3 document model.
    setAcceptRichText(false);
    setFrameShape(QFrame::NoFrame);
    setTabChangesFocus(false);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
}

NoteTextEdit::~NoteTextEdit() = default;

void NoteTextEdit::setTextPadding(int right, int bottom)
{
    m_rightPadding = right;
    m_bottomPadding = bottom;
    updateViewportMargins();
    applyBottomPadding();
}

int NoteTextEdit::rightInset() const
{
    return m_rightPadding + verticalScrollBar()->sizeHint().width();
}

void NoteTextEdit::setNoteBody(const QString& body, const QString& plainText)
{
    editor::NoteFormat::load(body, plainText, *document());
    if (document()->isEmpty()) {
        // A new (empty) note starts with its title, as in macOS Notes (task
        // 3.3): typing goes into a Title paragraph, and Enter then gives Body.
        QTextBlockFormat title;
        title.setHeadingLevel(editor::StyleManager::headingLevel(editor::ParagraphStyle::Title));
        QTextCursor(document()).setBlockFormat(title);
        editor::StyleManager::styleHeadings(*document());
    }
    // Loading isn't an edit: start the note with no undo history.
    document()->clearUndoRedoStacks();
    applyBottomPadding();
    // Filled directly rather than through setPlainText(), an empty document's
    // only line was left without a layout, and Qt draws no caret there: a new
    // note showed no caret until the first key. Lay it all out again.
    document()->markContentsDirty(0, document()->characterCount());
}

QString NoteTextEdit::noteBody() const
{
    return editor::NoteFormat::serialize(*document());
}

editor::ParagraphStyle NoteTextEdit::paragraphStyle() const
{
    const QTextCursor cursor = textCursor();
    return editor::StyleManager::styleOf(document()->findBlock(cursor.selectionStart()));
}

void NoteTextEdit::setParagraphStyle(editor::ParagraphStyle style)
{
    if (isReadOnly()) {
        return;
    }
    QTextCursor cursor = textCursor();
    editor::StyleManager::apply(cursor, style);
    setTextCursor(cursor);
}

bool NoteTextEdit::inlineStyle(editor::InlineStyle style) const
{
    return editor::StyleManager::hasInline(currentCharFormat(), style);
}

bool NoteTextEdit::canSetInlineStyle(editor::InlineStyle style) const
{
    if (style != editor::InlineStyle::Bold) {
        return true;
    }
    // Bold if any selected paragraph isn't a heading (those are skipped).
    const QTextCursor cursor = textCursor();
    const QTextBlock last = document()->findBlock(cursor.selectionEnd());
    for (QTextBlock block = document()->findBlock(cursor.selectionStart()); block.isValid();
         block = block.next()) {
        if (block.blockFormat().headingLevel() == 0) {
            return true;
        }
        if (block == last) {
            break;
        }
    }
    return false;
}

void NoteTextEdit::setInlineStyle(editor::InlineStyle style, bool on)
{
    if (isReadOnly() || !canSetInlineStyle(style)) {
        return;
    }
    QTextCursor cursor = textCursor();
    if (cursor.hasSelection()) {
        editor::StyleManager::setInline(cursor, style, on);
    } else {
        mergeCurrentCharFormat(editor::StyleManager::inlineFormat(style, on));
    }
}

void NoteTextEdit::keyPressEvent(QKeyEvent* event)
{
    const bool enter = event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter;
    QTextCursor cursor = textCursor();
    if (enter && !isReadOnly() && event->modifiers() == Qt::NoModifier &&
        cursor.blockFormat().headingLevel() > 0) {
        cursor.beginEditBlock();
        cursor.removeSelectedText();
        if (cursor.atBlockEnd()) {
            // A new paragraph without the heading's level or font. Keeps what
            // isn't the heading's own (indent, alignment).
            QTextBlockFormat body = cursor.blockFormat();
            body.setHeadingLevel(0);
            body.clearProperty(editor::NoteFormat::kCollapsedProperty);
            cursor.insertBlock(body, QTextCharFormat());
        } else {
            // Split: both halves stay headings. (Qt's own Enter drops the
            // level but not the font, leaving Body that looks like a heading.)
            cursor.insertBlock(cursor.blockFormat(), cursor.charFormat());
        }
        cursor.endEditBlock();
        setTextCursor(cursor);
        ensureCursorVisible();
        return;
    }
    QTextEdit::keyPressEvent(event);
}

void NoteTextEdit::applyBottomPadding()
{
    // On the document's root frame, so it scrolls with the text: space after
    // the last line, not a band that hides text while scrolling.
    // Not an edit: no undo step (a freshly opened note has no undo history).
    // Turning undo off and on also clears it, which setPlainText() did anyway.
    QTextDocument* doc = document();
    const bool undo = doc->isUndoRedoEnabled();
    doc->setUndoRedoEnabled(false);
    QTextFrame* root = doc->rootFrame();
    QTextFrameFormat format = root->frameFormat();
    format.setBottomMargin(doc->documentMargin() + m_bottomPadding);
    root->setFrameFormat(format);
    doc->setUndoRedoEnabled(undo);
}

void NoteTextEdit::changeEvent(QEvent* event)
{
    QTextEdit::changeEvent(event);
    if (event->type() == QEvent::StyleChange) {
        updateViewportMargins(); // the scroll bar's width may have changed
    }
}

void NoteTextEdit::updateViewportMargins()
{
    setViewportMargins(0, 0, m_rightPadding, 0);
}

} // namespace notes::ui
