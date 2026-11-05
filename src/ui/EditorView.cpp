#include "ui/EditorView.h"

#include "ui/NoteListDelegate.h"
#include "ui/NoteTextEdit.h"

#include <QDateTime>
#include <QLabel>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace notes::ui {

namespace {

constexpr int kHeaderTopMargin = 10;
constexpr int kSideMargin = 24;
// Between the scroll bar and the pane's right edge (UI review: flush against
// the edge looked cramped).
constexpr int kScrollBarGap = 3;

QLabel* createMutedLabel(const QString& objectName, QWidget* parent)
{
    auto* label = new QLabel(parent);
    label->setObjectName(objectName);
    label->setAlignment(Qt::AlignCenter);
    // Light gray; real theme colors arrive with task 1.8.
    label->setForegroundRole(QPalette::PlaceholderText);
    return label;
}

} // namespace

EditorView::EditorView(QWidget* parent)
    : QWidget(parent)
    , m_header(createMutedLabel(QStringLiteral("editorHeader"), this))
    , m_pages(new QStackedWidget(this))
    , m_textEdit(new NoteTextEdit(m_pages))
    , m_placeholder(createMutedLabel(QStringLiteral("editorPlaceholder"), m_pages))
{
    // One page: the margins around the text get the text background too (Base
    // in the palette; the style sheet's #editorPane rule needs WA_StyledBackground).
    setBackgroundRole(QPalette::Base);
    setAutoFillBackground(true);
    setAttribute(Qt::WA_StyledBackground);

    // The date line matches the time under each title in the note list.
    m_header->setFont(NoteListDelegate::detailFont(m_header->font()));

    m_textEdit->setObjectName(QStringLiteral("noteTextEdit"));
    m_pages->addWidget(m_textEdit);
    m_pages->addWidget(m_placeholder);

    // Even margins around the text (UI review): kSideMargin on the left, the
    // same between the text and the scroll bar on the right, and below the last
    // line. The text edit reaches almost to the pane's edge, so its scroll bar
    // sits there. The labels get the same right inset, to stay centered over
    // the text. The text edit itself starts kTextBleed earlier and keeps that
    // much inside, as room for code backgrounds.
    m_textEdit->setTextPadding(kSideMargin, kSideMargin);
    m_header->setContentsMargins(0, 0, m_textEdit->rightInset(), 0);
    m_placeholder->setContentsMargins(0, 0, m_textEdit->rightInset(), 0);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(kSideMargin - NoteTextEdit::kTextBleed, kHeaderTopMargin,
                               kScrollBarGap, 0);
    layout->setSpacing(kHeaderTopMargin);
    layout->addWidget(m_header);
    layout->addWidget(m_pages, 1);

    clear();
}

EditorView::~EditorView() = default;

void EditorView::showNote(const QString& text, const QDateTime& modified)
{
    showNoteBody({}, text, modified);
}

void EditorView::showNoteBody(const QString& body, const QString& plainText,
                              const QDateTime& modified,
                              const QList<models::Attachment>& attachments)
{
    m_header->setText(headerText(modified));
    m_header->show();
    m_textEdit->setNoteBody(body, plainText, attachments); // no undo history
    m_textEdit->setReadOnly(false);
    m_pages->setCurrentWidget(m_textEdit);
    m_hasNote = true;
}

void EditorView::showLockedNote(const QDateTime& modified)
{
    m_header->setText(headerText(modified));
    m_header->show();
    showPlaceholder(tr("This note is locked."));
    m_hasNote = true;
}

void EditorView::clear()
{
    m_header->clear();
    m_header->hide();
    showPlaceholder(tr("No note selected"));
    m_hasNote = false;
}

void EditorView::showPlaceholder(const QString& message)
{
    m_textEdit->clear();
    m_textEdit->setReadOnly(true);
    m_placeholder->setText(message);
    m_pages->setCurrentWidget(m_placeholder);
}

QString EditorView::headerText(const QDateTime& modified, const QLocale& locale)
{
    //: Editor header, e.g. "Thursday, October 28, 2026 at 9:41 PM".
    return tr("%1 at %2")
        .arg(locale.toString(modified.date(), QLocale::LongFormat),
             locale.toString(modified.time(), QLocale::ShortFormat));
}

} // namespace notes::ui
