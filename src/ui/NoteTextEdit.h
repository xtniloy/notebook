#pragma once

#include "editor/StyleManager.h"

#include <QTextEdit>

#include <optional>

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

    // Whether the text at the cursor (what typing would produce) has `style`.
    [[nodiscard]] bool inlineStyle(editor::InlineStyle style) const;
    // Turns `style` on or off for the selection (one undo step), or, with
    // nothing selected, for the text typed next. Bold can't be changed in a
    // heading, which is bold by its style. Does nothing while read-only.
    void setInlineStyle(editor::InlineStyle style, bool on);
    // Whether `style` can be changed at the cursor (Bold: not in a heading).
    [[nodiscard]] bool canSetInlineStyle(editor::InlineStyle style) const;

protected:
    void changeEvent(QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    // Enter in a heading: at its end a Body paragraph follows, as in macOS
    // Notes; elsewhere it splits into two headings. True if handled.
    bool headingKey(const QKeyEvent* event);
    // Lists (task 3.5): Tab / Shift+Tab nest and un-nest items, Enter on an
    // empty item goes up a level (or out of the list), Backspace at the start
    // of an item removes its marker. True if handled.
    bool listKey(const QKeyEvent* event);
    // The list a Space would start: "- " dashed, "* " bulleted, "1. "
    // numbered, typed at the start of a Body paragraph.
    [[nodiscard]] std::optional<editor::ParagraphStyle> listMarker(const QKeyEvent* event) const;
    // The dashes of dashed lists, which Qt can't draw.
    void paintDashes();

    void updateViewportMargins();
    void applyBottomPadding();

    int m_rightPadding = 0;
    int m_bottomPadding = 0;
};

} // namespace notes::ui
