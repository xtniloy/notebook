#pragma once

#include <QCache>
#include <QObject>
#include <QPixmap>
#include <QSizeF>
#include <QTextObjectInterface>

class QImage;
class QTextDocument;
class QTextImageFormat;

namespace notes::editor {

// Lays out and draws the images in a note (task 3.7), in place of Qt's own
// image handler: an image wider than the text is shown scaled down to the
// text's width, keeping its aspect ratio. Only the display scales; the stored
// image and the document stay as they are, so a wider window shows it larger
// again (up to its own size).
//
// Images come from QTextDocument::resource(), which asks the editor
// (NoteTextEdit::loadResource()). Register with registerOn().
class ImageObject : public QObject, public QTextObjectInterface
{
    Q_OBJECT
    Q_INTERFACES(QTextObjectInterface)
    Q_DISABLE_COPY_MOVE(ImageObject)

public:
    explicit ImageObject(QObject* parent = nullptr);
    ~ImageObject() override;

    // Makes this the image handler of `document`'s current layout.
    void registerOn(QTextDocument& document);

    // The size `format`'s image at `position` is shown at in `document`: its
    // own (or the size stored with it), scaled down to fit the text width.
    [[nodiscard]] static QSizeF displaySize(const QTextDocument& document, int position,
                                            const QTextImageFormat& format);

    [[nodiscard]] QSizeF intrinsicSize(QTextDocument* document, int posInDocument,
                                       const QTextFormat& format) override;
    void drawObject(QPainter* painter, const QRectF& rect, QTextDocument* document,
                    int posInDocument, const QTextFormat& format) override;

private:
    // Scaled copies of large images, so a repaint doesn't scale a photo again.
    QCache<QString, QPixmap> m_scaled;
};

} // namespace notes::editor
