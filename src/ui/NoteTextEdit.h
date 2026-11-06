#pragma once

#include "editor/ImageImport.h"
#include "editor/StyleManager.h"
#include "models/Attachment.h"

#include <QHash>
#include <QList>
#include <QStringList>
#include <QTextBlock>
#include <QTextEdit>

#include <cstdint>
#include <functional>
#include <optional>

class QAction;
class QPainter;

namespace notes::editor {
class ImageObject;
}

namespace notes::ui {

// The note body editor (Plan §3.5): paragraph styles (task 3.2), inline
// formats (3.4), lists (3.5), checklists (3.6), images (3.7) and safe paste
// (3.8), links (3.9) and code drawn on a gray background; tables arrive later.
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
    // Room around the text inside the editor for what's drawn behind it
    // (code backgrounds reach out this far): part of the left margin, of
    // setTextPadding()'s `right`, and the same again on each side.
    static constexpr int kTextBleed = 8;
    // From the text to the widget's right edge: the padding plus the scroll bar.
    [[nodiscard]] int rightInset() const;

    // Copied or dragged selections carry this format too: the selection in
    // the note format plus its images' data, so they paste into any note with
    // every style kept (checklists included) and images of their own (new
    // uuids).
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

    // Pasting (task 3.8). Ctrl+V keeps what a note can show of the
    // clipboard, cleaned (editor::PasteCleaner): HTML from other apps, or
    // plain text that clearly is Markdown. These two act on the editor only
    // (their shortcuts work while it has focus) and are in its context menu;
    // the window's Edit menu shows them too.
    // Ctrl+Shift+V: the clipboard's plain text, as it is.
    [[nodiscard]] QAction* pasteAsPlainTextAction() const { return m_pasteAsPlainText; }
    // Ctrl+Alt+V: the clipboard's text read as Markdown, even when it doesn't
    // look like it.
    [[nodiscard]] QAction* pasteAsMarkdownAction() const { return m_pasteAsMarkdown; }

    // The style of the paragraph at the cursor (the first one selected).
    [[nodiscard]] editor::ParagraphStyle paragraphStyle() const;
    // Gives the selected paragraphs `style` (StyleManager::apply()): one undo
    // step. Monospaced turns on and off: a few words of one paragraph become
    // inline code or plain again (StyleManager::toggleInlineMonospace());
    // after inline code, with nothing selected, typing goes on in plain text;
    // code blocks become Body again. Does nothing while read-only.
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

