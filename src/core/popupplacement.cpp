#include "core/popupplacement.h"

#include <algorithm>

namespace PopupPlacement {

QPoint positionNear(const QPoint &anchor, const QSize &size, const QRect &available, int offset)
{
    QPoint position(anchor.x() + offset, anchor.y() + offset);

    if (position.x() + size.width() > available.right() + 1)
        position.setX(anchor.x() - size.width() - offset);
    if (position.y() + size.height() > available.bottom() + 1)
        position.setY(anchor.y() - size.height() - offset);

    // Keep the window inside the screen. If it is larger than the screen, the
    // top/left edge wins and the rest is cut by the window manager.
    const int maxX = available.right() + 1 - size.width();
    const int maxY = available.bottom() + 1 - size.height();
    position.setX(std::max(available.left(), std::min(position.x(), maxX)));
    position.setY(std::max(available.top(), std::min(position.y(), maxY)));
    return position;
}

} // namespace PopupPlacement
