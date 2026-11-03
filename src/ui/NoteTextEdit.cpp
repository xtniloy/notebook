#include "ui/NoteTextEdit.h"

#include "editor/NoteFormat.h"

#include <QAbstractTextDocumentLayout>
#include <QEvent>
#include <QFontMetricsF>
#include <QKeyEvent>
#include <QPainter>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFrame>
#include <QTextLayout>
#include <QTextList>

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
    if (!isReadOnly() && (headingKey(event) || listKey(event))) {
        event->accept();
        ensureCursorVisible();
        return;
    }
    // "- ", "* " or "1. " typed at the start of a paragraph (task 3.5).
    const std::optional<editor::ParagraphStyle> list =
        isReadOnly() ? std::nullopt : listMarker(event);
    QTextEdit::keyPressEvent(event);
    if (list) {
        // A separate undo step from typing the space: Ctrl+Z gives the marker
        // back as text, as in macOS Notes.
        QTextCursor cursor = textCursor();
        cursor.beginEditBlock();
        cursor.movePosition(QTextCursor::StartOfBlock, QTextCursor::KeepAnchor);
        cursor.removeSelectedText();
        editor::StyleManager::apply(cursor, *list);
        cursor.endEditBlock();
        setTextCursor(cursor);
    }
}

bool NoteTextEdit::headingKey(const QKeyEvent* event)
{
    const bool enter = event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter;
    QTextCursor cursor = textCursor();
    if (!enter || event->modifiers() != Qt::NoModifier ||
        cursor.blockFormat().headingLevel() == 0) {
        return false;
    }
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
    return true;
}

bool NoteTextEdit::listKey(const QKeyEvent* event)
{
    QTextCursor cursor = textCursor();
    const Qt::KeyboardModifiers modifiers = event->modifiers() & ~Qt::KeypadModifier;
    switch (event->key()) {
    case Qt::Key_Tab:
        return modifiers == Qt::NoModifier && editor::StyleManager::indentList(cursor, 1);
    case Qt::Key_Backtab: // Shift+Tab
        return editor::StyleManager::indentList(cursor, -1);
    case Qt::Key_Return:
    case Qt::Key_Enter:
        // Enter on an empty item: up a level, or out of the list from the top.
        if (modifiers != Qt::NoModifier || cursor.hasSelection() ||
            cursor.currentList() == nullptr || !cursor.block().text().isEmpty()) {
            return false;
        }
        if (cursor.currentList()->format().indent() > 1) {
            editor::StyleManager::indentList(cursor, -1);
        } else {
            editor::StyleManager::removeFromList(cursor);
        }
        return true;
    case Qt::Key_Backspace:
        // At the start of an item, the marker goes first; the text stays.
        if (modifiers != Qt::NoModifier || cursor.hasSelection() || !cursor.atBlockStart() ||
            cursor.currentList() == nullptr) {
            return false;
        }
        editor::StyleManager::removeFromList(cursor);
        return true;
    default:
        return false;
    }
}

std::optional<editor::ParagraphStyle> NoteTextEdit::listMarker(const QKeyEvent* event) const
{
    if (event->key() != Qt::Key_Space || event->modifiers() != Qt::NoModifier) {
        return std::nullopt;
    }
    const QTextCursor cursor = textCursor();
    const QTextBlock block = cursor.block();
    if (cursor.hasSelection() || block.textList() != nullptr ||
        editor::StyleManager::styleOf(block) != editor::ParagraphStyle::Body) {
        return std::nullopt; // headings and code stay as typed
    }
    const QString marker = block.text().left(cursor.positionInBlock());
    if (marker == u"-") {
        return editor::ParagraphStyle::DashedList;
    }
    if (marker == u"*") {
        return editor::ParagraphStyle::BulletedList;
    }
    if (marker == u"1.") {
        return editor::ParagraphStyle::NumberedList;
    }
    return std::nullopt;
}

void NoteTextEdit::paintEvent(QPaintEvent* event)
{
    QTextEdit::paintEvent(event);
    paintDashes();
}

void NoteTextEdit::paintDashes()
{
    // Qt draws no marker for a dashed list (it has no dash style): draw an en
    // dash where Qt draws a bullet, right-aligned before the text.
    QPainter painter(viewport());
    const QPoint offset(-horizontalScrollBar()->value(), -verticalScrollBar()->value());
    const QAbstractTextDocumentLayout* layout = document()->documentLayout();
    const QRect visible = viewport()->rect();
    for (QTextBlock block = cursorForPosition(QPoint(0, 0)).block(); block.isValid();
         block = block.next()) {
        const QRectF bounds = layout->blockBoundingRect(block).translated(offset);
        if (bounds.top() > visible.bottom()) {
            break;
        }
        const QTextList* list = block.textList();
        if (list == nullptr ||
            !list->format().boolProperty(editor::NoteFormat::kDashedListProperty) ||
            block.layout() == nullptr || block.layout()->lineCount() == 0) {
            continue;
        }
        const QTextLine line = block.layout()->lineAt(0);
        const QFont font = block.charFormat().font().resolve(document()->defaultFont());
        const QFontMetricsF metrics(font, viewport());
        const QString dash(QChar(0x2013));
        const qreal right = bounds.left() + line.x() - metrics.horizontalAdvance(u' ');
        painter.setFont(font);
        painter.setPen(palette().color(QPalette::Text));
        painter.drawText(QPointF(right - metrics.horizontalAdvance(dash),
                                 bounds.top() + line.y() + line.ascent()),
                         dash);
    }
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
