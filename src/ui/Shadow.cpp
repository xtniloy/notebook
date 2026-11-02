#include "ui/Shadow.h"

#include <QColor>
#include <QPainter>

namespace notes::ui {

void paintSoftShadow(QPainter& painter, const QRectF& rect, qreal radius, int size, qreal alpha)
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    const auto falloff = [size](int distance) {
        const qreal left = 1.0 - (static_cast<qreal>(distance) / size);
        return left * left;
    };
    for (int layer = size; layer >= 1; --layer) {
        const qreal layerAlpha = alpha * (falloff(layer - 1) - falloff(layer));
        painter.setBrush(QColor::fromRgbF(0, 0, 0, static_cast<float>(layerAlpha)));
        const qreal grow = layer;
        painter.drawRoundedRect(rect.adjusted(-grow, -grow, grow, grow), radius + grow,
                                radius + grow);
    }
    painter.restore();
}

} // namespace notes::ui
