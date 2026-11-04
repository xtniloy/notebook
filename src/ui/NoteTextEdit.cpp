#include "ui/NoteTextEdit.h"

#include "editor/NoteFormat.h"

#include <QAbstractTextDocumentLayout>
#include <QEvent>
#include <QFontMetricsF>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFrame>
#include <QTextLayout>
#include <QTextList>

#include <algorithm>
#include <array>
#include <cmath>

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
    connect(document(), &QTextDocument::contentsChanged, this, &NoteTextEdit::scheduleCheckedText);
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
    m_pressedCheckbox = QTextBlock(); // a click doesn't carry over to another note
    // No cursors of ours in the document while it's cleared and refilled.
    setExtraSelections({});
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
    updateCheckedText(); // grayed from the start, not a moment later
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
    const QTextCursor cursor = textCursor();
    if (cursor.hasSelection()) {
        // The whole selection's, not the one character before the cursor
        // (which, selected from the end, is outside it).
        return editor::StyleManager::selectionHas(cursor, style);
    }
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

void NoteTextEdit::toggleChecklist()
{
    if (isReadOnly()) {
        return;
    }
    QTextCursor cursor = textCursor();
    bool allItems = true;
    const QTextBlock last = document()->findBlock(cursor.selectionEnd());
    for (QTextBlock block = document()->findBlock(cursor.selectionStart()); block.isValid();
         block = block.next()) {
        allItems = allItems && editor::StyleManager::isChecklistItem(block);
        if (block == last) {
            break;
        }
    }
    editor::StyleManager::apply(cursor, allItems ? editor::ParagraphStyle::Body
                                                 : editor::ParagraphStyle::Checklist);
    setTextCursor(cursor);
}

void NoteTextEdit::setMoveCheckedToBottom(bool move)
{
    m_moveCheckedToBottom = move;
}

void NoteTextEdit::insertFromMimeData(const QMimeData* source)
{
    const int start = textCursor().selectionStart();
    const QTextBlockFormat startFormat = document()->findBlock(start).blockFormat();
    const int revision = document()->revision();
    QTextEdit::insertFromMimeData(source);
    if (isReadOnly() || document()->revision() == revision) {
        return;
    }
    // Pasted or dropped plain text takes the paragraph format of its own
    // first line and the font at the cursor: pasted into a new note's title,
    // every line became Body in the title's size and bold. The paragraph
    // pasted into keeps its style, the lines after it are Body, and each
    // gets its style's font, in the same undo step.
    QTextCursor cursor(document());
    cursor.setPosition(start);
    cursor.setPosition(textCursor().position(), QTextCursor::KeepAnchor);
    cursor.joinPreviousEditBlock();
    QTextCursor(document()->findBlock(start)).setBlockFormat(startFormat);
    editor::StyleManager::fitText(cursor);
    cursor.endEditBlock();
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
        body.clearProperty(QTextFormat::HeadingLevel);
        body.clearProperty(editor::NoteFormat::kCollapsedProperty);
        editor::StyleManager::clearDefaults(body);
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
        if (modifiers == Qt::NoModifier && editor::StyleManager::isChecklistItem(cursor.block()) &&
            !cursor.block().text().isEmpty()) {
            // The next checklist item starts unchecked, even after a checked
            // one (Qt's Enter would copy the check).
            cursor.beginEditBlock();
            cursor.removeSelectedText();
            QTextBlockFormat item = cursor.blockFormat();
            item.clearProperty(editor::NoteFormat::kCheckedProperty);
            cursor.insertBlock(item, cursor.charFormat());
            cursor.endEditBlock();
            setTextCursor(cursor);
            return true;
        }
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
    paintMarkers();
}

namespace {

// The font a list item's marker is drawn in: its paragraph's.
QFont markerFont(const QTextBlock& block, const QTextDocument& document)
{
    return block.charFormat().font().resolve(document.defaultFont());
}

} // namespace

QRectF NoteTextEdit::checkboxRect(const QTextBlock& block) const
{
    if (!editor::StyleManager::isChecklistItem(block) || block.layout() == nullptr ||
        block.layout()->lineCount() == 0) {
        return {};
    }
    const QTextLine line = block.layout()->lineAt(0);
    const QPointF offset(-horizontalScrollBar()->value(), -verticalScrollBar()->value());
    const QRectF bounds = document()->documentLayout()->blockBoundingRect(block).translated(offset);
    const QFontMetricsF metrics(markerFont(block, *document()), viewport());
    // A circle a little taller than the capitals, centered on them, a space
    // before the text (in the room Qt leaves for a list's marker).
    const qreal size = std::round(metrics.height() * 0.82);
    const qreal centerY = bounds.top() + line.y() + line.ascent() - (metrics.capHeight() / 2);
    const qreal right = bounds.left() + line.x() - metrics.horizontalAdvance(u' ');
    return {right - size, centerY - (size / 2), size, size};
}

