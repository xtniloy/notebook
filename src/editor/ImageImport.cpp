#include "editor/ImageImport.h"

#include "models/Uuid.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QImageWriter>
#include <QLocale>

#include <array>
#include <utility>

namespace notes::editor {

namespace {

// Kept as they are; anything else Qt reads is stored as PNG.
constexpr std::array kKeptFormats{
    std::pair{"png", "image/png"}, std::pair{"jpeg", "image/jpeg"}, std::pair{"jpg", "image/jpeg"},
    std::pair{"gif", "image/gif"}, std::pair{"webp", "image/webp"},
};

constexpr int kThumbnailJpegQuality = 80;

QString tr(const char* text)
{
    return QCoreApplication::translate("notes::editor::ImageImport", text);
}

QByteArray encode(const QImage& image, const char* format, int quality = -1)
{
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    QImageWriter writer(&buffer, format);
    writer.setQuality(quality);
    if (!writer.write(image)) {
        return {};
    }
    return bytes;
}

// Behind the header check, Qt's own allocation limit (256 MB by default, the
// same as kMaxPixels at 4 bytes a pixel) stops formats whose header doesn't
// give the size up front.
void prepare(QImageReader& reader)
{
    reader.setAutoTransform(true);
}

bool tooManyPixels(QSize size)
{
    return size.isValid() && qint64{size.width()} * size.height() > ImageImport::kMaxPixels;
}

} // namespace

ImageImport::Result ImageImport::fromData(const QByteArray& data, const QString& fileName)
{
    if (data.size() > kMaxFileBytes) {
        return {.attachment = {}, .error = tooLargeMessage()};
    }
    QBuffer buffer;
    buffer.setData(data);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer);
    prepare(reader);
    const QByteArray format = reader.format().toLower();
    if (format.isEmpty()) {
        return {.attachment = {}, .error = unreadableMessage()};
    }
    // From the header alone: nothing decoded yet.
    if (const QSize size = reader.size(); tooManyPixels(size)) {
        return {.attachment = {}, .error = tooManyPixelsMessage(size)};
    }
    const QImage image = reader.read();
    if (image.isNull()) {
        return {.attachment = {}, .error = unreadableMessage()};
    }

    models::Attachment attachment;
    attachment.uuid = models::newUuid();
    attachment.fileName = fileName;
    for (const auto& [name, mime] : kKeptFormats) {
        if (format == name) {
            attachment.mimeType = QString::fromLatin1(mime);
            attachment.data = data;
        }
    }
    if (attachment.data.isEmpty()) {
        attachment.mimeType = QStringLiteral("image/png");
        attachment.data = encode(image, "png");
        if (attachment.data.isEmpty() || attachment.data.size() > kMaxFileBytes) {
            return {.attachment = {}, .error = tooLargeMessage()};
        }
    }
    attachment.sizeBytes = attachment.data.size();
    attachment.sha256 = QString::fromLatin1(
        QCryptographicHash::hash(attachment.data, QCryptographicHash::Sha256).toHex());
    attachment.thumbnail = thumbnailFor(image);
    return {.attachment = attachment, .error = {}};
}

ImageImport::Result ImageImport::fromFile(const QString& path)
{
    const QFileInfo info(path);
    if (info.size() > kMaxFileBytes) {
        return {.attachment = {}, .error = tooLargeMessage()};
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {.attachment = {}, .error = unreadableMessage()};
    }
    // At most one byte over the limit: a file that grew since is still refused.
    return fromData(file.read(kMaxFileBytes + 1), info.fileName());
}

ImageImport::Result ImageImport::fromImage(const QImage& image)
{
    if (image.isNull()) {
        return {.attachment = {}, .error = unreadableMessage()};
    }
    if (tooManyPixels(image.size())) {
        return {.attachment = {}, .error = tooManyPixelsMessage(image.size())};
    }
    const QByteArray png = encode(image, "png");
    if (png.isEmpty()) {
        return {.attachment = {}, .error = unreadableMessage()};
    }
    //: File name of an image pasted from the clipboard.
    return fromData(png, tr("Pasted Image.png"));
}

QImage ImageImport::decode(const QByteArray& data)
{
    if (data.size() > kMaxFileBytes) {
        return {};
    }
    QBuffer buffer;
    buffer.setData(data);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer);
    prepare(reader);
    if (tooManyPixels(reader.size())) {
        return {};
    }
    return reader.read();
}

QByteArray ImageImport::thumbnailFor(const QImage& image)
{
    if (image.isNull()) {
        return {};
    }
    QImage small = image;
    if (image.width() > kThumbnailSide || image.height() > kThumbnailSide) {
        small = image.scaled(kThumbnailSide, kThumbnailSide, Qt::KeepAspectRatio,
                             Qt::SmoothTransformation);
    }
    if (small.hasAlphaChannel()) {
        return encode(small, "png");
    }
    return encode(small.convertToFormat(QImage::Format_RGB32), "jpeg", kThumbnailJpegQuality);
}

bool ImageImport::isImageFile(const QString& fileName)
{
    const QByteArray suffix = QFileInfo(fileName).suffix().toLower().toLatin1();
    return !suffix.isEmpty() && QImageReader::supportedImageFormats().contains(suffix);
}

QString ImageImport::tooLargeMessage()
{
    //: %1 is a size, e.g. "25 MB".
    return tr("Images can be up to %1.")
        .arg(QLocale().formattedDataSize(kMaxFileBytes, 0, QLocale::DataSizeTraditionalFormat));
}

QString ImageImport::unreadableMessage()
{
    return tr("The image can't be read. It may be damaged or in a format that isn't supported.");
}

QString ImageImport::tooManyPixelsMessage(QSize size)
{
    //: %1 x %2 are the image's width and height in pixels.
    return tr("The image is too large (%1 × %2 pixels).")
        .arg(QLocale().toString(size.width()), QLocale().toString(size.height()));
}

} // namespace notes::editor
