// Images in the window (task 3.7): TC-EDT-020 a pasted PNG becomes one
// attachment and shows, TC-EDT-026 the note list shows its thumbnail. Also
// fitting the editor's width, files dropped or picked, copies within the app,
// Undo after a save, and that nothing loads from files or the network.

#include "TestHelpers.h"
#include "editor/ImageImport.h"
#include "editor/ImageObject.h"
#include "models/Note.h"
#include "models/NoteListRoles.h"
#include "models/SidebarRoles.h"
#include "services/Autosave.h"
#include "services/FolderService.h"
#include "services/NoteService.h"
#include "ui/ActionRegistry.h"
#include "ui/EditorView.h"
#include "ui/MainWindow.h"
#include "ui/NoteListView.h"
#include "ui/NoteTextEdit.h"

#include <QAction>
#include <QApplication>
#include <QBuffer>
#include <QClipboard>
#include <QFile>
#include <QImage>
#include <QMimeData>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextFragment>
#include <QUrl>

#include <memory>

using notes::editor::ImageImport;
using notes::editor::ImageObject;
using notes::models::Note;
using notes::services::Autosave;
using notes::services::OpenedNote;
using notes::test::await;
using notes::ui::ActionId;
using notes::ui::EditorView;
using notes::ui::MainWindow;
using notes::ui::NoteListView;
using notes::ui::NoteTextEdit;
namespace roles = notes::models;

namespace {

QImage filled(int width, int height, QColor color = QColor(30, 120, 200))
{
    QImage image(width, height, QImage::Format_RGB32);
    image.fill(color);
    return image;
}

QByteArray png(const QImage& image)
{
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "png");
    return bytes;
}

struct ShownImage
{
    QString name;
    int position = 0;
};

// The images the document shows, in order.
QList<ShownImage> imagesIn(const QTextDocument& document)
{
    QList<ShownImage> images;
    for (QTextBlock block = document.begin(); block.isValid(); block = block.next()) {
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (fragment.charFormat().isImageFormat()) {
                for (int i = 0; i < fragment.length(); ++i) {
                    images.append({.name = fragment.charFormat().toImageFormat().name(),
                                   .position = fragment.position() + i});
                }
            }
        }
    }
    return images;
}

} // namespace

class tst_Images : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void pastedImageIsStoredAndShown();
    void noteListShowsThumbnail();
    void wideImagesFitTheEditor();
    void droppedFilesBecomeImages();
    void attachActionAddsPickedImages();
    void copyPasteMakesANewImage();
    void undoAfterSaveKeepsTheImage();
    void imagesSurviveReopening();
    void tooLargeImageIsReported();
    void nothingLoadsFromFilesOrTheNetwork();
    void titleSkipsImages();
    void textNextToAnImageStaysText();
    void otherNotesDontSeeTheImages();

private:
    void openWindow();
    // Saves now instead of 500 ms later.
    void flush() const;
    [[nodiscard]] OpenedNote reopen() const;
    [[nodiscard]] QString writeImage(const QString& name, const QImage& image) const;
    [[nodiscard]] QSizeF shownSize(int position) const;

    std::unique_ptr<notes::test::TempDb> m_dir;
    std::unique_ptr<QTemporaryDir> m_files;
    std::unique_ptr<notes::app::AppContext> m_context;
    std::unique_ptr<MainWindow> m_window;
    NoteTextEdit* m_edit = nullptr;
    Note m_note;
};

void tst_Images::init()
{
    m_dir = std::make_unique<notes::test::TempDb>();
    m_files = std::make_unique<QTemporaryDir>();
    m_context = notes::test::makeTestContext(m_dir->dirPath());
    (void)await(m_context->folders().ensureDefaultFolder());
    m_note = await(m_context->notes().create(roles::kAllNotesFolderId, {},
                                             QStringLiteral("Trip\nThe view")))
                 .value();
    QApplication::clipboard()->clear();
    openWindow();
}

void tst_Images::cleanup()
{
    m_window = nullptr;
    m_context = nullptr;
    m_files = nullptr;
    m_dir = nullptr;
}

