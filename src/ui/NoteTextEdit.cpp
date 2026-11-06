#include "ui/NoteTextEdit.h"

#include "editor/ImageObject.h"
#include "editor/Links.h"
#include "editor/NoteFormat.h"
#include "editor/PasteCleaner.h"
#include "models/Uuid.h"

#include <QAbstractTextDocumentLayout>
#include <QAction>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QDataStream>
#include <QDesktopServices>
#include <QEvent>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QHelpEvent>
#include <QIODevice>
#include <QImage>
#include <QKeyEvent>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextDocumentFragment>
#include <QTextFragment>
#include <QTextFrame>
#include <QTextLayout>
#include <QTextList>
#include <QToolTip>
#include <QUrl>

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>

namespace notes::ui {

namespace {

constexpr QLatin1StringView kAttachmentScheme("attachment://");
// Version of kFragmentMime's layout, first in the data.
constexpr qint32 kFragmentVersion = 1;
// Sanity limit for a fragment's image count: far beyond a real selection.
constexpr qint32 kMaxFragmentImages = 10'000;

// The uuid in an image name `attachment://<uuid>`; empty for other names.
QString uuidOf(const QString& imageName)
{
    if (!imageName.startsWith(kAttachmentScheme)) {
        return {};
    }
    return imageName.sliced(kAttachmentScheme.size());
}

// `format` without what makes it an image, for text next to one.
QTextCharFormat textFormat(QTextCharFormat format)
{
    for (const int property :
         {QTextFormat::ObjectType, QTextFormat::ImageName, QTextFormat::ImageWidth,
          QTextFormat::ImageHeight, QTextFormat::ImageQuality, QTextFormat::ImageAltText,
          QTextFormat::ImageTitle}) {
        format.clearProperty(property);
    }
    return format;
}

// `document` without the spacing the editor puts around code (block margins
// and line spacing; see NoteTextEdit::syncCodeSpacing()): for other apps.
void clearCodeSpacing(QTextDocument& document)
{
    QTextCursor cursor(&document);
    for (QTextBlock block = document.begin(); block.isValid(); block = block.next()) {
        QTextBlockFormat format = block.blockFormat();
        for (const int property : {QTextFormat::BlockTopMargin, QTextFormat::BlockBottomMargin,
                                   QTextFormat::BlockLeftMargin, QTextFormat::BlockRightMargin,
                                   QTextFormat::LineHeight, QTextFormat::LineHeightType}) {
            format.clearProperty(property);
        }
        cursor.setPosition(block.position());
        cursor.setBlockFormat(format);
    }
}

QString imageNameFor(const QString& uuid)
{
    return kAttachmentScheme + uuid;
}

// Calls `visit(fragment)` for each image in `document`, in document order
// (table cells included: they are blocks too).
template <typename Visit>
void forEachImage(const QTextDocument& document, Visit visit)
{
    for (QTextBlock block = document.begin(); block.isValid(); block = block.next()) {
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            if (it.fragment().charFormat().isImageFormat()) {
                visit(it.fragment());
            }
        }
    }
}

} // namespace

NoteTextEdit::NoteTextEdit(QWidget* parent)
    : QTextEdit(parent)
    , m_imageObject(new editor::ImageObject(this))
    , m_pasteAsPlainText(new QAction(tr("Paste as &Plain Text"), this))
    , m_pasteAsMarkdown(new QAction(tr("Paste as &Markdown"), this))
    , m_linkOpener([](const QUrl& url) { return QDesktopServices::openUrl(url); })
{
    m_pasteAsPlainText->setObjectName(QStringLiteral("pasteAsPlainTextAction"));
    m_pasteAsPlainText->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_V));
    m_pasteAsMarkdown->setObjectName(QStringLiteral("pasteAsMarkdownAction"));
    m_pasteAsMarkdown->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_V));
    for (QAction* action : {m_pasteAsPlainText, m_pasteAsMarkdown}) {
        // Only while typing in the note, never into the search field.
        action->setShortcutContext(Qt::WidgetShortcut);
        addAction(action);
    }
    connect(m_pasteAsPlainText, &QAction::triggered, this,
            [this] { pasteAs(PasteMode::PlainText); });
    connect(m_pasteAsMarkdown, &QAction::triggered, this, [this] { pasteAs(PasteMode::Markdown); });
    // Pasted HTML would bring formatting the plain editor can't show or save
    // yet; rich text support comes with the Phase 3 document model.
    setAcceptRichText(false);
    setFrameShape(QFrame::NoFrame);
    setTabChangesFocus(false);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    connect(document(), &QTextDocument::contentsChanged, this,
            &NoteTextEdit::scheduleTextDecorations);
    connect(document(), &QTextDocument::contentsChange, this,
            [this](int position, int removed, int added) {
                if (m_changedFrom < 0) {
                    m_changedFrom = position;
                    m_changedTo = position + added;
                    return;
                }
                if (m_changedTo >= position) {
                    m_changedTo = std::max(position, m_changedTo - removed) + added;
                }
                m_changedFrom = std::min(m_changedFrom, position);
                m_changedTo = std::max(m_changedTo, position + added);
            });
    // The link look and gray checked items are for the paragraphs in view.
    connect(verticalScrollBar(), &QScrollBar::valueChanged, this,
            &NoteTextEdit::updateTextDecorations);
    // Images fit the text width (task 3.7).
    m_imageObject->registerOn(*document());
}