void NoteTextEdit::paintMarkers()
{
    // Qt draws no marker for a dashed list (it has no dash style) or a
    // checklist (ours has none either, to draw the checkboxes ourselves).
    QPainter painter(viewport());
    painter.setRenderHint(QPainter::Antialiasing);
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
        if (list == nullptr || block.layout() == nullptr || block.layout()->lineCount() == 0) {
            continue;
        }
        if (editor::StyleManager::isChecklistItem(block)) {
            paintCheckbox(painter, checkboxRect(block), editor::StyleManager::isChecked(block));
            continue;
        }
        if (!list->format().boolProperty(editor::NoteFormat::kDashedListProperty)) {
            continue;
        }
        // An en dash where Qt draws a bullet, right-aligned before the text.
        const QTextLine line = block.layout()->lineAt(0);
        const QFont font = markerFont(block, *document());
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

void NoteTextEdit::paintCheckbox(QPainter& painter, const QRectF& box, bool checked) const
{
    const qreal size = box.width();
    if (!checked) {
        // An empty circle, as in macOS Notes.
        const qreal width = std::max(1.0, size / 12);
        painter.setPen(QPen(palette().color(QPalette::PlaceholderText), width));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(box.adjusted(width / 2, width / 2, -width / 2, -width / 2));
        return;
    }
    // Filled in the accent, with a check mark.
    painter.setPen(Qt::NoPen);
    painter.setBrush(palette().color(QPalette::Accent));
    painter.drawEllipse(box);
    QPen mark(palette().color(QPalette::HighlightedText), std::max(1.5, size / 9));
    mark.setCapStyle(Qt::RoundCap);
    mark.setJoinStyle(Qt::RoundJoin);
    painter.setPen(mark);
    painter.setBrush(Qt::NoBrush);
    const auto at = [&](qreal x, qreal y) {
        return QPointF(box.left() + (x * size), box.top() + (y * size));
    };
    const std::array points{at(0.29, 0.52), at(0.44, 0.67), at(0.72, 0.36)};
    painter.drawPolyline(points.data(), static_cast<int>(points.size()));
}

QTextBlock NoteTextEdit::checkboxAt(QPointF position) const
{
    const QTextBlock block = cursorForPosition(position.toPoint()).block();
    // A little larger than drawn, to be easy to hit, but short of the text.
    constexpr qreal kSlop = 2;
    if (checkboxRect(block).adjusted(-kSlop, -kSlop, kSlop, kSlop).contains(position)) {
        return block;
    }
    return {};
}

void NoteTextEdit::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && !isReadOnly()) {
        m_pressedCheckbox = checkboxAt(event->position());
        if (m_pressedCheckbox.isValid()) {
            event->accept(); // the caret stays where it was, as in macOS Notes
            return;
        }
    }
    QTextEdit::mousePressEvent(event);
}

void NoteTextEdit::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_pressedCheckbox.isValid()) {
        // A click: pressed and released on the same checkbox. One undo step.
        if (checkboxAt(event->position()) == m_pressedCheckbox && !isReadOnly()) {
            editor::StyleManager::setChecked(m_pressedCheckbox,
                                             !editor::StyleManager::isChecked(m_pressedCheckbox),
                                             m_moveCheckedToBottom);
        }
        m_pressedCheckbox = QTextBlock();
        event->accept();
        return;
    }
    QTextEdit::mouseReleaseEvent(event);
}

void NoteTextEdit::mouseDoubleClickEvent(QMouseEvent* event)
{
    // The second press of a double click: a second click on the checkbox
    // (toggled on its release), not a word to select.
    if (event->button() == Qt::LeftButton && !isReadOnly()) {
        m_pressedCheckbox = checkboxAt(event->position());
        if (m_pressedCheckbox.isValid()) {
            event->accept();
            return;
        }
    }
    QTextEdit::mouseDoubleClickEvent(event);
}

void NoteTextEdit::mouseMoveEvent(QMouseEvent* event)
{
    if (m_pressedCheckbox.isValid()) {
        event->accept(); // pressed on a checkbox: no text selection
        return;
    }
    QTextEdit::mouseMoveEvent(event);
    const bool over = event->buttons() == Qt::NoButton && !isReadOnly() &&
                      checkboxAt(event->position()).isValid();
    if (over != m_overCheckbox) {
        m_overCheckbox = over;
        viewport()->setCursor(over ? Qt::PointingHandCursor : Qt::IBeamCursor);
    }
}

void NoteTextEdit::scheduleCheckedText()
{
    if (m_checkedTextPending) {
        return;
    }
    m_checkedTextPending = true;
    QMetaObject::invokeMethod(
        this,
        [this] {
            m_checkedTextPending = false;
            updateCheckedText();
        },
        Qt::QueuedConnection);
}

void NoteTextEdit::updateCheckedText()
{
    QTextCharFormat gray;
    gray.setForeground(palette().color(QPalette::PlaceholderText));
    QList<ExtraSelection> selections;
    for (QTextBlock block = document()->begin(); block.isValid(); block = block.next()) {
        if (block.length() > 1 && editor::StyleManager::isChecked(block)) {
            QTextCursor text(block);
            text.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
            selections.append({.cursor = text, .format = gray});
        }
    }
    setExtraSelections(selections);
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
    if (event->type() == QEvent::PaletteChange) {
        updateCheckedText(); // the theme's gray
    }
}

void NoteTextEdit::updateViewportMargins()
{
    setViewportMargins(0, 0, m_rightPadding, 0);
}

} // namespace notes::ui