void tst_Images::openWindow()
{
    m_window = std::make_unique<MainWindow>(*m_context);
    m_window->resize(1100, 700);
    m_window->show();
    m_window->activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(m_window.get()));
    m_edit = m_window->findChild<EditorView*>(QStringLiteral("editorPane"))->textEdit();
    QTRY_VERIFY(m_edit->toPlainText().startsWith(QStringLiteral("Trip")));
    QTRY_COMPARE(m_window->findChild<Autosave*>()->trackedNote(), m_note.id);
    m_edit->setFocus();
    QTRY_VERIFY(m_edit->hasFocus());
    m_edit->moveCursor(QTextCursor::End);
}

void tst_Images::flush() const
{
    const QSignalSpy saved(&m_context->notes(), &notes::services::NoteService::noteUpdated);
    m_window->findChild<Autosave*>()->flush();
    QTRY_COMPARE(saved.count(), 1);
}

OpenedNote tst_Images::reopen() const
{
    return await(m_context->notes().open(m_note.id)).value();
}

QString tst_Images::writeImage(const QString& name, const QImage& image) const
{
    const QString path = m_files->filePath(name);
    image.save(path);
    return path;
}

QSizeF tst_Images::shownSize(int position) const
{
    const QTextDocument& document = *m_edit->document();
    QTextCursor cursor(m_edit->document());
    cursor.setPosition(position + 1);
    return ImageObject::displaySize(document, position, cursor.charFormat().toImageFormat());
}

// TC-EDT-020
void tst_Images::pastedImageIsStoredAndShown()
{
    QApplication::clipboard()->setImage(filled(120, 80));
    QTest::keyClick(m_edit, Qt::Key_V, Qt::ControlModifier);

    const QList<ShownImage> images = imagesIn(*m_edit->document());
    QCOMPARE(images.size(), 1);
    QVERIFY(images.first().name.startsWith(QStringLiteral("attachment://")));
    // Drawn from the note's own data, at its size.
    QCOMPARE(shownSize(images.first().position), QSizeF(120, 80));
    QCOMPARE(m_edit->document()
                 ->resource(QTextDocument::ImageResource, QUrl(images.first().name))
                 .value<QImage>()
                 .size(),
             QSize(120, 80));

    flush();
    const OpenedNote opened = reopen();
    QCOMPARE(opened.attachments.size(), 1);
    QCOMPARE(QStringLiteral("attachment://") + opened.attachments.first().uuid,
             images.first().name);
    QCOMPARE(ImageImport::decode(opened.attachments.first().data).size(), QSize(120, 80));
    QCOMPARE(opened.attachments.first().mimeType, QStringLiteral("image/png"));

    // One undo step takes it out again.
    QTest::keyClick(m_edit, Qt::Key_Z, Qt::ControlModifier);
    QVERIFY(imagesIn(*m_edit->document()).isEmpty());
}

// TC-EDT-026
void tst_Images::noteListShowsThumbnail()
{
    auto* list = m_window->findChild<NoteListView*>(QStringLiteral("noteListPane"));
    const auto row = [list]() -> QModelIndex {
        for (int i = 0; i < list->model()->rowCount(); ++i) {
            const QModelIndex index = list->model()->index(i, 0);
            if (!index.data(roles::kIsSectionHeaderRole).toBool()) {
                return index;
            }
        }
        return {};
    };
    QTRY_VERIFY(row().isValid());
    QVERIFY(!row().data(roles::kHasThumbnailRole).toBool());

    m_edit->insertImageFiles({writeImage(QStringLiteral("red.png"), filled(600, 300, Qt::red))});
    flush();
    QTRY_VERIFY(row().data(roles::kHasThumbnailRole).toBool());
    const QImage thumbnail = QImage::fromData(row().data(roles::kThumbnailRole).toByteArray());
    QCOMPARE(thumbnail.size(), QSize(256, 128));

    // And it is drawn: the row's right end is red.
    const QRect rect = list->visualRect(row());
    const QImage shot = list->viewport()->grab(rect).toImage();
    const QColor right = shot.pixelColor(shot.width() - 30, shot.height() / 2);
    QVERIFY2(right.red() > 200 && right.green() < 60 && right.blue() < 60,
             qPrintable(right.name()));
}

