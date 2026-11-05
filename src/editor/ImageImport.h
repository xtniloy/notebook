#pragma once

#include "models/Attachment.h"

#include <QByteArray>
#include <QImage>
#include <QSize>
#include <QString>

namespace notes::editor {

// Images coming into a note (task 3.7): checked, then turned into an
// attachment with its thumbnail. Decoding stored images for display goes
// through the same limits.
//
// Limits: files over kMaxFileBytes are refused before being read, and images
// whose header claims more than kMaxPixels are refused before any pixel memory
// is allocated (a "decompression bomb": a small file claiming a huge image).
class ImageImport
{
public:
    static constexpr qint64 kMaxFileBytes = qint64{25} * 1024 * 1024;
    // 64 megapixels: beyond any camera photo, and 256 MB decoded.
    static constexpr qint64 kMaxPixels = qint64{64} * 1024 * 1024;
    // Thumbnails (the note list) fit in a square of this side.
    static constexpr int kThumbnailSide = 256;

    struct Result
    {
        models::Attachment attachment; // without noteId and createdAt
        QString error;                 // for the user; empty on success

        [[nodiscard]] bool ok() const { return error.isEmpty(); }
    };

    // An image file's bytes, as dropped, pasted or picked. PNG, JPEG, GIF and
    // WebP are kept as they are; other formats Qt reads (BMP, TIFF, ...) are
    // stored as PNG.
    [[nodiscard]] static Result fromData(const QByteArray& data, const QString& fileName);
    // A file on disk; its size is checked before it is read.
    [[nodiscard]] static Result fromFile(const QString& path);
    // An image without a file (the clipboard), stored as PNG.
    [[nodiscard]] static Result fromImage(const QImage& image);

    // A stored image, for display; a null image if it is unreadable or over
    // the limits. EXIF orientation applied.
    [[nodiscard]] static QImage decode(const QByteArray& data);

    // The thumbnail for `image`: at most kThumbnailSide on its longer side,
    // PNG if it has transparency, JPEG otherwise.
    [[nodiscard]] static QByteArray thumbnailFor(const QImage& image);

    // Whether `fileName` looks like an image Qt can read, by its suffix.
    [[nodiscard]] static bool isImageFile(const QString& fileName);

private:
    [[nodiscard]] static QString tooLargeMessage();
    [[nodiscard]] static QString unreadableMessage();
    [[nodiscard]] static QString tooManyPixelsMessage(QSize size);
};

} // namespace notes::editor