NoteTextEdit::~NoteTextEdit() = default;

void NoteTextEdit::setTextPadding(int right, int bottom)
{
    m_rightPadding = right;
    m_bottomPadding = bottom;
    updateViewportMargins();
    applyFramePadding();
}

int NoteTextEdit::rightInset() const
{
    return m_rightPadding - kTextBleed + verticalScrollBar()->sizeHint().width();
}

void NoteTextEdit::setNoteBody(const QString& body, const QString& plainText,
                               const QList<models::Attachment>& attachments)
{
    m_pressedCheckbox = QTextBlock(); // a click doesn't carry over to another note
    m_attachments.clear();
    for (const models::Attachment& attachment : attachments) {
        m_attachments.insert(attachment.uuid, attachment);
    }
    // No cursors of ours in the document while it's cleared and refilled.
    setExtraSelections({});
    // One edit, so Qt lays the note out once it's all there (a long one in
    // the background, the first screen first), not after every paragraph:
    // a 50,000-word note opened in about 280 ms, now about 40 (task 3.11).
    QTextCursor batch(document());
    batch.beginEditBlock();
    editor::NoteFormat::load(body, plainText, *document());
    if (document()->isEmpty()) {
        // A new (empty) note starts with its title, as in macOS Notes (task
        // 3.3): typing goes into a Title paragraph, and Enter then gives Body.
        QTextBlockFormat title;
        title.setHeadingLevel(editor::StyleManager::headingLevel(editor::ParagraphStyle::Title));
        QTextCursor(document()).setBlockFormat(title);
        editor::StyleManager::styleHeadings(*document());
    }
    syncCodeSpacing(false);
    batch.endEditBlock();
    // Loading isn't an edit: start the note with no undo history.
    document()->clearUndoRedoStacks();
    applyFramePadding();
    if (document()->characterCount() <= 1) {
        // Filled directly rather than through setPlainText(), an empty
        // document's only line was left without a layout, and Qt draws no
        // caret there: a new note showed no caret until the first key.
        document()->markContentsDirty(0, document()->characterCount());
    }
    m_changedFrom = -1;      // all synced above
    updateTextDecorations(); // grayed from the start, not a moment later
}

QString NoteTextEdit::noteBody() const
{
    return editor::NoteFormat::serialize(*document());
}

QString NoteTextEdit::notePlainText() const
{
    return toPlainText().remove(QChar::ObjectReplacementCharacter);
}

QList<models::Attachment> NoteTextEdit::attachments() const
{
    QList<models::Attachment> result;
    QStringList seen;
    forEachImage(*document(), [&](const QTextFragment& fragment) {
        const QString uuid = uuidOf(fragment.charFormat().toImageFormat().name());
        if (uuid.isEmpty() || seen.contains(uuid)) {
            return;
        }
        seen.append(uuid);
        models::Attachment attachment = m_attachments.value(uuid);
        attachment.uuid = uuid;
        result.append(attachment);
    });
    return result;
}

void NoteTextEdit::insertImageFiles(const QStringList& paths)
{
    if (isReadOnly()) {
        return;
    }
    QList<editor::ImageImport::Result> results;
    for (const QString& path : paths) {
        results.append(editor::ImageImport::fromFile(path));
    }
    insertImported(results);
}

QVariant NoteTextEdit::loadResource(int type, const QUrl& name)
{
    // Always an answer, never an invalid QVariant: given none, QTextDocument
    // reads the name as a local file itself.
    if (type != QTextDocument::ImageResource) {
        return QByteArray();
    }
    const auto found = m_attachments.constFind(uuidOf(name.toString()));
    if (found == m_attachments.cend()) {
        return QImage(); // drawn as a missing image
    }
    return editor::ImageImport::decode(found->data); // null if unreadable: missing too
}

void NoteTextEdit::contextMenuEvent(QContextMenuEvent* event)
{
    const std::unique_ptr<QMenu> menu(createStandardContextMenu(event->pos()));
    // After Qt's Paste.
    QAction* after = nullptr;
    const QList<QAction*> actions = menu->actions();
    for (qsizetype i = 0; i < actions.size(); ++i) {
        if (actions.at(i)->objectName() == u"edit-paste" && i + 1 < actions.size()) {
            after = actions.at(i + 1);
        }
    }
    if (!isReadOnly()) {
        menu->insertActions(after, {m_pasteAsPlainText, m_pasteAsMarkdown});
    }
    menu->exec(event->globalPos());
}