void tst_Images::wideImagesFitTheEditor()
{
    m_edit->insertImageFiles({writeImage(QStringLiteral("wide.png"), filled(4000, 1000))});
    const QList<ShownImage> images = imagesIn(*m_edit->document());
    QCOMPARE(images.size(), 1);
    const qreal textWidth = m_edit->document()->textWidth();
    const QSizeF shown = shownSize(images.first().position);
    QVERIFY2(shown.width() <= textWidth, qPrintable(QString::number(shown.width())));
    QVERIFY(shown.width() > textWidth - 80);
    QCOMPARE(qRound(shown.width() / shown.height() * 100), 400); // aspect kept

    // A narrower window shows it smaller; the stored image stays as it was.
    m_window->resize(900, 700);
    QTRY_VERIFY(m_edit->document()->textWidth() < textWidth);
    const QSizeF narrower = shownSize(images.first().position);
    QVERIFY(narrower.width() < shown.width());
    QVERIFY(narrower.width() <= m_edit->document()->textWidth());
    // The layout uses that size too.
    const QRectF laidOut = m_edit->document()->documentLayout()->blockBoundingRect(
        m_edit->document()->findBlock(images.first().position));
    QVERIFY(laidOut.height() >= narrower.height());
    QVERIFY(laidOut.height() < shown.height() + 40);

    // Small images keep their own size.
    m_edit->insertImageFiles({writeImage(QStringLiteral("small.png"), filled(50, 40))});
    QCOMPARE(shownSize(imagesIn(*m_edit->document()).last().position), QSizeF(50, 40));
}

void tst_Images::droppedFilesBecomeImages()
{
    // As a file manager drags them (paste takes the same path as a drop).
    auto* mime = new QMimeData;
    mime->setUrls({QUrl::fromLocalFile(writeImage(QStringLiteral("a.png"), filled(30, 30))),
                   QUrl::fromLocalFile(writeImage(QStringLiteral("b.jpg"), filled(40, 20))),
                   QUrl::fromLocalFile(m_files->filePath(QStringLiteral("notes.txt")))});
    QApplication::clipboard()->setMimeData(mime);
    QTest::keyClick(m_edit, Qt::Key_V, Qt::ControlModifier);
    const QList<ShownImage> images = imagesIn(*m_edit->document());
    QCOMPARE(images.size(), 2); // the text file isn't an image
    QCOMPARE(shownSize(images.at(1).position), QSizeF(40, 20));
    QVERIFY(!m_edit->toPlainText().contains(QStringLiteral("file:")));

    flush();
    const OpenedNote opened = reopen();
    QCOMPARE(opened.attachments.size(), 2);
    QCOMPARE(opened.attachments.at(1).mimeType, QStringLiteral("image/jpeg"));
    QCOMPARE(opened.attachments.at(1).fileName, QStringLiteral("b.jpg"));

    // Links that aren't image files still paste as text.
    auto* links = new QMimeData;
    links->setUrls({QUrl(QStringLiteral("https://example.com/photo.png"))});
    links->setText(QStringLiteral("https://example.com/photo.png"));
    QApplication::clipboard()->setMimeData(links);
    QTest::keyClick(m_edit, Qt::Key_V, Qt::ControlModifier);
    QVERIFY(m_edit->toPlainText().endsWith(QStringLiteral("https://example.com/photo.png")));
    QCOMPARE(imagesIn(*m_edit->document()).size(), 2);
}

