#include "ui/NoteTextEdit.h"

#include <QEvent>
#include <QScrollBar>

#include <algorithm>

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

void NoteTextEdit::setRightPadding(int padding)
{
    m_rightPadding = padding;
    updateViewportMargins();
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
    const int scrollBar = verticalScrollBar()->sizeHint().width();
    setViewportMargins(0, 0, std::max(0, m_rightPadding - scrollBar), 0);
}

} // namespace notes::ui
