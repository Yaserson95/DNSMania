#include "TreeIcons.h"

#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPixmap>
#include <QPolygon>

#include <functional>

namespace {

QIcon makeIcon(const std::function<void(QPainter&)>& draw)
{
    QPixmap pm(16, 16);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    draw(p);
    p.end();
    return QIcon(pm);
}

// Глобус заданного цвета + опциональная стрелка (для обратного просмотра и
// пересылки).
QIcon globe(const QColor& line, const QColor& fill, bool leftArrow, bool rightArrow)
{
    return makeIcon([&](QPainter& p) {
        p.setPen(QPen(line, 1.3));
        p.setBrush(fill);
        p.drawEllipse(QRectF(1.5, 1.5, 13, 13));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(QRectF(5.5, 1.5, 4, 13));   // меридиан
        p.drawLine(QPointF(1.5, 8), QPointF(14.5, 8)); // экватор
        if (leftArrow) {
            p.setPen(QPen(line, 1.7));
            p.drawPolyline(QPolygon() << QPoint(11, 5) << QPoint(5, 5));
            p.drawPolyline(QPolygon() << QPoint(7, 3) << QPoint(5, 5) << QPoint(7, 7));
        }
        if (rightArrow) {
            p.setPen(QPen(line, 1.7));
            p.drawPolyline(QPolygon() << QPoint(5, 10) << QPoint(11, 10));
            p.drawPolyline(QPolygon() << QPoint(9, 8) << QPoint(11, 10) << QPoint(9, 12));
        }
    });
}

} // namespace

namespace TreeIcons {

QIcon forwardZones()
{
    return globe(QColor(0x1f, 0x4e, 0x89), QColor(0xd6, 0xe6, 0xf7), false, false);
}

QIcon reverseZones()
{
    return globe(QColor(0x1d, 0x6f, 0x42), QColor(0xd8, 0xf0, 0xdf), true, false);
}

QIcon forwarderZones()
{
    return globe(QColor(0xa5, 0x5a, 0x0f), QColor(0xfb, 0xe7, 0xc8), false, true);
}

QIcon zone()
{
    return makeIcon([](QPainter& p) {
        const QColor line(0x4f, 0x81, 0xbd);
        QPainterPath path;
        path.moveTo(1.5, 3.75);
        path.arcTo(QRectF(1.5, 1.5, 13, 4.5), 180, -180);   // верхняя дуга
        path.lineTo(14.5, 10.75);
        path.arcTo(QRectF(1.5, 10.75, 13, 4.5), 0, -180);   // нижняя дуга
        path.closeSubpath();
        p.setPen(QPen(line, 1.3));
        p.setBrush(QColor(0xdd, 0xe8, 0xf6));
        p.drawPath(path);
        p.setPen(QPen(QColor(0x9c, 0xb6, 0xd9), 1));
        p.setBrush(Qt::NoBrush);
        p.drawArc(QRectF(1.5, 6.5, 13, 4.5), 0, 180);       // срединная линия
    });
}

QIcon scope()
{
    return makeIcon([](QPainter& p) {
        p.setPen(QPen(QColor(0x0f, 0x6b, 0x6b), 1.3));
        p.setBrush(QColor(0xbf, 0xe3, 0xe3));
        p.drawRoundedRect(QRectF(1.5, 7, 13, 7.5), 2, 2);
        p.setBrush(QColor(0xe6, 0xf7, 0xf7));
        p.drawRoundedRect(QRectF(3.5, 1.5, 9, 7.5), 2, 2);
    });
}

QIcon host()
{
    return makeIcon([](QPainter& p) {
        p.setPen(QPen(QColor(0x55, 0x55, 0x55), 1.3));
        p.setBrush(QColor(0xe8, 0xef, 0xf7));
        p.drawRoundedRect(QRectF(1.5, 2.5, 13, 9), 1.5, 1.5);
        p.setBrush(QColor(0x55, 0x55, 0x55));
        p.setPen(Qt::NoPen);
        p.drawRect(QRectF(6.5, 11.5, 3, 1.4));  // ножка
        p.drawRect(QRectF(4, 13, 8, 1.5));      // подставка
    });
}

QIcon policies()
{
    return makeIcon([](QPainter& p) {
        QPainterPath path;
        path.moveTo(2, 2.5);
        path.lineTo(14, 2.5);
        path.lineTo(9.3, 8.5);
        path.lineTo(9.3, 13);
        path.lineTo(6.7, 13);
        path.lineTo(6.7, 8.5);
        path.closeSubpath();
        p.setPen(QPen(QColor(0xa5, 0x5a, 0x0f), 1.3));
        p.setBrush(QColor(0xfb, 0xe3, 0xc0));
        p.drawPath(path);
    });
}

QIcon subnets()
{
    return makeIcon([](QPainter& p) {
        const QColor line(0x6a, 0x3f, 0x9e);
        p.setPen(QPen(line, 1.4));
        p.setBrush(QColor(0xe7, 0xdc, 0xf5));
        p.drawEllipse(QRectF(5.5, 1.5, 5, 5));
        p.drawEllipse(QRectF(1.5, 10, 4, 4));
        p.drawEllipse(QRectF(10.5, 10, 4, 4));
        p.drawLine(QPointF(7, 6.5), QPointF(3.5, 10));
        p.drawLine(QPointF(9, 6.5), QPointF(12.5, 10));
    });
}

} // namespace TreeIcons