bool NoteTextEdit::canInsertFromMimeData(const QMimeData* source) const
{
    return source->hasFormat(QLatin1StringView(kFragmentMime)) || source->hasImage() ||
           QTextEdit::canInsertFromMimeData(source);
}

QMimeData* NoteTextEdit::createMimeDataFromSelection() const
{
    const QTextDocumentFragment selection = textCursor().selection();
    QTextDocument copy;
    QTextCursor(&copy).insertFragment(selection);
    QStringList uuids;
    forEachImage(copy, [&](const QTextFragment& fragment) {
        const QString uuid = uuidOf(fragment.charFormat().toImageFormat().name());
        if (m_attachments.contains(uuid) && !uuids.contains(uuid)) {
            uuids.append(uuid);
        }
    });
    // A plain copy of Qt's: its own lists only the formats it makes.
    auto* mime = new QMimeData;
    {
        const std::unique_ptr<QMimeData> text(QTextEdit::createMimeDataFromSelection());
        for (const QString& format : text->formats()) {
            mime->setData(format, text->data(format));
        }
    }
    // Other apps get the HTML without the room made around code here.
    if (mime->hasHtml()) {
        QTextDocument html;
        QTextCursor(&html).insertFragment(selection);
        clearCodeSpacing(html);
        mime->setHtml(QTextDocumentFragment(&html).toHtml());
    }
    QByteArray encoded;
    QDataStream out(&encoded, QIODevice::WriteOnly);
    out << kFragmentVersion << editor::NoteFormat::serialize(copy)
        << static_cast<qint32>(uuids.size());
    for (const QString& uuid : std::as_const(uuids)) {
        const models::Attachment attachment = m_attachments.value(uuid);
        out << attachment.uuid << attachment.fileName << attachment.mimeType << attachment.data
            << attachment.thumbnail;
    }
    mime->setData(QLatin1StringView(kFragmentMime), encoded);
    // An image alone also goes as an image, for other apps. (One character,
    // plus the paragraph end every document has.)
    if (uuids.size() == 1 && copy.characterCount() == 2) {
        mime->setImageData(editor::ImageImport::decode(m_attachments.value(uuids.first()).data));
    }
    return mime;
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
    using editor::ParagraphStyle;
    using editor::StyleManager;
    if (style == ParagraphStyle::Monospaced) {
        if (StyleManager::selectsWords(cursor)) {
            // A few words selected: inline code, not a code block.
            StyleManager::toggleInlineMonospace(cursor);
            setTextCursor(cursor);
            return;
        }
        if (!cursor.hasSelection() &&
            StyleManager::styleOf(cursor.block()) != ParagraphStyle::Monospaced &&
            StyleManager::isMonospaced(currentCharFormat())) {
            // Just after inline code: what's typed next is plain text.
            setCurrentCharFormat(StyleManager::withoutMonospace(currentCharFormat()));
            return;
        }
        // Code blocks already: back to Body.
        bool allCode = true;
        const QTextBlock last = document()->findBlock(cursor.selectionEnd());
        for (QTextBlock block = document()->findBlock(cursor.selectionStart()); block.isValid();
             block = block.next()) {
            allCode = allCode && StyleManager::styleOf(block) == ParagraphStyle::Monospaced;
            if (block == last) {
                break;
            }
        }
        if (allCode) {
            style = ParagraphStyle::Body;
        }
    }
    StyleManager::apply(cursor, style);
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
    if (isReadOnly()) {
        return;
    }
    const int start = textCursor().selectionStart();
    const QTextBlock startBlock = document()->findBlock(start);
    const QTextBlockFormat startFormat = startBlock.blockFormat();
    // Pasting into an empty paragraph replaces it, as far as its style goes.
    const bool startEmpty = startBlock.length() == 1;
    const int revision = document()->revision();

    bool rich = false;
    switch (m_pasteMode) {
    case PasteMode::Auto:
        rich = insertImages(source) || insertRichText(source);
        break;
    case PasteMode::Markdown:
        rich = source->hasText() && insertBody(editor::PasteCleaner::fromMarkdown(source->text()));
        break;
    case PasteMode::PlainText:
        break;
    }
    if (!rich && source->hasText()) {
        // Right after an image, the cursor's format is the image's: pasted
        // text would become one image per character.
        if (QTextCursor cursor = textCursor(); cursor.charFormat().isImageFormat()) {
            cursor.setCharFormat(textFormat(cursor.charFormat()));
            setTextCursor(cursor);
        }
        QMimeData text;
        text.setText(source->text());
        QTextEdit::insertFromMimeData(&text);
    }
    if (document()->revision() == revision) {
        return;
    }
    // Pasted or dropped plain text takes the paragraph format of its own
    // first line and the font at the cursor: pasted into a new note's title,
    // every line became Body in the title's size and bold. The paragraph
    // pasted into keeps its style (unless it was empty and the paste brings
    // styles of its own), the lines after it are Body or their own style, and
    // each gets its style's font, in the same undo step.
    QTextCursor cursor(document());
    cursor.setPosition(start);
    cursor.setPosition(textCursor().position(), QTextCursor::KeepAnchor);
    cursor.joinPreviousEditBlock();
    if (!(rich && startEmpty)) {
        QTextCursor(document()->findBlock(start)).setBlockFormat(startFormat);
    }
    editor::StyleManager::fitText(cursor);
    cursor.endEditBlock();
}