void tst_Images::attachActionAddsPickedImages()
{
    QAction* attach = m_window->actions().action(ActionId::Attachment);
    QVERIFY(attach->isEnabled());
    const QString path = writeImage(QStringLiteral("picked.png"), filled(64, 48));
    int asked = 0;
    m_window->setImagePicker([&](QWidget*) {
        ++asked;
        return QStringList{path};
    });
    attach->trigger();
    QCOMPARE(asked, 1);
    QCOMPARE(imagesIn(*m_edit->document()).size(), 1);

    // Cancelled: nothing.
    m_window->setImagePicker([](QWidget*) { return QStringList(); });
    attach->trigger();
    QCOMPARE(imagesIn(*m_edit->document()).size(), 1);
}

void tst_Images::copyPasteMakesANewImage()
{
    m_edit->insertImageFiles({writeImage(QStringLiteral("copy.png"), filled(70, 50))});
    const QString original = imagesIn(*m_edit->document()).first().name;

    // Select the image and copy it.
    QTextCursor cursor = m_edit->textCursor();
    cursor.movePosition(QTextCursor::Left, QTextCursor::KeepAnchor);
    m_edit->setTextCursor(cursor);
    m_edit->copy();
    const QMimeData* copied = QApplication::clipboard()->mimeData();
    QVERIFY(copied->hasFormat(QLatin1StringView(NoteTextEdit::kFragmentMime)));
    QCOMPARE(qvariant_cast<QImage>(copied->imageData()).size(), QSize(70, 50)); // for other apps

    m_edit->moveCursor(QTextCursor::End);
    m_edit->paste();
    const QList<ShownImage> images = imagesIn(*m_edit->document());
    QCOMPARE(images.size(), 2);
    QCOMPARE(images.first().name, original);
    QVERIFY(images.last().name != original); // its own attachment
    QCOMPARE(shownSize(images.last().position), QSizeF(70, 50));

    flush();
    QCOMPARE(reopen().attachments.size(), 2);

    // Into another note: shown and stored there too.
    const Note other =
        await(m_context->notes().create(roles::kAllNotesFolderId, {}, QStringLiteral("Other")))
            .value();
    auto* list = m_window->findChild<NoteListView*>(QStringLiteral("noteListPane"));
    QTRY_VERIFY(list->selectNote(other.id));
    QTRY_COMPARE(m_window->findChild<Autosave*>()->trackedNote(), other.id);
    m_edit->moveCursor(QTextCursor::End);
    m_edit->paste();
    const QList<ShownImage> pasted = imagesIn(*m_edit->document());
    QCOMPARE(pasted.size(), 1);
    QCOMPARE(shownSize(pasted.first().position), QSizeF(70, 50));
    flush();
    QCOMPARE(await(m_context->notes().open(other.id)).value().attachments.size(), 1);
    QCOMPARE(reopen().attachments.size(), 2); // the first note's are its own
}

void tst_Images::undoAfterSaveKeepsTheImage()
{
    m_edit->insertImageFiles({writeImage(QStringLiteral("undo.png"), filled(30, 30))});
    flush();
    QCOMPARE(reopen().attachments.size(), 1);

    QTest::keyClick(m_edit, Qt::Key_Backspace);
    QVERIFY(imagesIn(*m_edit->document()).isEmpty());
    flush();
    QVERIFY(reopen().attachments.isEmpty()); // TC-EDT-025: cleaned up

    QTest::keyClick(m_edit, Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(imagesIn(*m_edit->document()).size(), 1);
    flush();
    const OpenedNote opened = reopen();
    QCOMPARE(opened.attachments.size(), 1);
    QCOMPARE(ImageImport::decode(opened.attachments.first().data).size(), QSize(30, 30));
}

void tst_Images::imagesSurviveReopening()
{
    m_edit->insertImageFiles({writeImage(QStringLiteral("keep.png"), filled(90, 60))});
    flush();
    m_window = nullptr; // closed: everything from the database again
    openWindow();
    const QList<ShownImage> images = imagesIn(*m_edit->document());
    QCOMPARE(images.size(), 1);
    QCOMPARE(shownSize(images.first().position), QSizeF(90, 60));
}

// TC-EDT-023 in the editor: refused, said why, nothing inserted.
void tst_Images::tooLargeImageIsReported()
{
    NoteTextEdit edit;
    edit.setNoteBody({}, QStringLiteral("Text"));
    const QSignalSpy rejected(&edit, &NoteTextEdit::imageRejected);
    const QString path = m_files->filePath(QStringLiteral("huge.png"));
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(png(filled(10, 10)));
        QVERIFY(file.resize(qint64{30} * 1024 * 1024));
    }
    edit.insertImageFiles({path, writeImage(QStringLiteral("ok.png"), filled(10, 10))});
    QCOMPARE(rejected.count(), 1);
    QVERIFY(rejected.first().first().toString().contains(QStringLiteral("25")));
    QCOMPARE(imagesIn(*edit.document()).size(), 1); // the other one went in
}