    // Links (task 3.9). A web or e-mail address typed and followed by a
    // space, Enter or Tab becomes a link (Ctrl+Z gives the plain text back).
    // Links are drawn in the theme's link color, underlined (drawn so, not
    // stored), and Ctrl+Click opens them, only http, https and mailto ones
    // (editor::Links::isAllowed()); nothing else is ever opened.
    // How a link is opened: QDesktopServices::openUrl() unless replaced
    // (tests). Only ever called with an allowed link.
    using LinkOpener = std::function<bool(const QUrl& url)>;
    void setLinkOpener(LinkOpener opener) { m_linkOpener = std::move(opener); }
    // Opens `href` if it's allowed; false (and nothing opened) if it isn't.
    bool openLink(const QString& href);

signals:
    // An image wasn't added (too large, unreadable); `message` is for the user.
    void imageRejected(const QString& message);

protected:
    void changeEvent(QEvent* event) override;
    // Only the note's own attachments; nothing from files or the network.
    QVariant loadResource(int type, const QUrl& name) override;
    [[nodiscard]] bool canInsertFromMimeData(const QMimeData* source) const override;
    [[nodiscard]] QMimeData* createMimeDataFromSelection() const override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void insertFromMimeData(const QMimeData* source) override;
    void keyPressEvent(QKeyEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    // The link's address and how to open it, as a tooltip.
    bool viewportEvent(QEvent* event) override;

private:
    enum class PasteMode : std::uint8_t {
        Auto,      // Ctrl+V: rich when the clipboard has it
        PlainText, // Ctrl+Shift+V
        Markdown,  // Ctrl+Alt+V
    };
    void pasteAs(PasteMode mode);
    // Cleaned HTML, or Markdown that looks like it. False if there is
    // neither (or nothing is left of it): the plain text is pasted instead.
    bool insertRichText(const QMimeData* source);
    // A note body (PasteCleaner's) at the cursor; false if empty or unreadable.
    bool insertBody(const QString& body);
    void insertDocument(const QTextDocument& pasted);

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
    // Enter in a code block: the next line is code too, empty lines
    // included. (Qt's own Enter on an empty line resets its format instead,
    // as the panel's spacing isn't the default.) True if handled.
    bool codeKey(const QKeyEvent* event);
    // A key as the editor handles it without links.
    void typeKey(QKeyEvent* event);
    // An address typed before the cursor, made a link once the key that
    // ends it (Space, Enter, Tab) is typed.
    struct PendingLink
    {
        int position = 0;
        QString text;
        QString href;
    };
    [[nodiscard]] std::optional<PendingLink> linkBeforeKey(const QKeyEvent* event) const;
    // Makes the pending link one: its own undo step.
    void applyLink(const PendingLink& link);
    // Whether a click at `position` (viewport coordinates) with `modifiers`
    // opens a link: Ctrl held, over an allowed one.
    [[nodiscard]] QString linkToOpenAt(QPointF position, Qt::KeyboardModifiers modifiers) const;
    // Lists (task 3.5): Tab / Shift+Tab nest and un-nest items, Enter on an
    // empty item goes up a level (or out of the list), Backspace at the start
    // of an item removes its marker. True if handled.
    bool listKey(const QKeyEvent* event);
    // The list a Space would start: "- " dashed, "* " bulleted, "1. "
    // numbered, typed at the start of a Body paragraph.
    [[nodiscard]] std::optional<editor::ParagraphStyle> listMarker(const QKeyEvent* event) const;
    // The markers Qt can't draw: the dashes of dashed lists and checkboxes.
    void paintMarkers();
    // Code on a gray background (QPalette::AlternateBase), drawn, not stored,
    // as in Markdown viewers: a rounded panel the column's width behind each
    // run of Monospaced paragraphs, and a small rounded pill behind
    // monospaced text in other paragraphs (reaching a little past the text,
    // into kTextBleed at the start of a line).
    void paintCodeBackgrounds();
    // The room code needs (not stored: NoteFormat keeps neither): margins on
    // Monospaced paragraphs, for the panel's padding and its distance from the
    // text around it, and on paragraphs with inline code more space above and
    // below each line, for the pills' top and bottom. Kept up to
    // date after every change, as part of that change's undo step
    // (`joinUndo`), or outside the history when a note is loaded. Copies for
    // other apps leave it out.
    void syncCodeSpacing(bool joinUndo);
    // The same for the paragraphs that changed since the last time (and the
    // ones next to them, whose spacing depends on them), so a keystroke in a
    // long note doesn't go over all of it.
    void syncChangedCodeSpacing();
    void syncCodeSpacing(bool joinUndo, const QTextBlock& first, const QTextBlock& last);
    // The checklist item whose checkbox is at `position` (viewport
    // coordinates), if any.
    [[nodiscard]] QTextBlock checkboxAt(QPointF position) const;
    void paintCheckbox(QPainter& painter, const QRectF& box, bool checked) const;
    // Checked items' text is grayed and links take the link color, drawn so,
    // not stored (extra selections). For the paragraphs in view and around
    // them; scrolling and resizing update them.
    void updateTextDecorations();
    // Updates them after the current change, from the event loop.
    // Never from inside the document's signals: QTextDocument::clear() emits
    // contentsChanged() while it holds its cursor list aside, and replacing
    // the extra selections there freed cursors it then put back (heap
    // corruption, the app aborted when switching from a note with checked
    // items).
    void scheduleTextDecorations();

    void updateViewportMargins();
    // The root frame's margins: kTextBleed at the sides, the bottom padding.
    void applyFramePadding();

    editor::ImageObject* m_imageObject;
    QAction* m_pasteAsPlainText;
    QAction* m_pasteAsMarkdown;
    PasteMode m_pasteMode = PasteMode::Auto;
    // The note's attachments by uuid: loaded with it, and every one added since.
    QHash<QString, models::Attachment> m_attachments;

    int m_rightPadding = 0;
    int m_bottomPadding = 0;
    bool m_moveCheckedToBottom = false;
    bool m_overClickable = false; // the pointer shows a hand there
    bool m_decorationsPending = false;
    // The part of the document changed since the code spacing was last
    // synced (document positions); -1: nothing.
    int m_changedFrom = -1;
    int m_changedTo = -1;
    // The checkbox a left button press went to: the click is ours from press
    // to release (Qt sees none of it), and toggles if released on it too.
    QTextBlock m_pressedCheckbox;
    // Likewise the link a Ctrl+Click went to: opened if released on it.
    QString m_pressedLink;
    LinkOpener m_linkOpener;
};

} // namespace notes::ui