void NoteTextEdit::pasteAs(PasteMode mode)
{
    if (isReadOnly()) {
        return;
    }
    const QMimeData* source = QGuiApplication::clipboard()->mimeData();
    if (source == nullptr || !source->hasText()) {
        return;
    }
    m_pasteMode = mode;
    insertFromMimeData(source);
    m_pasteMode = PasteMode::Auto;
    ensureCursorVisible();
}

bool NoteTextEdit::insertRichText(const QMimeData* source)
{
    // HTML wins: it is the formatted version of the same text.
    if (source->hasHtml()) {
        return insertBody(editor::PasteCleaner::fromHtml(source->html()));
    }
    if (source->hasText() && editor::PasteCleaner::looksLikeMarkdown(source->text())) {
        return insertBody(editor::PasteCleaner::fromMarkdown(source->text()));
    }
    return false;
}

bool NoteTextEdit::insertBody(const QString& body)
{
    QTextDocument pasted;
    if (body.isEmpty() || editor::NoteFormat::deserialize(body, pasted)) {
        return false; // nothing left: the plain text instead
    }
    insertDocument(pasted);
    return true;
}

void NoteTextEdit::insertDocument(const QTextDocument& pasted)
{
    QTextCursor cursor = textCursor();
    const QTextBlock target = cursor.block();
    // Into an empty paragraph, the first pasted one comes with its style.
    const bool intoEmpty = !cursor.hasSelection() && target.length() == 1;
    const QTextBlock first = pasted.firstBlock();
    cursor.beginEditBlock();
    if (intoEmpty) {
        // For when Qt adds the first pasted paragraph to this one, which
        // keeps its own style then.
        QTextBlockFormat format = first.blockFormat();
        format.clearProperty(QTextFormat::ObjectIndex); // the pasted document's list
        cursor.setBlockFormat(format);
    }
    cursor.insertFragment(QTextDocumentFragment(&pasted));
    if (intoEmpty && target.length() == 1 && target.next().isValid()) {
        // Qt put it on a paragraph of its own instead (a list item does),
        // after this one, which is left over: remove it.
        QTextCursor leftover(target);
        if (target.previous().isValid()) {
            leftover.movePosition(QTextCursor::PreviousCharacter, QTextCursor::KeepAnchor);
            leftover.removeSelectedText();
        } else {
            // The first line: the next one joins it, with its style and list.
            const QTextBlock next = target.next();
            QTextList* list = next.textList();
            leftover.setBlockFormat(next.blockFormat());
            leftover.deleteChar();
            if (list != nullptr) {
                list->add(target);
            }
        }
    }
    cursor.endEditBlock();
    setTextCursor(cursor);
}

bool NoteTextEdit::insertImages(const QMimeData* source)
{
    if (source->hasFormat(QLatin1StringView(kFragmentMime))) {
        return insertFragment(source->data(QLatin1StringView(kFragmentMime)));
    }
    if (source->hasUrls()) {
        QList<editor::ImageImport::Result> results;
        for (const QUrl& url : source->urls()) {
            // Local image files only: nothing is fetched from the network.
            if (url.isLocalFile() && editor::ImageImport::isImageFile(url.toLocalFile())) {
                results.append(editor::ImageImport::fromFile(url.toLocalFile()));
            }
        }
        if (results.isEmpty()) {
            return false; // not images: the links paste as text
        }
        insertImported(results);
        return true;
    }
    // An image alone (a screenshot, "Copy Image" in a browser). With text
    // too, the text wins: word processors add a picture of the copied text.
    if (source->hasImage() && (!source->hasText() || source->text().trimmed().isEmpty())) {
        insertImported(
            {editor::ImageImport::fromImage(qvariant_cast<QImage>(source->imageData()))});
        return true;
    }
    return false;
}

void NoteTextEdit::insertImported(const QList<editor::ImageImport::Result>& results)
{
    QTextCursor cursor = textCursor();
    cursor.beginEditBlock();
    QString error;
    for (const editor::ImageImport::Result& result : results) {
        if (!result.ok()) {
            if (error.isEmpty()) {
                error = result.error;
            }
            continue;
        }
        m_attachments.insert(result.attachment.uuid, result.attachment);
        QTextImageFormat image;
        image.setName(imageNameFor(result.attachment.uuid));
        cursor.insertImage(image);
    }
    cursor.endEditBlock();
    setTextCursor(cursor);
    if (!error.isEmpty()) {
        emit imageRejected(error);
    }
}