// Privacy: images come only from the note's own attachments.
void tst_Images::nothingLoadsFromFilesOrTheNetwork()
{
    const QString path = writeImage(QStringLiteral("local.png"), filled(20, 20));
    QTextDocument* document = m_edit->document();
    // Without an answer from the editor, QTextDocument would read the file.
    QVERIFY(document->resource(QTextDocument::ImageResource, QUrl::fromLocalFile(path))
                .value<QImage>()
                .isNull());
    QVERIFY(document
                ->resource(QTextDocument::ImageResource,
                           QUrl(QStringLiteral("https://example.com/pixel.png")))
                .value<QImage>()
                .isNull());
    QVERIFY(document->resource(QTextDocument::StyleSheetResource, QUrl::fromLocalFile(path))
                .toByteArray()
                .isEmpty());
    // An image the note has no data for shows as a placeholder, not an error.
    QTextImageFormat missing;
    missing.setName(QStringLiteral("attachment://00000000-0000-0000-0000-000000000000"));
    QVERIFY(ImageObject::displaySize(*document, 0, missing).isValid());
}

void tst_Images::titleSkipsImages()
{
    m_edit->moveCursor(QTextCursor::Start);
    m_edit->insertImageFiles({writeImage(QStringLiteral("top.png"), filled(20, 20))});
    QVERIFY(m_edit->toPlainText().contains(QChar::ObjectReplacementCharacter));
    QCOMPARE(m_edit->notePlainText(), QStringLiteral("Trip\nThe view"));
    flush();
    QCOMPARE(reopen().note.title, QStringLiteral("Trip"));
}

void tst_Images::textNextToAnImageStaysText()
{
    m_edit->insertImageFiles({writeImage(QStringLiteral("next.png"), filled(20, 20))});
    QTest::keyClicks(m_edit, QStringLiteral("abc"));
    QApplication::clipboard()->setText(QStringLiteral("xyz"));
    m_edit->moveCursor(QTextCursor::Left);
    m_edit->moveCursor(QTextCursor::Left);
    m_edit->moveCursor(QTextCursor::Left); // right after the image
    QTest::keyClick(m_edit, Qt::Key_V, Qt::ControlModifier);
    QCOMPARE(imagesIn(*m_edit->document()).size(), 1);
    QVERIFY(m_edit->notePlainText().endsWith(QStringLiteral("xyzabc")));
}

void tst_Images::otherNotesDontSeeTheImages()
{
    m_edit->insertImageFiles({writeImage(QStringLiteral("mine.png"), filled(20, 20))});
    const QUrl name(imagesIn(*m_edit->document()).first().name);
    QVERIFY(
        !m_edit->document()->resource(QTextDocument::ImageResource, name).value<QImage>().isNull());
    flush();

    const Note other =
        await(m_context->notes().create(roles::kAllNotesFolderId, {}, QStringLiteral("Other")))
            .value();
    auto* list = m_window->findChild<NoteListView*>(QStringLiteral("noteListPane"));
    QTRY_VERIFY(list->selectNote(other.id));
    QTRY_COMPARE(m_window->findChild<Autosave*>()->trackedNote(), other.id);
    // Neither its data nor its decoded image stays behind.
    QVERIFY(
        m_edit->document()->resource(QTextDocument::ImageResource, name).value<QImage>().isNull());
}

QTEST_MAIN(tst_Images)
#include "tst_images.moc"
