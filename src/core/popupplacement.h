#pragma once

#include <QPoint>
#include <QRect>
#include <QSize>

// Placement of the Ctrl+Q popup next to the cursor (see doc/architecture.md,
// §4.13). Pure geometry in Qt logical pixels; the caller supplies the screen's
// availableGeometry(), so per-monitor scaling is already taken into account.
namespace PopupPlacement {

// Prefers the bottom-right corner of the anchor; flips to the left/up when the
// popup would leave `available`, then clamps it to the screen. An anchor
// outside `available` is handled as well.
QPoint positionNear(const QPoint &anchor, const QSize &size, const QRect &available,
                    int offset);

} // namespace PopupPlacement