bool NoteTextEdit::insertFragment(const QByteArray& encoded)
{
    QDataStream in(encoded);
    qint32 version = 0;
    QString body;
    qint32 count = 0;
    in >> version >> body >> count;
    if (in.status() != QDataStream::Ok || version != kFragmentVersion || count < 0 ||
        count > kMaxFragmentImages) {
        return false;
    }
    QHash<QString, models::Attachment> copies; // old uuid -> copy under a new one
    for (qint32 i = 0; i < count; ++i) {
        models::Attachment attachment;
        in >> attachment.uuid >> attachment.fileName >> attachment.mimeType >> attachment.data >>
            attachment.thumbnail;
        if (in.status() != QDataStream::Ok) {
            return false;
        }
        const QString original = attachment.uuid;
        attachment.uuid = models::newUuid();
        attachment.sizeBytes = attachment.data.size();
        copies.insert(original, attachment);
    }
    QTextDocument pasted;
    if (editor::NoteFormat::deserialize(body, pasted)) {
        return false; // unreadable
    }
    // Each image becomes its own copy: an attachment belongs to one note,
    // and deleting the original mustn't take the copy with it.
    QList<std::pair<int, QTextImageFormat>> renamed;
    forEachImage(pasted, [&](const QTextFragment& fragment) {
        QTextImageFormat image = fragment.charFormat().toImageFormat();
        const auto copy = copies.constFind(uuidOf(image.name()));
        if (copy != copies.cend()) {
            image.setName(imageNameFor(copy->uuid));
            for (int at = fragment.position(); at < fragment.position() + fragment.length(); ++at) {
                renamed.append({at, image});
            }
        }
    });
    for (const auto& [position, image] : std::as_const(renamed)) {
        QTextCursor at(&pasted);
        at.setPosition(position);
        at.setPosition(position + 1, QTextCursor::KeepAnchor);
        at.setCharFormat(image);
    }
    for (const models::Attachment& attachment : std::as_const(copies)) {
        m_attachments.insert(attachment.uuid, attachment);
    }
    insertDocument(pasted);
    return true;
}

void NoteTextEdit::keyPressEvent(QKeyEvent* event)
{
    const std::optional<PendingLink> link = isReadOnly() ? std::nullopt : linkBeforeKey(event);
    typeKey(event);
    if (link) {
        applyLink(*link);
    }
}

std::optional<NoteTextEdit::PendingLink> NoteTextEdit::linkBeforeKey(const QKeyEvent* event) const
{
    const int key = event->key();
    const bool endsWord =
        key == Qt::Key_Space || key == Qt::Key_Return || key == Qt::Key_Enter || key == Qt::Key_Tab;
    const Qt::KeyboardModifiers modifiers =
        event->modifiers() & ~(Qt::KeypadModifier | Qt::ShiftModifier);
    const QTextCursor cursor = textCursor();
    // Not in code, and not after a link already (a space typed after one).
    if (!endsWord || modifiers != Qt::NoModifier || cursor.hasSelection() ||
        cursor.charFormat().isAnchor() ||
        editor::StyleManager::styleOf(cursor.block()) == editor::ParagraphStyle::Monospaced) {
        return std::nullopt;
    }
    const QTextBlock block = cursor.block();
    const std::optional<editor::Links::Match> match =
        editor::Links::endingAt(QStringView(block.text()).first(cursor.positionInBlock()));
    if (!match) {
        return std::nullopt;
    }
    return PendingLink{.position = block.position() + static_cast<int>(match->start),
                       .text = block.text().sliced(match->start, match->length),
                       .href = match->href};
}

void NoteTextEdit::applyLink(const PendingLink& link)
{
    QTextCursor cursor(document());
    cursor.setPosition(link.position);
    cursor.setPosition(link.position + static_cast<int>(link.text.size()), QTextCursor::KeepAnchor);
    if (cursor.selectedText() != link.text) {
        return; // the key changed the text (it never should)
    }
    QTextCharFormat format;
    format.setAnchor(true);
    format.setAnchorHref(link.href);
    cursor.mergeCharFormat(format);
}

bool NoteTextEdit::openLink(const QString& href)
{
    if (!editor::Links::isAllowed(href)) {
        return false;
    }
    return m_linkOpener(QUrl(href));
}

QString NoteTextEdit::linkToOpenAt(QPointF position, Qt::KeyboardModifiers modifiers) const
{
    if (!modifiers.testFlag(Qt::ControlModifier)) {
        return {};
    }
    const QString href = anchorAt(position.toPoint());
    return editor::Links::isAllowed(href) ? href : QString();
}

