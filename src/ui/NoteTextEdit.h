#pragma once

#include "editor/StyleManager.h"

#include <QTextEdit>

namespace notes::ui {

// The note body editor (Plan §3.5): paragraph styles (task 3.2); lists, tables
// and images arrive later in Phase 3.
class NoteTextEdit : public QTextEdit
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(NoteTextEdit)

public:
    explicit NoteTextEdit(QWidget* parent = nullptr);
    ~NoteTextEdit() override;

    // Space around the text (UI review: the margins should look even). `right`
    // is between the text and the vertical scroll bar, which sits at the
    // widget's right edge and always has its room reserved, so the text doesn't
    // re-wrap when a note becomes long enough to scroll. `bottom` is below the
    // last line, reached by scrolling to the end.
    void setTextPadding(int right, int bottom);
    // From the text to the widget's right edge: the padding plus the scroll bar.
    [[nodiscard]] int rightInset() const;

    // A stored body (editor::NoteFormat::load()), keeping the bottom padding.
    // No undo history. An empty note starts in the Title style (task 3.3).
    void setNoteBody(const QString& body, const QString& plainText);
    // The document in the note format, for saving.
    [[nodiscard]] QString noteBody() const;

    // The style of the paragraph at the cursor (the first one selected).
    [[nodiscard]] editor::ParagraphStyle paragraphStyle() const;
    // Gives the selected paragraphs `style` (StyleManager::apply()): one undo
    // step. Does nothing while read-only.
    void setParagraphStyle(editor::ParagraphStyle style);

protected:
    void changeEvent(QEvent* event) override;
    // Enter at the end of a heading starts a Body paragraph, as in macOS Notes;
    // elsewhere in a heading it splits it into two headings.
    void keyPressEvent(QKeyEvent* event) override;

private:
    void updateViewportMargins();
    void applyBottomPadding();

    int m_rightPadding = 0;
    int m_bottomPadding = 0;
};

} // namespace notes::ui
