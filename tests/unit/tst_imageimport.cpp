// Images coming into a note (task 3.7): TC-EDT-022 thumbnails, TC-EDT-023 the
// file size limit, TC-EDT-024 decompression bombs refused from the header.

#include "editor/ImageImport.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>
#include <QTest>
#include <QtEndian>

#include <array>

using notes::editor::ImageImport;

namespace {

QByteArray encoded(const QImage& image, const char* format)
{
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, format);
    return bytes;
}

QImage filled(int width, int height, QImage::Format format = QImage::Format_RGB32)
{
    QImage image(width, height, format);
    image.fill(QColor(200, 80, 40));
    return image;
}

// zlib's CRC-32, for PNG chunks.
quint32 crc32(const QByteArray& data)
{
    quint32 crc = 0xFFFFFFFFU;
    for (const char byte : data) {
        crc ^= static_cast<quint8>(byte);
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 1U) != 0 ? (crc >> 1U) ^ 0xEDB88320U : crc >> 1U;
        }
    }
    return ~crc;
}

QByteArray bigEndian(quint32 value)
{
    std::array<char, 4> bytes{};
    qToBigEndian(value, bytes.data());
    return {bytes.data(), 4};
}

// A PNG of a few bytes whose header claims `side` x `side` pixels: a valid
// signature and IHDR chunk, then a tiny (wrong) image data chunk.
QByteArray pngBomb(quint32 side)
{
    QByteArray ihdr = QByteArrayLiteral("IHDR") + bigEndian(side) + bigEndian(side);
    ihdr += QByteArray::fromHex("0806000000"); // 8-bit RGBA, no interlace
    QByteArray png = QByteArray::fromHex("89504E470D0A1A0A");
    png += bigEndian(13) + ihdr + bigEndian(crc32(ihdr));
    const QByteArray idat = QByteArrayLiteral("IDAT") + QByteArray::fromHex("789C030000000001");
    png += bigEndian(8) + idat + bigEndian(crc32(idat));
    const QByteArray iend = QByteArrayLiteral("IEND");
    png += bigEndian(0) + iend + bigEndian(crc32(iend));
    return png;
}

} // namespace

class tst_ImageImport : public QObject
{
    Q_OBJECT

private slots:
    void storesImagesAsTheyAre();
    void otherFormatsBecomePng();
    void clipboardImagesBecomePng();
    void unreadableDataIsRefused();
    void thumbnailFitsIn256();
    void thumbnailKeepsTransparency();
    void oversizedFileIsRefusedUnread();
    void oversizedDataIsRefused();
    void hugeHeaderIsRefusedUndecoded_data();
    void hugeHeaderIsRefusedUndecoded();
    void recognizesImageFiles();
};

void tst_ImageImport::storesImagesAsTheyAre()
{
    const QByteArray png = encoded(filled(40, 30), "png");
    const ImageImport::Result result = ImageImport::fromData(png, QStringLiteral("photo.png"));
    QVERIFY2(result.ok(), qPrintable(result.error));
    const notes::models::Attachment& attachment = result.attachment;
    QCOMPARE(attachment.data, png); // byte for byte
    QCOMPARE(attachment.mimeType, QStringLiteral("image/png"));
    QCOMPARE(attachment.fileName, QStringLiteral("photo.png"));
    QCOMPARE(attachment.sizeBytes, png.size());
    QCOMPARE(
        attachment.sha256,
        QString::fromLatin1(QCryptographicHash::hash(png, QCryptographicHash::Sha256).toHex()));
    QCOMPARE(attachment.uuid.size(), 36);
    QCOMPARE(attachment.id, qint64{0}); // not stored yet

    const QByteArray jpeg = encoded(filled(40, 30), "jpeg");
    const ImageImport::Result photo = ImageImport::fromData(jpeg, QStringLiteral("a.jpg"));
    QVERIFY(photo.ok());
    QCOMPARE(photo.attachment.mimeType, QStringLiteral("image/jpeg"));
    QCOMPARE(photo.attachment.data, jpeg);
    QVERIFY(photo.attachment.uuid != attachment.uuid);
}

void tst_ImageImport::otherFormatsBecomePng()
{
    const QByteArray bmp = encoded(filled(20, 10), "bmp");
    const ImageImport::Result result = ImageImport::fromData(bmp, QStringLiteral("old.bmp"));
    QVERIFY2(result.ok(), qPrintable(result.error));
    QCOMPARE(result.attachment.mimeType, QStringLiteral("image/png"));
    QVERIFY(result.attachment.data.startsWith(QByteArray::fromHex("89504E47")));
    QCOMPARE(ImageImport::decode(result.attachment.data).size(), QSize(20, 10));
}

void tst_ImageImport::clipboardImagesBecomePng()
{
    const ImageImport::Result result = ImageImport::fromImage(filled(30, 20));
    QVERIFY2(result.ok(), qPrintable(result.error));
    QCOMPARE(result.attachment.mimeType, QStringLiteral("image/png"));
    QVERIFY(result.attachment.fileName.endsWith(QStringLiteral(".png")));
    QCOMPARE(ImageImport::decode(result.attachment.data).size(), QSize(30, 20));
    QVERIFY(!ImageImport::fromImage(QImage()).ok());
}

