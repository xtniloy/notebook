#include "ui/Icons.h"

#include <QFile>
#include <QGuiApplication>
#include <QIconEngine>
#include <QPainter>
#include <QPalette>
#include <QPixmapCache>
#include <QSvgRenderer>

#include <array>

namespace notes::ui {

namespace {

// No `default:` so the compiler flags any Icon without a file.
const char* iconFileName(Icon icon)
{
    switch (icon) {
    case Icon::SidebarToggle:
        return "panel-left";
    case Icon::ListView:
        return "list";
    case Icon::GalleryView:
        return "layout-grid";
    case Icon::Trash:
        return "trash";
    case Icon::NewNote:
        return "square-pen";
    case Icon::Format:
        return "a-large-small";
    case Icon::Checklist:
        return "list-checks";
    case Icon::Table:
        return "table";
    case Icon::Attachment:
        return "paperclip";
    case Icon::Lock:
        return "lock";
    case Icon::Unlock:
        return "lock-open";
    case Icon::Share:
        return "share";
    case Icon::Search:
        return "search";
    case Icon::Clear:
        return "x";
    case Icon::Folder:
        return "folder";
    case Icon::NewFolder:
        return "folder-plus";
    case Icon::SmartFolder:
        return "folder-cog";
    case Icon::Tag:
        return "hash";
    case Icon::Pin:
        return "pin";
    case Icon::OnMyComputer:
        return "monitor";
    case Icon::Cloud:
        return "cloud";
    case Icon::Expand:
        return "chevron-right";
    case Icon::Collapse:
        return "chevron-down";
    case Icon::Count:
        break;
    }
    return nullptr;
}

// Renders a Lucide SVG in the current palette's colors. Lucide strokes with
// `currentColor`, which QtSvg would draw black (invisible in the dark theme), so
// it is replaced at paint time: icons follow theme changes without being rebuilt.
class ThemedSvgIconEngine : public QIconEngine
{
public:
    explicit ThemedSvgIconEngine(const QString& path)
        : m_path(path)
    {
        QFile file(path);
        if (file.open(QIODevice::ReadOnly)) {
            m_svg = file.readAll();
        }
    }

    void paint(QPainter* painter, const QRect& rect, QIcon::Mode mode, QIcon::State state) override
    {
        const qreal scale =
            painter->device() != nullptr ? painter->device()->devicePixelRatioF() : 1.0;
        painter->drawPixmap(rect, scaledPixmap(rect.size(), mode, state, scale));
    }

    QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override
    {
        return scaledPixmap(size, mode, state, 1.0);
    }

    QPixmap scaledPixmap(const QSize& size, QIcon::Mode mode, QIcon::State /*state*/,
                         qreal scale) override
    {
        if (m_svg.isEmpty() || size.isEmpty()) {
            return {};
        }
        const QColor color = colorFor(mode);
        const QSize pixels = size * scale;
        const QString key = QStringLiteral("notes-icon:%1:%2x%3:%4")
                                .arg(m_path)
                                .arg(pixels.width())
                                .arg(pixels.height())
                                .arg(color.rgba(), 0, 16);
        QPixmap pixmap;
        if (!QPixmapCache::find(key, &pixmap)) {
            QByteArray svg = m_svg;
            svg.replace("currentColor", color.name(QColor::HexRgb).toLatin1());
            QSvgRenderer renderer(svg);
            QImage image(pixels, QImage::Format_ARGB32_Premultiplied);
            image.fill(Qt::transparent);
            QPainter imagePainter(&image);
            renderer.render(&imagePainter);
            imagePainter.end();
            pixmap = QPixmap::fromImage(image);
            QPixmapCache::insert(key, pixmap);
        }
        pixmap.setDevicePixelRatio(scale);
        return pixmap;
    }

    [[nodiscard]] QIconEngine* clone() const override { return new ThemedSvgIconEngine(*this); }
    [[nodiscard]] QString key() const override { return QStringLiteral("notes-themed-svg"); }
    bool isNull() override { return m_svg.isEmpty(); }

    ~ThemedSvgIconEngine() override = default;
    ThemedSvgIconEngine& operator=(const ThemedSvgIconEngine&) = delete;
    ThemedSvgIconEngine(ThemedSvgIconEngine&&) = delete;
    ThemedSvgIconEngine& operator=(ThemedSvgIconEngine&&) = delete;

private:
    // For clone() only.
    ThemedSvgIconEngine(const ThemedSvgIconEngine&) = default;

    static QColor colorFor(QIcon::Mode mode)
    {
        const QPalette palette = QGuiApplication::palette();
        switch (mode) {
        case QIcon::Disabled:
            return palette.color(QPalette::Disabled, QPalette::WindowText);
        case QIcon::Selected:
            return palette.color(QPalette::HighlightedText);
        case QIcon::Normal:
        case QIcon::Active:
            break;
        }
        return palette.color(QPalette::Normal, QPalette::WindowText);
    }

    QString m_path;
    QByteArray m_svg;
};

} // namespace

QString iconPath(Icon icon)
{
    const char* name = iconFileName(icon);
    if (name == nullptr) {
        return {};
    }
    return QStringLiteral(":/icons/%1.svg").arg(QLatin1StringView(name));
}

QIcon icon(Icon icon)
{
    // Delegates ask for icons on every paint; read each SVG file only once.
    static std::array<QIcon, static_cast<std::size_t>(Icon::Count)> cache;
    const auto index = static_cast<std::size_t>(icon);
    if (index >= cache.size()) {
        return {};
    }
    if (cache.at(index).isNull()) {
        cache.at(index) = QIcon(new ThemedSvgIconEngine(iconPath(icon)));
    }
    return cache.at(index);
}

} // namespace notes::ui