void NoteTextEdit::typeKey(QKeyEvent* event)
{
    if (!isReadOnly() && (headingKey(event) || listKey(event) || codeKey(event))) {
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

bool NoteTextEdit::codeKey(const QKeyEvent* event)
{
    const bool enter = event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter;
    QTextCursor cursor = textCursor();
    if (!enter || (event->modifiers() & ~Qt::KeypadModifier) != Qt::NoModifier ||
        cursor.currentList() != nullptr ||
        editor::StyleManager::styleOf(cursor.block()) != editor::ParagraphStyle::Monospaced) {
        return false;
    }
    cursor.beginEditBlock();
    cursor.removeSelectedText();
    // Code's own format for the new line; syncCodeSpacing() moves the
    // panel's spacing.
    QTextCharFormat code = cursor.charFormat();
    if (!editor::StyleManager::isMonospaced(code)) {
        code = cursor.block().charFormat();
    }
    cursor.insertBlock(cursor.blockFormat(), code);
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

void NoteTextEdit::resizeEvent(QResizeEvent* event)
{
    QTextEdit::resizeEvent(event);
    updateTextDecorations(); // other paragraphs may be in view now
}

void NoteTextEdit::paintEvent(QPaintEvent* event)
{
    paintCodeBackgrounds(); // behind the text
    QTextEdit::paintEvent(event);
    paintMarkers();
}

namespace {

// The font a list item's marker is drawn in: its paragraph's.
QFont markerFont(const QTextBlock& block, const QTextDocument& document)
{
    return block.charFormat().font().resolve(document.defaultFont());
}

// Code backgrounds. Inline code: a pill reaching kPillPad past the text at
// the sides, no space added beside it: that space would belong to the letters
// next to it, and the caret and selection with them. A code block: its text kPanelPadX inside the
// panel at the sides and kPanelPadY at the top and bottom, the panel kPanelGap from the text
// around.
constexpr qreal kPillPad = 2;
// Above and below: the pill reaches kPillPadY past the line, and a paragraph
// with inline code has kInlineRoomY more above and below each line, so pills
// on lines next to each other stay 2 * (kInlineRoomY - kPillPadY) apart.
constexpr qreal kPillPadY = 1;
constexpr qreal kInlineRoomY = 2;
constexpr qreal kPillRadius = 4;
constexpr qreal kPanelPadX = 12;
constexpr qreal kPanelPadY = 8;
constexpr qreal kPanelGap = 8;
constexpr qreal kPanelRadius = 6;

// A Monospaced paragraph outside a table: part of a code panel.
bool isCodeBlock(const QTextBlock& block)
{
    return block.isValid() &&
           editor::StyleManager::styleOf(block) == editor::ParagraphStyle::Monospaced &&
           QTextCursor(block).currentTable() == nullptr;
}

// Monospaced text in a paragraph that isn't code: [start, end) offsets in the
// block, neighboring runs (bold code next to code) as one.
struct CodeRange
{
    int start = 0;
    int end = 0;
};

QList<CodeRange> inlineCode(const QTextBlock& block)
{
    QList<CodeRange> ranges;
    if (isCodeBlock(block)) {
        return ranges;
    }
    for (auto it = block.begin(); !it.atEnd(); ++it) {
        const QTextFragment fragment = it.fragment();
        const QTextCharFormat format = fragment.charFormat();
        if (format.isImageFormat() || !editor::StyleManager::isMonospaced(format)) {
            continue;
        }
        const int start = fragment.position() - block.position();
        const int end = start + fragment.length();
        if (!ranges.isEmpty() && ranges.last().end == start) {
            ranges.last().end = end;
        } else {
            ranges.append({.start = start, .end = end});
        }
    }
    return ranges;
}

// Where `block`'s layout is, in viewport coordinates (`offset`: the scroll).
QPointF layoutOrigin(const QTextBlock& block, const QAbstractTextDocumentLayout& layout,
                     const QPointF& offset)
{
    return layout.blockBoundingRect(block).topLeft() - block.layout()->boundingRect().topLeft() +
           offset;
}

// `block`'s format with the margins of code (see syncCodeSpacing()).
QTextBlockFormat spacedFormat(const QTextBlock& block)
{
    QTextBlockFormat format = block.blockFormat();
    for (const int property :
         {QTextFormat::BlockTopMargin, QTextFormat::BlockBottomMargin, QTextFormat::BlockLeftMargin,
          QTextFormat::BlockRightMargin, QTextFormat::LineHeight, QTextFormat::LineHeightType}) {
        format.clearProperty(property);
    }
    if (!isCodeBlock(block)) {
        if (!inlineCode(block).isEmpty()) {
            // Room for the pills above and below the lines.
            format.setTopMargin(kInlineRoomY);
            format.setBottomMargin(kInlineRoomY);
            format.setLineHeight(2 * kInlineRoomY, QTextBlockFormat::LineDistanceHeight);
        }
        return format;
    }
    // Its text inside the panel, the panel off its neighbors.
    format.setLeftMargin(kPanelPadX);
    format.setRightMargin(kPanelPadX);
    if (!isCodeBlock(block.previous())) {
        format.setTopMargin(kPanelPadY + kPanelGap);
    }
    if (!isCodeBlock(block.next())) {
        format.setBottomMargin(kPanelPadY + kPanelGap);
    }
    return format;
}

// The pills behind `block`'s inline code, one per line it's on.
QList<QRectF> pillsFor(const QTextBlock& block, const QPointF& origin)
{
    QList<QRectF> pills;
    const QTextLayout* layout = block.layout();
    for (const CodeRange& range : inlineCode(block)) {
        for (int i = 0; i < layout->lineCount(); ++i) {
            const QTextLine line = layout->lineAt(i);
            const int from = std::max(range.start, line.textStart());
            const int to = std::min(range.end, line.textStart() + line.textLength());
            if (from >= to) {
                continue;
            }
            // Where the code is on screen: its edges, whatever the direction
            // of its text (right to left: its end is at the left).
            qreal left = line.cursorToX(from);
            qreal right = left;
            for (int position = from + 1; position <= to; ++position) {
                const qreal x = line.cursorToX(position);
                left = std::min(left, x);
                right = std::max(right, x);
            }
            pills.append(QRectF(origin.x() + left - kPillPad, origin.y() + line.y() - kPillPadY,
                                right - left + (2 * kPillPad), line.height() + (2 * kPillPadY)));
        }
    }
    return pills;
}

// The top and bottom of `block`'s lines, in viewport coordinates.
std::pair<qreal, qreal> linesSpan(const QTextBlock& block, const QPointF& origin)
{
    const QTextLayout* layout = block.layout();
    if (layout->lineCount() == 0) {
        return {origin.y(), origin.y()};
    }
    const QTextLine last = layout->lineAt(layout->lineCount() - 1);
    return {origin.y() + layout->lineAt(0).y(), origin.y() + last.y() + last.height()};
}

} // namespace

void NoteTextEdit::paintCodeBackgrounds()
{
    QPainter painter(viewport());
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(palette().color(QPalette::AlternateBase));
    const QPointF offset(-horizontalScrollBar()->value(), -verticalScrollBar()->value());
    const QAbstractTextDocumentLayout& layout = *document()->documentLayout();
    const QRect visible = viewport()->rect();
    // The text column, where panels go.
    const QTextFrameFormat frame = document()->rootFrame()->frameFormat();
    const qreal columnLeft = offset.x() + frame.leftMargin();
    const qreal columnRight = offset.x() + document()->textWidth() - frame.rightMargin();

    QTextBlock block = cursorForPosition(QPoint(0, 0)).block();
    while (isCodeBlock(block) && isCodeBlock(block.previous())) {
        block = block.previous(); // a panel starting above the view
    }
    while (block.isValid()) {
        if (block.layout() == nullptr) {
            block = block.next();
            continue;
        }
        QPointF origin = layoutOrigin(block, layout, offset);
        if (origin.y() > visible.bottom()) {
            break;
        }
        if (!isCodeBlock(block)) {
            for (const QRectF& pill : pillsFor(block, origin)) {
                painter.drawRoundedRect(pill, kPillRadius, kPillRadius);
            }
            block = block.next();
            continue;
        }
        // One panel for the run of Monospaced paragraphs, the column's width.
        const qreal left = columnLeft + (block.blockFormat().indent() * document()->indentWidth());
        const qreal top = linesSpan(block, origin).first;
        qreal bottom = linesSpan(block, origin).second;
        for (block = block.next(); isCodeBlock(block) && block.layout() != nullptr;
             block = block.next()) {
            origin = layoutOrigin(block, layout, offset);
            bottom = linesSpan(block, origin).second;
        }
        const QRectF panel(QPointF(left, top - kPanelPadY),
                           QPointF(columnRight, bottom + kPanelPadY));
        painter.drawRoundedRect(panel, kPanelRadius, kPanelRadius);
    }
}

void NoteTextEdit::syncCodeSpacing(bool joinUndo)
{
    syncCodeSpacing(joinUndo, document()->begin(), document()->lastBlock());
}

void NoteTextEdit::syncChangedCodeSpacing()
{
    if (m_changedFrom < 0) {
        return;
    }
    const int last = document()->characterCount() - 1;
    QTextBlock first = document()->findBlock(std::clamp(m_changedFrom, 0, last));
    QTextBlock end = document()->findBlock(std::clamp(m_changedTo, 0, last));
    m_changedFrom = -1;
    if (first.previous().isValid()) {
        first = first.previous();
    }
    if (end.next().isValid()) {
        end = end.next();
    }
    syncCodeSpacing(true, first, end);
}

void NoteTextEdit::syncCodeSpacing(bool joinUndo, const QTextBlock& first, const QTextBlock& last)
{
    QTextCursor cursor(document());
    bool editing = false;
    const auto edit = [&] {
        if (!editing) {
            joinUndo ? cursor.joinPreviousEditBlock() : cursor.beginEditBlock();
            editing = true;
        }
    };
    const int end = last.blockNumber();
    for (QTextBlock block = first; block.isValid() && block.blockNumber() <= end;
         block = block.next()) {
        if (const QTextBlockFormat format = spacedFormat(block); format != block.blockFormat()) {
            edit();
            cursor.setPosition(block.position());
            cursor.setBlockFormat(format);
        }
    }
    if (editing) {
        cursor.endEditBlock();
    }
}

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
    if (event->button() == Qt::LeftButton) {
        // Ctrl+Click on a link: opened on release; the caret stays.
        m_pressedLink = linkToOpenAt(event->position(), event->modifiers());
        if (!m_pressedLink.isEmpty()) {
            event->accept();
            return;
        }
    }
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
    if (event->button() == Qt::LeftButton && !m_pressedLink.isEmpty()) {
        if (anchorAt(event->position().toPoint()) == m_pressedLink) {
            openLink(m_pressedLink);
        }
        m_pressedLink.clear();
        event->accept();
        return;
    }
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
    // (toggled on its release) or the link, not a word to select.
    if (event->button() == Qt::LeftButton) {
        m_pressedLink = linkToOpenAt(event->position(), event->modifiers());
        if (!m_pressedLink.isEmpty()) {
            event->accept();
            return;
        }
    }
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
    if (m_pressedCheckbox.isValid() || !m_pressedLink.isEmpty()) {
        event->accept(); // pressed on a checkbox or link: no text selection
        return;
    }
    QTextEdit::mouseMoveEvent(event);
    const bool over = event->buttons() == Qt::NoButton &&
                      ((!isReadOnly() && checkboxAt(event->position()).isValid()) ||
                       !linkToOpenAt(event->position(), event->modifiers()).isEmpty());
    if (over != m_overClickable) {
        m_overClickable = over;
        viewport()->setCursor(over ? Qt::PointingHandCursor : Qt::IBeamCursor);
    }
}

bool NoteTextEdit::viewportEvent(QEvent* event)
{
    if (const auto* help = dynamic_cast<const QHelpEvent*>(event);
        help != nullptr && event->type() == QEvent::ToolTip) {
        const QString href = anchorAt(help->pos());
        if (href.isEmpty()) {
            QToolTip::hideText();
            event->ignore();
            return true;
        }
        const QString how = editor::Links::isAllowed(href)
                                ? tr("Ctrl+Click to open")
                                : tr("Links of this kind can't be opened");
        QToolTip::showText(help->globalPos(), href + u'\n' + how, viewport());
        return true;
    }
    return QTextEdit::viewportEvent(event);
}

void NoteTextEdit::scheduleTextDecorations()
{
    if (m_decorationsPending) {
        return;
    }
    m_decorationsPending = true;
    QMetaObject::invokeMethod(
        this,
        [this] {
            m_decorationsPending = false;
            if (!isReadOnly()) {
                syncChangedCodeSpacing();
            }
            updateTextDecorations();
        },
        Qt::QueuedConnection);
}

void NoteTextEdit::updateTextDecorations()
{
    QTextCharFormat gray;
    gray.setForeground(palette().color(QPalette::PlaceholderText));
    QTextCharFormat link;
    link.setForeground(palette().color(QPalette::Link));
    link.setFontUnderline(true);
    QList<ExtraSelection> selections;
    // Only the paragraphs in view (and a screen's worth around): asking Qt
    // where text far away is would make it lay the whole note out at once.
    const QAbstractTextDocumentLayout* layout = document()->documentLayout();
    const int height = viewport()->height();
    const qreal bottom = verticalScrollBar()->value() + (2.0 * height);
    QTextBlock block = cursorForPosition(QPoint(0, -height)).block();
    for (; block.isValid() && layout->blockBoundingRect(block).top() <= bottom;
         block = block.next()) {
        if (block.length() > 1 && editor::StyleManager::isChecked(block)) {
            QTextCursor text(block);
            text.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
            selections.append({.cursor = text, .format = gray});
        }
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (fragment.charFormat().isAnchor() && !fragment.charFormat().anchorHref().isEmpty()) {
                QTextCursor text(document());
                text.setPosition(fragment.position());
                text.setPosition(fragment.position() + fragment.length(), QTextCursor::KeepAnchor);
                selections.append({.cursor = text, .format = link});
            }
        }
    }
    setExtraSelections(selections);
}

void NoteTextEdit::applyFramePadding()
{
    // On the document's root frame, so it scrolls with the text: space after
    // the last line, not a band that hides text while scrolling; and
    // kTextBleed at the sides, room for code backgrounds.
    // Not an edit: no undo step (a freshly opened note has no undo history).
    // Turning undo off and on also clears it, which setPlainText() did anyway.
    QTextDocument* doc = document();
    const bool undo = doc->isUndoRedoEnabled();
    doc->setUndoRedoEnabled(false);
    QTextFrame* root = doc->rootFrame();
    QTextFrameFormat format = root->frameFormat();
    format.setBottomMargin(doc->documentMargin() + m_bottomPadding);
    format.setLeftMargin(doc->documentMargin() + kTextBleed);
    format.setRightMargin(doc->documentMargin() + kTextBleed);
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
        updateTextDecorations(); // the theme's gray
    }
}

void NoteTextEdit::updateViewportMargins()
{
    setViewportMargins(0, 0, m_rightPadding - kTextBleed, 0);
}

} // namespace notes::ui
