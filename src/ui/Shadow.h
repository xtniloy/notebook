#pragma once

#include <QRectF>

class QPainter;

namespace notes::ui {

// A soft shadow around the rounded rectangle `rect`, fading out over `size`
// pixels from `alpha` (black) at its edge: nested rounded rectangles of tiny
// opacity whose sum fades quadratically. Each layer is one continuous shape, so
// there are no seams at fractional display scales (125%), unlike shadows pieced
// together from corner and side gradients. Used by the window and by menus.
void paintSoftShadow(QPainter& painter, const QRectF& rect, qreal radius, int size, qreal alpha);

} // namespace notes::ui
