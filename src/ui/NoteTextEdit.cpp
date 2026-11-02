#include "ui/NoteTextEdit.h"

#include <QEvent>
#include <QScrollBar>
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

void NoteTextEdit::setNoteText(const QString& text)
{
    setPlainText(text);
    applyBottomPadding();
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
