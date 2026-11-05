#pragma once

#include "editor/ImageImport.h"
#include "editor/StyleManager.h"
#include "models/Attachment.h"

#include <QHash>
#include <QList>
#include <QStringList>
#include <QTextBlock>
#include <QTextEdit>

#include <optional>

class QPainter;

namespace notes::editor {
class ImageObject;
}

namespace notes::ui {

// The note body editor (Plan §3.5): paragraph styles (task 3.2), inline
// formats (3.4), lists (3.5), checklists (3.6) and images (3.7); tables
// arrive later in Phase 3.
//
// Images are attachments (models::Attachment) the document shows as
// `attachment://<uuid>` images. The editor holds the note's attachments with
// their data, from opening the note and from every image added since, and
// draws them from there (loadResource()); nothing is ever loaded from a file
// or the network. Saving stores the ones the body still shows
// (NoteService::updateBody()); keeping the data here, an image deleted and
// brought back with Undo after a save is stored again.
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

    // Copied or dragged selections with images carry them in this format: the
    // selection in the note format plus its images' data, so they paste into
    // any note as images of its own (new uuids).
    static constexpr const char* kFragmentMime = "application/x-notesapp-fragment";

    // A stored body (editor::NoteFormat::load()), keeping the bottom padding,
    // with the note's attachments. No undo history. An empty note starts in
    // the Title style (task 3.3).
    void setNoteBody(const QString& body, const QString& plainText,
                     const QList<models::Attachment>& attachments = {});
    // The document in the note format, for saving.
    [[nodiscard]] QString noteBody() const;
    // The plain text, for the title, snippet and search: without the
    // characters standing for images.
    [[nodiscard]] QString notePlainText() const;
    // The images the document shows, in document order, each once, with the
    // editor's copy of their data (an empty one for an image it has none for).
    [[nodiscard]] QList<models::Attachment> attachments() const;

    // Image files (the file picker), inserted at the cursor in one undo step.
    // Files that aren't images, or are over the limits, are left out, and
    // imageRejected() says why. Does nothing while read-only.
    void insertImageFiles(const QStringList& paths);

    // The style of the paragraph at the cursor (the first one selected).
    [[nodiscard]] editor::ParagraphStyle paragraphStyle() const;
    // Gives the selected paragraphs `style` (StyleManager::apply()): one undo
    // step. Does nothing while read-only.
    void setParagraphStyle(editor::ParagraphStyle style);

    // Whether the selected text all has `style`, or with nothing selected,
    // whether typing would produce it.
    [[nodiscard]] bool inlineStyle(editor::InlineStyle style) const;
    // Turns `style` on or off for the selection (one undo step), or, with
    // nothing selected, for the text typed next. Bold can't be changed in a
    // heading, which is bold by its style. Does nothing while read-only.
    void setInlineStyle(editor::InlineStyle style, bool on);
    // Whether `style` can be changed at the cursor (Bold: not in a heading).
    [[nodiscard]] bool canSetInlineStyle(editor::InlineStyle style) const;

    // Ctrl+Shift+L (task 3.6): the selected paragraphs become unchecked
    // checklist items, or Body again when they all are checklist items
    // already. One undo step; does nothing while read-only.
    void toggleChecklist();
    // Whether checking an item moves it below the last item of its checklist
    // (the "Move checked items to bottom" setting, Plan §3.5). Off by default.
    void setMoveCheckedToBottom(bool move);
    [[nodiscard]] bool moveCheckedToBottom() const { return m_moveCheckedToBottom; }
    // Where the checkbox of `block` is drawn, in viewport coordinates; empty
    // when `block` isn't a checklist item or isn't laid out.
    [[nodiscard]] QRectF checkboxRect(const QTextBlock& block) const;

signals:
    // An image wasn't added (too large, unreadable); `message` is for the user.
    void imageRejected(const QString& message);

protected:
    void changeEvent(QEvent* event) override;
    // Only the note's own attachments; nothing from files or the network.
    QVariant loadResource(int type, const QUrl& name) override;
    [[nodiscard]] bool canInsertFromMimeData(const QMimeData* source) const override;
    [[nodiscard]] QMimeData* createMimeDataFromSelection() const override;
    void insertFromMimeData(const QMimeData* source) override;
    void keyPressEvent(QKeyEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;

private:
    // Inserts the images `source` holds (a fragment of ours, image files, an
    // image), or reports why they were refused. False if it holds none, and
    // the text is pasted as usual.
    bool insertImages(const QMimeData* source);
    // `results` at the cursor, as one undo step; the first error is reported.
    void insertImported(const QList<editor::ImageImport::Result>& results);
    // A kFragmentMime fragment at the cursor, its images under new uuids.
    bool insertFragment(const QByteArray& encoded);

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
    // The markers Qt can't draw: the dashes of dashed lists and checkboxes.
    void paintMarkers();
    // The checklist item whose checkbox is at `position` (viewport
    // coordinates), if any.
    [[nodiscard]] QTextBlock checkboxAt(QPointF position) const;
    void paintCheckbox(QPainter& painter, const QRectF& box, bool checked) const;
    // Checked items' text is grayed: drawn so, not stored (extra selections).
    void updateCheckedText();
    // Updates the gray text after the current change, from the event loop.
    // Never from inside the document's signals: QTextDocument::clear() emits
    // contentsChanged() while it holds its cursor list aside, and replacing
    // the extra selections there freed cursors it then put back (heap
    // corruption, the app aborted when switching from a note with checked
    // items).
    void scheduleCheckedText();

    void updateViewportMargins();
    void applyBottomPadding();

    editor::ImageObject* m_imageObject;
    // The note's attachments by uuid: loaded with it, and every one added since.
    QHash<QString, models::Attachment> m_attachments;

    int m_rightPadding = 0;
    int m_bottomPadding = 0;
    bool m_moveCheckedToBottom = false;
    bool m_overCheckbox = false; // the pointer shows a hand there
    bool m_checkedTextPending = false;
    // The checkbox a left button press went to: the click is ours from press
    // to release (Qt sees none of it), and toggles if released on it too.
    QTextBlock m_pressedCheckbox;
};

} // namespace notes::ui
