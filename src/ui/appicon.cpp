#include "ui/appicon.h"

#include <QFont>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

namespace {

QPixmap renderIcon(int size)
{
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);

    // Rounded square background.
    const QRectF rect(0, 0, size, size);
    const qreal radius = size * 0.22;
    QLinearGradient gradient(rect.topLeft(), rect.bottomRight());
    gradient.setColorAt(0.0, QColor(0x3D, 0x8B, 0xFD));
    gradient.setColorAt(1.0, QColor(0x1F, 0x5F, 0xD6));
    QPainterPath background;
    background.addRoundedRect(rect, radius, radius);
    painter.fillPath(background, gradient);

    // Letter "Q".
    QFont font(QStringLiteral("Segoe UI"));
    font.setBold(true);
    font.setPixelSize(qRound(size * 0.78));
    painter.setFont(font);
    painter.setPen(Qt::white);
    painter.drawText(rect.adjusted(0, -size * 0.04, 0, 0), Qt::AlignCenter, QStringLiteral("Q"));

    return pixmap;
}

} // namespace

namespace AppIcon {

QIcon create()
{
    QIcon icon;
    for (const int size : {16, 20, 24, 32, 40, 48, 64, 128, 256})
        icon.addPixmap(renderIcon(size));
    return icon;
}

} // namespace AppIcon
