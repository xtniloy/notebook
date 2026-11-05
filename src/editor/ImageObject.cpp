#include "editor/ImageObject.h"

#include <QAbstractTextDocumentLayout>
#include <QImage>
#include <QPaintDevice>
#include <QPainter>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextFrame>
#include <QTextList>
#include <QUrl>

#include <algorithm>

namespace notes::editor {

namespace {

// Room the scaled copies may take (QCache cost: KiB).
constexpr int kScaledCacheKiB = 64 * 1024;
constexpr int kBytesPerKiB = 1024;
// An image that can't be shown (missing from the note, unreadable).
constexpr QSizeF kMissingSize(96, 64);
constexpr qreal kMissingRadius = 6;
constexpr int kMissingAlpha = 40;

QImage imageOf(const QTextDocument& document, const QTextImageFormat& format)
{
    const QVariant resource = document.resource(QTextDocument::ImageResource, QUrl(format.name()));
    if (resource.userType() == QMetaType::QImage) {
        return resource.value<QImage>();
    }
    if (resource.userType() == QMetaType::QPixmap) {
        return resource.value<QPixmap>().toImage();
    }
    return {};
}

// The width the text of the paragraph at `position` has.
qreal availableWidth(const QTextDocument& document, int position)
{
    const qreal textWidth = document.textWidth();
    if (textWidth <= 0) {
        return -1; // no width set: nothing to fit
    }
    const QTextFrameFormat frame = document.rootFrame()->frameFormat();
    qreal width = textWidth - frame.leftMargin() - frame.rightMargin() -
                  (2 * (frame.padding() + frame.border()));
    const QTextBlock block = document.findBlock(position);
    const QTextBlockFormat format = block.blockFormat();
    width -= format.leftMargin() + format.rightMargin() + std::max(0.0, format.textIndent());
    int indent = format.indent();
    if (const QTextList* list = block.textList()) {
        indent += list->format().indent();
    }
    width -= indent * document.indentWidth();
    return std::max(1.0, width);
}

} // namespace

ImageObject::ImageObject(QObject* parent)
    : QObject(parent)
    , m_scaled(kScaledCacheKiB)
{}

ImageObject::~ImageObject() = default;

void ImageObject::registerOn(QTextDocument& document)
{
    document.documentLayout()->registerHandler(QTextFormat::ImageObject, this);
}

QSizeF ImageObject::displaySize(const QTextDocument& document, int position,
                                const QTextImageFormat& format)
{
    const QImage image = imageOf(document, format);
    if (image.isNull()) {
        return kMissingSize;
    }
    QSizeF size = image.deviceIndependentSize();
    // A size stored with the image (NoteFormat's "w" / "h") wins, keeping the
    // aspect ratio when only one is given.
    const qreal width = format.width();
    const qreal height = format.height();
    if (width > 0 && height > 0) {
        size = QSizeF(width, height);
    } else if (width > 0) {
        size = QSizeF(width, size.height() * width / size.width());
    } else if (height > 0) {
        size = QSizeF(size.width() * height / size.height(), height);
    }
    const qreal available = availableWidth(document, position);
    if (available > 0 && size.width() > available) {
        size = QSizeF(available, size.height() * available / size.width());
    }
    return size;
}

QSizeF ImageObject::intrinsicSize(QTextDocument* document, int posInDocument,
                                  const QTextFormat& format)
{
    return displaySize(*document, posInDocument, format.toImageFormat());
}

void ImageObject::drawObject(QPainter* painter, const QRectF& rect, QTextDocument* document,
                             int /*posInDocument*/, const QTextFormat& format)
{
    const QTextImageFormat imageFormat = format.toImageFormat();
    const QImage image = imageOf(*document, imageFormat);
    if (image.isNull()) {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(128, 128, 128, kMissingAlpha));
        painter->drawRoundedRect(rect, kMissingRadius, kMissingRadius);
        painter->restore();
        return;
    }
    const qreal ratio = painter->device() != nullptr ? painter->device()->devicePixelRatioF() : 1.0;
    const QSize target = (rect.size() * ratio).toSize();
    if (target == image.size() || target.isEmpty()) {
        painter->drawImage(rect, image);
        return;
    }
    // Scaled once per size, smoothly; drawing a photo scaled on every repaint
    // would be slow and look rough.
    const QString key =
        QStringLiteral("%1@%2x%3").arg(imageFormat.name()).arg(target.width()).arg(target.height());
    QPixmap scaled;
    if (const QPixmap* cached = m_scaled.object(key)) {
        scaled = *cached;
    } else {
        scaled = QPixmap::fromImage(
            image.scaled(target, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
        const auto cost = static_cast<qsizetype>(
            std::max<qint64>(1, qint64{scaled.width()} * scaled.height() * 4 / kBytesPerKiB));
        // The cache owns its copy (implicitly shared), and may drop it at once
        // when it is larger than the whole cache.
        m_scaled.insert(key, new QPixmap(scaled), cost);
    }
    painter->drawPixmap(rect, scaled, QRectF(scaled.rect()));
}

} // namespace notes::editor