void tst_ImageImport::unreadableDataIsRefused()
{
    const ImageImport::Result result =
        ImageImport::fromData(QByteArrayLiteral("not an image at all"), QStringLiteral("x.png"));
    QVERIFY(!result.ok());
    QVERIFY(result.attachment.data.isEmpty());
    // A PNG cut short: the header reads, the pixels don't.
    const QByteArray png = encoded(filled(200, 200), "png");
    QVERIFY(!ImageImport::fromData(png.first(png.size() / 3), QStringLiteral("cut.png")).ok());
    QVERIFY(ImageImport::decode(QByteArrayLiteral("garbage")).isNull());
}

// TC-EDT-022
void tst_ImageImport::thumbnailFitsIn256()
{
    const ImageImport::Result wide =
        ImageImport::fromData(encoded(filled(1600, 900), "png"), QStringLiteral("wide.png"));
    QVERIFY(wide.ok());
    const QImage thumbnail = QImage::fromData(wide.attachment.thumbnail);
    QVERIFY(!thumbnail.isNull());
    QCOMPARE(thumbnail.width(), ImageImport::kThumbnailSide);
    QCOMPARE(thumbnail.height(), 144); // 900 * 256 / 1600: aspect kept

    const QImage tall = QImage::fromData(ImageImport::thumbnailFor(filled(300, 3000)));
    QCOMPARE(qMax(tall.width(), tall.height()), ImageImport::kThumbnailSide);
    // Opaque images: JPEG, small.
    QVERIFY(wide.attachment.thumbnail.startsWith(QByteArray::fromHex("FFD8")));

    // Small images aren't enlarged.
    QCOMPARE(QImage::fromData(ImageImport::thumbnailFor(filled(64, 32))).size(), QSize(64, 32));
}

void tst_ImageImport::thumbnailKeepsTransparency()
{
    QImage icon(400, 400, QImage::Format_ARGB32);
    icon.fill(Qt::transparent);
    const QByteArray thumbnail = ImageImport::thumbnailFor(icon);
    QVERIFY(thumbnail.startsWith(QByteArray::fromHex("89504E47"))); // PNG
    QVERIFY(QImage::fromData(thumbnail).hasAlphaChannel());
}

// TC-EDT-023
void tst_ImageImport::oversizedFileIsRefusedUnread()
{
    const QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("huge.png"));
    {
        // A 30 MB file: a real PNG at the start, so only its size is wrong.
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(encoded(filled(10, 10), "png"));
        QVERIFY(file.resize(qint64{30} * 1024 * 1024));
    }
    const ImageImport::Result result = ImageImport::fromFile(path);
    QVERIFY(!result.ok());
    QVERIFY2(result.error.contains(QStringLiteral("25")), qPrintable(result.error));
    QVERIFY(result.attachment.data.isEmpty());

    // Just under the limit is fine.
    const QString fine = dir.filePath(QStringLiteral("fine.png"));
    QVERIFY(QFile::copy(path, fine));
    {
        QFile file(fine);
        QVERIFY(file.open(QIODevice::ReadWrite));
        QVERIFY(file.resize(ImageImport::kMaxFileBytes));
    }
    QVERIFY2(ImageImport::fromFile(fine).ok(), qPrintable(ImageImport::fromFile(fine).error));
}

void tst_ImageImport::oversizedDataIsRefused()
{
    QByteArray data = encoded(filled(10, 10), "png");
    data.resize(ImageImport::kMaxFileBytes + 1, '\0');
    QVERIFY(!ImageImport::fromData(data, QStringLiteral("big.png")).ok());
    QVERIFY(ImageImport::decode(data).isNull());
}

// TC-EDT-024
void tst_ImageImport::hugeHeaderIsRefusedUndecoded_data()
{
    QTest::addColumn<QByteArray>("data");
    QTest::newRow("100,000 x 100,000") << pngBomb(100'000);
    QTest::newRow("just over the limit") << pngBomb(8193); // 67.1 megapixels
}

void tst_ImageImport::hugeHeaderIsRefusedUndecoded()
{
    QFETCH(QByteArray, data);
    QVERIFY(data.size() < 200); // a few bytes claiming 40 GB of pixels
    const ImageImport::Result result = ImageImport::fromData(data, QStringLiteral("bomb"));
    QVERIFY(!result.ok());
    // Refused for its claimed size, read from the header, before any
    // decoding (which would have failed as "can't be read" instead).
    QVERIFY2(result.error.contains(QStringLiteral("pixels")), qPrintable(result.error));
    QVERIFY(ImageImport::decode(data).isNull());
}

void tst_ImageImport::recognizesImageFiles()
{
    QVERIFY(ImageImport::isImageFile(QStringLiteral("/home/me/Photo.JPG")));
    QVERIFY(ImageImport::isImageFile(QStringLiteral("shot.png")));
    QVERIFY(!ImageImport::isImageFile(QStringLiteral("report.pdf")));
    QVERIFY(!ImageImport::isImageFile(QStringLiteral("README")));
}

QTEST_MAIN(tst_ImageImport)
#include "tst_imageimport.moc"
