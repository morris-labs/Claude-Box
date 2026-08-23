#include "Icons.h"

#include "Theme.h"

#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QtMath>

#include <cmath>

#include <functional>

namespace {

using DrawFn = std::function<void(QPainter &, const QRectF &, qreal)>;

// Renders `draw` at each size an icon might realistically be asked for.
// `draw` receives the painter, the content rect (already inset by a
// margin), and the stroke width to use, so each icon only describes its
// shape and not its scaling.
QIcon render(const DrawFn &draw, const QColor &color)
{
    QIcon icon;
    for (const int size : {16, 20, 24, 32, 48}) {
        QPixmap pm(size, size);
        pm.fill(Qt::transparent);

        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing, true);

        const qreal stroke = qMax(1.25, size / 11.0);
        QPen pen(color);
        pen.setWidthF(stroke);
        pen.setCapStyle(Qt::RoundCap);
        pen.setJoinStyle(Qt::RoundJoin);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);

        const qreal margin = size * 0.21;
        draw(p, QRectF(margin, margin, size - 2 * margin, size - 2 * margin), stroke);
        p.end();

        icon.addPixmap(pm);
    }
    return icon;
}

QColor iconColor() { return Theme::text(); }

} // namespace

QIcon Icons::newBox()
{
    return render([](QPainter &p, const QRectF &r, qreal) {
        p.drawLine(QPointF(r.center().x(), r.top()), QPointF(r.center().x(), r.bottom()));
        p.drawLine(QPointF(r.left(), r.center().y()), QPointF(r.right(), r.center().y()));
    }, iconColor());
}

QIcon Icons::open()
{
    // Play triangle: "start this box up and show it to me".
    return render([](QPainter &p, const QRectF &r, qreal) {
        QPainterPath path;
        path.moveTo(r.left() + r.width() * 0.12, r.top());
        path.lineTo(r.right(), r.center().y());
        path.lineTo(r.left() + r.width() * 0.12, r.bottom());
        path.closeSubpath();
        p.fillPath(path, p.pen().color());
    }, iconColor());
}

QIcon Icons::closeBox()
{
    // Stop square, matching the play triangle above.
    return render([](QPainter &p, const QRectF &r, qreal) {
        const qreal inset = r.width() * 0.08;
        QPainterPath path;
        path.addRoundedRect(r.adjusted(inset, inset, -inset, -inset), r.width() * 0.12, r.width() * 0.12);
        p.fillPath(path, p.pen().color());
    }, iconColor());
}

QIcon Icons::removeBox()
{
    // Minus in a circle: removes the stopped container, leaves the record.
    return render([](QPainter &p, const QRectF &r, qreal) {
        p.drawEllipse(r);
        const qreal inset = r.width() * 0.26;
        p.drawLine(QPointF(r.left() + inset, r.center().y()),
                   QPointF(r.right() - inset, r.center().y()));
    }, iconColor());
}

QIcon Icons::purge()
{
    // Trash can: the genuinely destructive one (drops conversation history
    // and every record for the directory), so it gets the heavier symbol.
    return render([](QPainter &p, const QRectF &r, qreal stroke) {
        const qreal lidY = r.top() + r.height() * 0.18;
        p.drawLine(QPointF(r.left(), lidY), QPointF(r.right(), lidY));

        // Handle.
        const qreal hw = r.width() * 0.22;
        p.drawLine(QPointF(r.center().x() - hw, lidY),
                   QPointF(r.center().x() - hw, r.top()));
        p.drawLine(QPointF(r.center().x() + hw, lidY),
                   QPointF(r.center().x() + hw, r.top()));
        p.drawLine(QPointF(r.center().x() - hw, r.top()),
                   QPointF(r.center().x() + hw, r.top()));

        // Tapered body.
        const qreal bodyInset = r.width() * 0.13;
        QPainterPath body;
        body.moveTo(r.left() + bodyInset, lidY + stroke * 0.5);
        body.lineTo(r.left() + bodyInset * 2.0, r.bottom());
        body.lineTo(r.right() - bodyInset * 2.0, r.bottom());
        body.lineTo(r.right() - bodyInset, lidY + stroke * 0.5);
        p.drawPath(body);
    }, iconColor());
}

QIcon Icons::refresh()
{
    return render([](QPainter &p, const QRectF &r, qreal stroke) {
        // Arc with a gap, plus an arrowhead at the open end.
        const int startAngle = 55 * 16;
        const int spanAngle = -290 * 16;
        p.drawArc(r, startAngle, spanAngle);

        const qreal rad = r.width() / 2.0;
        const qreal a = qDegreesToRadians(55.0);
        const QPointF tip(r.center().x() + rad * std::cos(a),
                          r.center().y() - rad * std::sin(a));
        const qreal h = stroke * 1.9;
        QPainterPath head;
        head.moveTo(tip.x() - h, tip.y() - h * 0.35);
        head.lineTo(tip.x() + h * 0.55, tip.y() - h * 0.9);
        head.lineTo(tip.x() + h * 0.15, tip.y() + h * 0.85);
        head.closeSubpath();
        p.fillPath(head, p.pen().color());
    }, iconColor());
}

QIcon Icons::terminal()
{
    // ">_" -- used on terminal tabs.
    return render([](QPainter &p, const QRectF &r, qreal) {
        p.drawLine(QPointF(r.left(), r.top() + r.height() * 0.18),
                   QPointF(r.left() + r.width() * 0.42, r.center().y()));
        p.drawLine(QPointF(r.left() + r.width() * 0.42, r.center().y()),
                   QPointF(r.left(), r.bottom() - r.height() * 0.18));
        p.drawLine(QPointF(r.left() + r.width() * 0.58, r.bottom()),
                   QPointF(r.right(), r.bottom()));
    }, iconColor());
}

QIcon Icons::statusDot(const QColor &color)
{
    QIcon icon;
    for (const int size : {12, 16, 20, 24}) {
        QPixmap pm(size, size);
        pm.fill(Qt::transparent);

        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(Qt::NoPen);
        p.setBrush(color);

        const qreal d = size * 0.46;
        p.drawEllipse(QRectF((size - d) / 2.0, (size - d) / 2.0, d, d));
        p.end();

        icon.addPixmap(pm);
    }
    return icon;
}
