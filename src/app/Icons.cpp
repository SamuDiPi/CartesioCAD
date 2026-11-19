#include "app/Icons.h"

#include "app/Theme.h"
#include "core/Vec2.h"

#include <QHash>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPolygonF>

#include <cmath>

namespace cartesio {

namespace {

using namespace theme;

// Tutte le icone sono disegnate su una tela di 32x32 unità.
QPen stroke(const QColor& c, double w = 2.0)
{
    QPen p(c, w);
    p.setCapStyle(Qt::RoundCap);
    p.setJoinStyle(Qt::RoundJoin);
    return p;
}

void grip(QPainter& p, QPointF c, const QColor& color = kIconYellow, double s = 2.5)
{
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawRect(QRectF(c.x() - s, c.y() - s, 2 * s, 2 * s));
    p.setBrush(Qt::NoBrush);
}

void arrowHead(QPainter& p, QPointF tip, double angleDeg, const QColor& c, double size = 5.0)
{
    const double a = angleDeg * kPi / 180.0;
    const QPointF left(tip.x() - size * std::cos(a - 0.5), tip.y() + size * std::sin(a - 0.5));
    const QPointF right(tip.x() - size * std::cos(a + 0.5), tip.y() + size * std::sin(a + 0.5));
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    const QPointF tri[] = {tip, left, right};
    p.drawPolygon(tri, 3);
    p.setBrush(Qt::NoBrush);
}

void layers(QPainter& p, double dy = 0.0)
{
    // Tre fogli sovrapposti in prospettiva.
    const QColor fills[] = {QColor(70, 130, 200), QColor(90, 170, 110), QColor(220, 180, 70)};
    for (int i = 2; i >= 0; --i) {
        const double y = 8 + i * 6 + dy;
        const QPointF poly[] = {{4, y + 4}, {16, y - 2}, {28, y + 4}, {16, y + 10}};
        p.setPen(stroke(kIconStroke, 1.2));
        p.setBrush(fills[i]);
        p.drawPolygon(poly, 4);
    }
    p.setBrush(Qt::NoBrush);
}

void magnifier(QPainter& p)
{
    p.setPen(stroke(kIconStroke, 2.2));
    p.drawEllipse(QPointF(13, 13), 8, 8);
    p.setPen(stroke(kIconStroke, 3.5));
    p.drawLine(QPointF(19, 19), QPointF(27, 27));
}

void bulb(QPainter& p, bool on)
{
    const QColor glass = on ? kIconYellow : QColor(120, 128, 140);
    p.setPen(stroke(on ? QColor(255, 230, 140) : QColor(150, 158, 170), 1.5));
    p.setBrush(glass);
    p.drawEllipse(QPointF(16, 12), 8, 8);
    p.setBrush(QColor(170, 176, 186));
    p.setPen(Qt::NoPen);
    p.drawRect(QRectF(12, 19, 8, 7));
    p.setPen(stroke(QColor(90, 96, 106), 1.0));
    p.drawLine(QPointF(12, 22), QPointF(20, 22));
    p.drawLine(QPointF(12, 24.5), QPointF(20, 24.5));
    p.setBrush(Qt::NoBrush);
}

void lock(QPainter& p, bool closed)
{
    p.setPen(stroke(kIconStroke, 2.2));
    QPainterPath shackle;
    if (closed) {
        shackle.moveTo(10, 15);
        shackle.lineTo(10, 10);
        shackle.arcTo(QRectF(10, 4, 12, 12), 180, -180);
        shackle.lineTo(22, 15);
    } else {
        shackle.moveTo(10, 15);
        shackle.lineTo(10, 7);
        shackle.arcTo(QRectF(10, 1, 12, 12), 180, -180);
    }
    p.drawPath(shackle);
    p.setPen(stroke(closed ? kIconYellow : QColor(170, 176, 186), 1.2));
    p.setBrush(closed ? kIconYellow : QColor(170, 176, 186));
    p.drawRoundedRect(QRectF(7, 15, 18, 13), 2, 2);
    p.setBrush(Qt::NoBrush);
}

void draw(QPainter& p, Icon id)
{
    switch (id) {
    case Icon::AppLogo: {
        p.setPen(stroke(Qt::white, 3.2));
        p.drawArc(QRectF(7, 7, 18, 18), 45 * 16, 270 * 16);
        p.setPen(stroke(Qt::white, 1.4));
        p.drawLine(QPointF(16, 3), QPointF(16, 29));
        p.drawLine(QPointF(3, 16), QPointF(29, 16));
        break;
    }
    case Icon::New: {
        QPainterPath page;
        page.moveTo(8, 3); page.lineTo(20, 3); page.lineTo(26, 9); page.lineTo(26, 29); page.lineTo(8, 29);
        page.closeSubpath();
        p.setPen(stroke(kIconStroke, 1.6));
        p.setBrush(QColor(70, 80, 96));
        p.drawPath(page);
        p.drawLine(QPointF(20, 3), QPointF(20, 9));
        p.drawLine(QPointF(20, 9), QPointF(26, 9));
        p.setPen(stroke(kIconYellow, 2.2));
        p.drawLine(QPointF(4, 24), QPointF(12, 24));
        p.drawLine(QPointF(8, 20), QPointF(8, 28));
        break;
    }
    case Icon::Open: {
        p.setPen(stroke(QColor(240, 200, 100), 1.6));
        p.setBrush(QColor(200, 150, 50));
        QPainterPath back;
        back.moveTo(3, 8); back.lineTo(12, 8); back.lineTo(14, 11); back.lineTo(27, 11); back.lineTo(27, 26);
        back.lineTo(3, 26); back.closeSubpath();
        p.drawPath(back);
        p.setBrush(QColor(240, 195, 90));
        const QPointF front[] = {{3, 26}, {7, 15}, {30, 15}, {27, 26}};
        p.drawPolygon(front, 4);
        break;
    }
    case Icon::Save:
    case Icon::SaveAs: {
        p.setPen(stroke(kIconStroke, 1.6));
        p.setBrush(QColor(60, 110, 180));
        QPainterPath disk;
        disk.moveTo(4, 4); disk.lineTo(24, 4); disk.lineTo(28, 8); disk.lineTo(28, 28); disk.lineTo(4, 28);
        disk.closeSubpath();
        p.drawPath(disk);
        p.setBrush(QColor(220, 225, 232));
        p.drawRect(QRectF(9, 4, 13, 8));
        p.drawRect(QRectF(8, 17, 16, 11));
        if (id == Icon::SaveAs) {
            p.setPen(stroke(kIconYellow, 3.0));
            p.drawLine(QPointF(18, 28), QPointF(29, 17));
        }
        break;
    }
    case Icon::Undo:
    case Icon::Redo: {
        if (id == Icon::Redo) {
            p.translate(32, 0);
            p.scale(-1, 1);
        }
        p.setPen(stroke(kIconStroke, 2.6));
        QPainterPath curve;
        curve.moveTo(9, 12);
        curve.lineTo(19, 12);
        curve.cubicTo(30, 12, 30, 27, 19, 27);
        curve.lineTo(12, 27);
        p.drawPath(curve);
        arrowHead(p, QPointF(4, 12), 180, kIconStroke, 7);
        break;
    }
    case Icon::Close:
        p.setPen(stroke(kIconStroke, 2.4));
        p.drawLine(QPointF(9, 9), QPointF(23, 23));
        p.drawLine(QPointF(23, 9), QPointF(9, 23));
        break;

    case Icon::Line:
        p.setPen(stroke(kIconStroke, 2.2));
        p.drawLine(QPointF(6, 26), QPointF(26, 6));
        grip(p, {6, 26});
        grip(p, {26, 6});
        break;
    case Icon::Circle:
        p.setPen(stroke(kIconStroke, 2.2));
        p.drawEllipse(QPointF(16, 16), 11, 11);
        p.setPen(stroke(kIconBlue, 1.6));
        p.drawLine(QPointF(16, 16), QPointF(24, 8));
        grip(p, {16, 16});
        break;
    case Icon::Arc:
        p.setPen(stroke(kIconStroke, 2.2));
        p.drawArc(QRectF(5, 7, 22, 22), 0, 180 * 16);
        grip(p, {5, 18});
        grip(p, {16, 7});
        grip(p, {27, 18});
        break;

    case Icon::Polyline: {
        p.setPen(stroke(kIconStroke, 2.2));
        QPainterPath path;
        path.moveTo(4, 26);
        path.lineTo(10, 10);
        path.lineTo(18, 20);
        path.quadTo(28, 4, 28, 12);
        p.drawPath(path);
        grip(p, {4, 26});
        grip(p, {10, 10});
        grip(p, {18, 20});
        break;
    }
    case Icon::Rectangle:
        p.setPen(stroke(kIconStroke, 2.2));
        p.drawRect(QRectF(5, 8, 22, 16));
        grip(p, {5, 24});
        grip(p, {27, 8});
        break;
    case Icon::Ellipse:
        p.setPen(stroke(kIconStroke, 2.2));
        p.drawEllipse(QPointF(16, 16), 12, 7);
        p.setPen(stroke(kIconBlue, 1.4));
        p.drawLine(QPointF(4, 16), QPointF(28, 16));
        grip(p, {16, 16});
        break;
    case Icon::PointIcon:
        p.setPen(stroke(kIconStroke, 1.4));
        p.drawLine(QPointF(9, 16), QPointF(23, 16));
        p.drawLine(QPointF(16, 9), QPointF(16, 23));
        p.setPen(Qt::NoPen);
        p.setBrush(kIconYellow);
        p.drawEllipse(QPointF(16, 16), 3.5, 3.5);
        p.setBrush(Qt::NoBrush);
        break;
    case Icon::Hatch: {
        p.setPen(stroke(kIconStroke, 2.0));
        p.drawRect(QRectF(5, 5, 22, 22));
        p.save();
        p.setClipRect(QRectF(6, 6, 20, 20));
        p.setPen(stroke(kIconBlue, 1.4));
        for (int i = -24; i < 32; i += 5)
            p.drawLine(QPointF(i, 28), QPointF(i + 24, 4));
        p.restore();
        break;
    }
    case Icon::TextSingle:
    case Icon::TextMulti: {
        QFont f(QStringLiteral("Times New Roman"));
        f.setPixelSize(id == Icon::TextMulti ? 20 : 24);
        f.setBold(true);
        p.setFont(f);
        p.setPen(kIconStroke);
        p.drawText(QRectF(1, id == Icon::TextMulti ? 0 : 2, 22, 26), Qt::AlignCenter, QStringLiteral("A"));
        if (id == Icon::TextMulti) {
            p.setPen(stroke(kIconYellow, 1.6));
            for (int i = 0; i < 3; ++i)
                p.drawLine(QPointF(19, 12 + i * 5), QPointF(29, 12 + i * 5));
        } else {
            p.setPen(stroke(kIconYellow, 1.6));
            p.drawLine(QPointF(4, 28), QPointF(28, 28));
        }
        break;
    }
    case Icon::DimLinear:
    case Icon::DimAligned: {
        if (id == Icon::DimAligned) {
            p.translate(16, 16);
            p.rotate(-30);
            p.translate(-16, -16);
        }
        p.setPen(stroke(kIconStroke, 1.4));
        p.drawLine(QPointF(5, 26), QPointF(5, 8));
        p.drawLine(QPointF(27, 26), QPointF(27, 8));
        p.setPen(stroke(kIconYellow, 1.8));
        p.drawLine(QPointF(7, 13), QPointF(25, 13));
        arrowHead(p, {6, 13}, 180, kIconYellow, 5);
        arrowHead(p, {26, 13}, 0, kIconYellow, 5);
        p.setPen(stroke(kIconStroke, 1.2));
        p.drawLine(QPointF(12, 6), QPointF(20, 6));
        break;
    }
    case Icon::DimRadius:
    case Icon::DimDiameter:
        p.setPen(stroke(kIconStroke, 1.8));
        p.drawEllipse(QPointF(16, 16), 11, 11);
        p.setPen(stroke(kIconYellow, 1.8));
        if (id == Icon::DimDiameter) {
            p.drawLine(QPointF(8.5, 23.5), QPointF(23.5, 8.5));
            arrowHead(p, {7.5, 24.5}, -135, kIconYellow, 5);
            arrowHead(p, {24.5, 7.5}, 45, kIconYellow, 5);
        } else {
            p.drawLine(QPointF(16, 16), QPointF(23.5, 8.5));
            arrowHead(p, {24.5, 7.5}, 45, kIconYellow, 5);
            grip(p, {16, 16}, kIconBlue, 1.8);
        }
        break;
    case Icon::BlockCreate:
    case Icon::BlockInsert:
        p.setPen(stroke(kIconStroke, 1.6));
        p.setBrush(QColor(60, 110, 180));
        p.drawRect(QRectF(5, 9, 15, 15));
        p.setBrush(Qt::NoBrush);
        p.drawLine(QPointF(5, 9), QPointF(11, 4));
        p.drawLine(QPointF(20, 9), QPointF(26, 4));
        p.drawLine(QPointF(11, 4), QPointF(26, 4));
        p.drawLine(QPointF(26, 4), QPointF(26, 19));
        p.drawLine(QPointF(20, 24), QPointF(26, 19));
        if (id == Icon::BlockCreate) {
            p.setPen(stroke(kIconYellow, 2.4));
            p.drawLine(QPointF(20, 27), QPointF(30, 27));
            p.drawLine(QPointF(25, 22), QPointF(25, 32));
        } else {
            grip(p, {5, 24}, kIconYellow, 3);
        }
        break;
    case Icon::Explode:
        p.setPen(stroke(kIconStroke, 1.6));
        p.drawRect(QRectF(11, 11, 10, 10));
        p.setPen(stroke(kIconYellow, 1.8));
        for (int i = 0; i < 4; ++i) {
            const double a = kPi / 4 + i * kPi / 2;
            p.drawLine(QPointF(16 + 9 * std::cos(a), 16 + 9 * std::sin(a)),
                       QPointF(16 + 14 * std::cos(a), 16 + 14 * std::sin(a)));
        }
        break;

    case Icon::AttributeDef: {
        p.setPen(stroke(kIconStroke, 1.6));
        p.setBrush(QColor(70, 80, 96));
        const QPointF tag[] = {{4, 10}, {22, 10}, {28, 16}, {22, 22}, {4, 22}};
        p.drawPolygon(tag, 5);
        p.setBrush(Qt::NoBrush);
        QFont f(QStringLiteral("Arial"));
        f.setPixelSize(11);
        f.setBold(true);
        p.setFont(f);
        p.setPen(kIconYellow);
        p.drawText(QRectF(4, 10, 18, 12), Qt::AlignCenter, QStringLiteral("ATT"));
        break;
    }
    case Icon::LayoutNew:
    case Icon::ViewportIcon:
    case Icon::TitleBlock:
    case Icon::PageSetup:
    case Icon::Pdf: {
        // Foglio bianco con il bordo.
        p.setPen(stroke(kIconStroke, 1.4));
        p.setBrush(QColor(235, 238, 242));
        p.drawRect(QRectF(3, 6, 26, 20));
        p.setBrush(Qt::NoBrush);
        if (id == Icon::LayoutNew) {
            p.setPen(stroke(QColor(90, 100, 115), 1.2));
            p.drawRect(QRectF(6, 9, 20, 14));
            p.setPen(stroke(kIconYellow, 2.6));
            p.drawLine(QPointF(19, 26), QPointF(29, 26));
            p.drawLine(QPointF(24, 21), QPointF(24, 31));
        } else if (id == Icon::TitleBlock) {
            // Cornice e cartiglio in basso a destra.
            p.setPen(stroke(QColor(90, 100, 115), 1.2));
            p.drawRect(QRectF(5.5, 8.5, 21, 15));
            p.setPen(stroke(kIconBlue, 1.4));
            p.setBrush(QColor(205, 222, 245));
            p.drawRect(QRectF(14.5, 17.5, 12, 6));
            p.setBrush(Qt::NoBrush);
            p.drawLine(QPointF(14.5, 20.5), QPointF(26.5, 20.5));
            p.drawLine(QPointF(20.5, 20.5), QPointF(20.5, 23.5));
        } else if (id == Icon::ViewportIcon) {
            p.setPen(stroke(kIconBlue, 1.8));
            p.drawRect(QRectF(7, 10, 18, 12));
            p.setPen(stroke(QColor(60, 70, 85), 1.4));
            p.drawEllipse(QPointF(16, 16), 4, 4);
        } else if (id == Icon::PageSetup) {
            p.setPen(stroke(QColor(90, 100, 115), 1.2));
            for (int i = 0; i < 3; ++i)
                p.drawLine(QPointF(7, 11 + i * 4), QPointF(20, 11 + i * 4));
            p.setPen(stroke(kIconYellow, 2.0));
            p.drawEllipse(QPointF(24, 22), 4.5, 4.5);
            p.drawLine(QPointF(24, 15.5), QPointF(24, 17.5));
            p.drawLine(QPointF(24, 26.5), QPointF(24, 28.5));
        } else {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(200, 40, 40));
            p.drawRect(QRectF(3, 15, 26, 11));
            p.setBrush(Qt::NoBrush);
            QFont f(QStringLiteral("Arial"));
            f.setPixelSize(10);
            f.setBold(true);
            p.setFont(f);
            p.setPen(Qt::white);
            p.drawText(QRectF(3, 15, 26, 11), Qt::AlignCenter, QStringLiteral("PDF"));
        }
        break;
    }
    case Icon::PlotIcon: {
        // Stampante con un foglio.
        p.setPen(stroke(kIconStroke, 1.4));
        p.setBrush(QColor(235, 238, 242));
        p.drawRect(QRectF(9, 4, 14, 10));
        p.setBrush(QColor(70, 80, 96));
        p.drawRoundedRect(QRectF(3, 12, 26, 11), 2, 2);
        p.setBrush(QColor(235, 238, 242));
        p.drawRect(QRectF(9, 19, 14, 9));
        p.setPen(stroke(QColor(90, 100, 115), 1.0));
        p.drawLine(QPointF(11, 22.5), QPointF(21, 22.5));
        p.drawLine(QPointF(11, 25.5), QPointF(18, 25.5));
        grip(p, QPointF(25, 15.5), kIconYellow, 1.2);
        break;
    }
    case Icon::PreviewIcon: {
        // Foglio con la lente.
        p.setPen(stroke(kIconStroke, 1.4));
        p.setBrush(QColor(235, 238, 242));
        p.drawRect(QRectF(4, 3, 18, 24));
        p.setPen(stroke(QColor(90, 100, 115), 1.0));
        for (int i = 0; i < 4; ++i)
            p.drawLine(QPointF(7, 8 + i * 4), QPointF(18, 8 + i * 4));
        p.setBrush(QColor(205, 222, 245, 200));
        p.setPen(stroke(kIconBlue, 2.0));
        p.drawEllipse(QPointF(20, 19), 5.5, 5.5);
        p.setBrush(Qt::NoBrush);
        p.setPen(stroke(kIconYellow, 3.0));
        p.drawLine(QPointF(24, 23), QPointF(29, 28));
        break;
    }
    case Icon::Script: {
        p.setPen(stroke(kIconStroke, 1.6));
        p.setBrush(QColor(70, 80, 96));
        p.drawRect(QRectF(6, 3, 20, 26));
        p.setBrush(Qt::NoBrush);
        QFont f(QStringLiteral("Consolas"));
        f.setPixelSize(8);
        p.setFont(f);
        p.setPen(kIconYellow);
        p.drawText(QPointF(8, 11), QStringLiteral("LINE"));
        p.setPen(kIconStroke);
        p.drawText(QPointF(8, 18), QStringLiteral("0,0"));
        p.drawText(QPointF(8, 25), QStringLiteral("10,0"));
        break;
    }

    case Icon::Rotate: {
        p.setPen(stroke(QColor(140, 150, 165), 1.6));
        p.drawRect(QRectF(6, 14, 12, 12));
        p.save();
        p.translate(18, 14);
        p.rotate(-30);
        p.setPen(stroke(kIconStroke, 1.8));
        p.drawRect(QRectF(-12, 0, 12, 12));
        p.restore();
        p.setPen(stroke(kIconYellow, 1.8));
        p.drawArc(QRectF(10, 3, 18, 18), 10 * 16, 110 * 16);
        arrowHead(p, {13, 6}, 160, kIconYellow, 4.5);
        grip(p, {18, 14}, kIconBlue, 2);
        break;
    }
    case Icon::ScaleIcon:
        p.setPen(stroke(QColor(140, 150, 165), 1.6));
        p.drawRect(QRectF(4, 16, 10, 10));
        p.setPen(stroke(kIconStroke, 1.8));
        p.drawRect(QRectF(4, 6, 20, 20));
        p.setPen(stroke(kIconYellow, 1.8));
        p.drawLine(QPointF(14, 16), QPointF(27, 3));
        arrowHead(p, {28, 2}, 45, kIconYellow, 4.5);
        grip(p, {4, 26}, kIconBlue, 2);
        break;
    case Icon::Mirror: {
        QPen dashed = stroke(kIconYellow, 1.6);
        dashed.setDashPattern({2, 2});
        p.setPen(dashed);
        p.drawLine(QPointF(16, 3), QPointF(16, 29));
        p.setPen(stroke(kIconStroke, 1.8));
        const QPointF left[] = {{4, 24}, {13, 24}, {13, 8}};
        p.drawPolygon(left, 3);
        p.setPen(stroke(QColor(140, 150, 165), 1.6));
        const QPointF right[] = {{28, 24}, {19, 24}, {19, 8}};
        p.drawPolygon(right, 3);
        break;
    }
    case Icon::ArrayIcon:
        p.setPen(stroke(kIconStroke, 1.4));
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j) {
                p.setBrush(i == 0 && j == 2 ? kIconBlue : QColor(70, 80, 96));
                p.drawRect(QRectF(4 + i * 9, 4 + j * 9, 6, 6));
            }
        p.setBrush(Qt::NoBrush);
        break;
    case Icon::Extend:
        p.setPen(stroke(kIconStroke, 2.2));
        p.drawLine(QPointF(26, 4), QPointF(26, 28));
        p.drawLine(QPointF(4, 16), QPointF(14, 16));
        p.setPen(stroke(kIconYellow, 1.8));
        p.drawLine(QPointF(14, 16), QPointF(22, 16));
        arrowHead(p, {25, 16}, 0, kIconYellow, 5);
        break;
    case Icon::Fillet:
        p.setPen(stroke(kIconStroke, 2.2));
        p.drawLine(QPointF(5, 27), QPointF(5, 15));
        p.drawLine(QPointF(17, 4), QPointF(28, 4));
        p.setPen(stroke(kIconYellow, 2.2));
        p.drawArc(QRectF(5, 4, 24, 22), 90 * 16, 90 * 16);
        break;
    case Icon::Chamfer:
        p.setPen(stroke(kIconStroke, 2.2));
        p.drawLine(QPointF(5, 28), QPointF(5, 14));
        p.drawLine(QPointF(15, 4), QPointF(28, 4));
        p.setPen(stroke(kIconYellow, 2.2));
        p.drawLine(QPointF(5, 14), QPointF(15, 4));
        break;
    case Icon::Stretch:
        // Rettangolo con il lato destro tirato verso destra.
        p.setPen(stroke(kIconStroke, 1.6));
        p.drawLine(QPointF(4, 9), QPointF(18, 9));
        p.drawLine(QPointF(4, 23), QPointF(18, 23));
        p.drawLine(QPointF(4, 9), QPointF(4, 23));
        p.setPen(stroke(kIconYellow, 2.0));
        p.drawLine(QPointF(18, 9), QPointF(27, 9));
        p.drawLine(QPointF(18, 23), QPointF(27, 23));
        p.drawLine(QPointF(27, 9), QPointF(27, 23));
        p.setPen(stroke(kIconBlue, 1.4));
        p.drawLine(QPointF(13, 16), QPointF(23, 16));
        p.drawLine(QPointF(20, 13), QPointF(23, 16));
        p.drawLine(QPointF(20, 19), QPointF(23, 16));
        break;
    case Icon::Break:
        p.setPen(stroke(kIconStroke, 2.2));
        p.drawLine(QPointF(3, 16), QPointF(12, 16));
        p.drawLine(QPointF(20, 16), QPointF(29, 16));
        p.setPen(stroke(kIconYellow, 1.6));
        p.drawLine(QPointF(12, 10), QPointF(12, 22));
        p.drawLine(QPointF(20, 10), QPointF(20, 22));
        break;
    case Icon::Join:
        p.setPen(stroke(kIconStroke, 2.2));
        p.drawLine(QPointF(3, 22), QPointF(14, 10));
        p.drawLine(QPointF(18, 10), QPointF(29, 22));
        p.setPen(stroke(kIconYellow, 2.2));
        p.drawLine(QPointF(14, 10), QPointF(18, 10));
        p.setBrush(kIconYellow);
        p.drawEllipse(QPointF(16, 10), 2.2, 2.2);
        p.setBrush(Qt::NoBrush);
        break;
    case Icon::Leader:
        // Freccia, linea inclinata, approdo e due righe di testo.
        p.setPen(stroke(kIconStroke, 1.8));
        p.drawLine(QPointF(5, 27), QPointF(15, 12));
        p.drawLine(QPointF(15, 12), QPointF(19, 12));
        p.setBrush(kIconStroke);
        p.drawPolygon(QPolygonF({QPointF(4, 29), QPointF(5, 22), QPointF(9.5, 25)}));
        p.setBrush(Qt::NoBrush);
        p.setPen(stroke(kIconYellow, 2.2));
        p.drawLine(QPointF(21, 9), QPointF(29, 9));
        p.drawLine(QPointF(21, 15), QPointF(27, 15));
        break;
    case Icon::Pan: {
        // Manina aperta.
        p.setPen(stroke(kIconStroke, 1.4));
        p.setBrush(QColor(225, 205, 170));
        QPainterPath hand;
        hand.moveTo(9, 29);
        hand.lineTo(6, 19);
        hand.cubicTo(5, 16, 8, 15, 9.5, 18);
        hand.lineTo(10, 8);
        hand.cubicTo(10, 6, 13, 6, 13, 8);
        hand.lineTo(13, 15);
        hand.lineTo(14, 5);
        hand.cubicTo(14, 3, 17, 3, 17, 5);
        hand.lineTo(17, 15);
        hand.lineTo(18.5, 6.5);
        hand.cubicTo(18.5, 4.5, 21.5, 4.5, 21.5, 6.5);
        hand.lineTo(21, 16);
        hand.lineTo(22.5, 10);
        hand.cubicTo(23, 8, 26, 8.5, 25.5, 10.5);
        hand.lineTo(24, 22);
        hand.cubicTo(23, 27, 21, 29, 19, 29);
        hand.closeSubpath();
        p.drawPath(hand);
        p.setBrush(Qt::NoBrush);
        break;
    }
    case Icon::Polygon: {
        p.setPen(stroke(kIconStroke, 1.8));
        QPolygonF hex;
        for (int i = 0; i < 6; ++i)
            hex << QPointF(16 + 12 * std::cos(i * kPi / 3.0), 16 + 12 * std::sin(i * kPi / 3.0));
        p.drawPolygon(hex);
        p.setPen(stroke(kIconYellow, 2.0));
        p.drawPoint(QPointF(16, 16));
        break;
    }
    case Icon::XLineIcon:
        p.setPen(stroke(kIconStroke, 1.6));
        p.drawLine(QPointF(2, 26), QPointF(30, 8));
        p.setPen(stroke(kIconYellow, 1.6));
        p.drawLine(QPointF(4, 6), QPointF(28, 28));
        p.setBrush(kIconYellow);
        p.drawEllipse(QPointF(16, 17), 2.2, 2.2);
        p.setBrush(Qt::NoBrush);
        break;
    case Icon::Revcloud: {
        p.setPen(stroke(kIconStroke, 1.6));
        for (int i = 0; i < 4; ++i) {
            p.drawArc(QRectF(4 + i * 6, 5, 6, 6), 0, 180 * 16);
            p.drawArc(QRectF(4 + i * 6, 21, 6, 6), 180 * 16, 180 * 16);
        }
        for (int i = 0; i < 2; ++i) {
            p.drawArc(QRectF(1, 8 + i * 8, 6, 8), 90 * 16, 180 * 16);
            p.drawArc(QRectF(25, 8 + i * 8, 6, 8), -90 * 16, 180 * 16);
        }
        break;
    }
    case Icon::TableIcon:
        p.setPen(stroke(kIconStroke, 1.4));
        p.setBrush(QColor(60, 70, 85));
        p.drawRect(QRectF(4, 6, 24, 5));
        p.setBrush(Qt::NoBrush);
        p.drawRect(QRectF(4, 6, 24, 20));
        p.drawLine(QPointF(4, 16), QPointF(28, 16));
        p.drawLine(QPointF(4, 21), QPointF(28, 21));
        p.drawLine(QPointF(12, 11), QPointF(12, 26));
        p.drawLine(QPointF(20, 11), QPointF(20, 26));
        break;
    case Icon::GroupIcon:
    case Icon::UngroupIcon:
        p.setPen(stroke(kIconStroke, 1.6));
        p.drawRect(QRectF(6, 9, 10, 10));
        p.drawEllipse(QPointF(21, 17), 6, 6);
        {
            QPen dash = stroke(id == Icon::GroupIcon ? kIconBlue : kIconYellow, 1.4);
            dash.setStyle(Qt::DashLine);
            p.setPen(dash);
        }
        p.drawRect(QRectF(3, 5, 26, 22));
        if (id == Icon::UngroupIcon) {
            p.setPen(stroke(QColor(220, 80, 70), 2.0));
            p.drawLine(QPointF(4, 28), QPointF(28, 4));
        }
        break;
    case Icon::WipeoutIcon:
        p.setPen(stroke(kIconStroke, 1.4));
        for (int i = 0; i < 5; ++i)
            p.drawLine(QPointF(3, 6 + i * 5), QPointF(29, 6 + i * 5));
        p.setPen(stroke(kIconYellow, 1.6));
        p.setBrush(QColor(33, 40, 48));
        p.drawPolygon(QPolygonF({QPointF(8, 9), QPointF(25, 7), QPointF(23, 24), QPointF(10, 22)}));
        p.setBrush(Qt::NoBrush);
        break;
    case Icon::MlineIcon:
        p.setPen(stroke(kIconStroke, 1.6));
        p.drawPolyline(QPolygonF({QPointF(3, 24), QPointF(14, 9), QPointF(29, 9)}));
        p.setPen(stroke(kIconYellow, 1.6));
        p.drawPolyline(QPolygonF({QPointF(7, 27), QPointF(16, 15), QPointF(29, 15)}));
        break;
    case Icon::Measure:
        p.setPen(stroke(kIconStroke, 1.6));
        p.setBrush(QColor(200, 170, 80));
        p.drawRect(QRectF(3, 12, 26, 9));
        p.setBrush(Qt::NoBrush);
        p.setPen(stroke(QColor(60, 50, 30), 1.2));
        for (int i = 0; i < 6; ++i)
            p.drawLine(QPointF(6 + i * 4, 12), QPointF(6 + i * 4, i % 2 ? 15 : 17));
        break;
    case Icon::AreaIcon: {
        p.setPen(stroke(kIconStroke, 1.8));
        p.setBrush(QColor(88, 166, 255, 90));
        const QPointF poly[] = {{4, 26}, {8, 7}, {22, 4}, {28, 18}, {18, 28}};
        p.drawPolygon(poly, 5);
        p.setBrush(Qt::NoBrush);
        break;
    }
    case Icon::ListIcon:
        p.setPen(stroke(kIconStroke, 1.6));
        p.setBrush(QColor(70, 80, 96));
        p.drawRect(QRectF(6, 3, 20, 26));
        p.setBrush(Qt::NoBrush);
        p.setPen(stroke(kIconYellow, 1.6));
        p.drawLine(QPointF(10, 9), QPointF(22, 9));
        p.setPen(stroke(kIconStroke, 1.4));
        for (int i = 0; i < 3; ++i)
            p.drawLine(QPointF(10, 14 + i * 4.5), QPointF(22 - i * 3, 14 + i * 4.5));
        break;
    case Icon::MatchProp:
        p.setPen(stroke(kIconStroke, 1.6));
        p.setBrush(QColor(200, 200, 210));
        p.drawRect(QRectF(5, 4, 18, 9));
        p.drawRect(QRectF(12, 13, 4, 6));
        p.setBrush(kIconYellow);
        p.drawRect(QRectF(11, 19, 6, 10));
        p.setBrush(Qt::NoBrush);
        break;
    case Icon::Paste:
        p.setPen(stroke(kIconStroke, 1.6));
        p.setBrush(QColor(170, 120, 60));
        p.drawRoundedRect(QRectF(5, 5, 18, 23), 2, 2);
        p.setBrush(QColor(200, 205, 215));
        p.drawRect(QRectF(10, 3, 8, 5));
        p.setBrush(QColor(235, 238, 242));
        p.drawRect(QRectF(14, 13, 14, 16));
        p.setBrush(Qt::NoBrush);
        break;

    case Icon::Move: {
        p.setPen(stroke(kIconStroke, 2.2));
        p.drawLine(QPointF(16, 5), QPointF(16, 27));
        p.drawLine(QPointF(5, 16), QPointF(27, 16));
        arrowHead(p, {16, 3}, 90, kIconStroke);
        arrowHead(p, {16, 29}, -90, kIconStroke);
        arrowHead(p, {3, 16}, 180, kIconStroke);
        arrowHead(p, {29, 16}, 0, kIconStroke);
        grip(p, {16, 16}, kIconYellow, 2.5);
        break;
    }
    case Icon::Copy: {
        p.setPen(stroke(QColor(140, 150, 165), 1.8));
        p.drawRect(QRectF(4, 4, 14, 14));
        p.setPen(stroke(kIconStroke, 2.0));
        p.setBrush(QColor(60, 110, 180));
        p.drawRect(QRectF(14, 14, 14, 14));
        p.setBrush(Qt::NoBrush);
        p.setPen(stroke(kIconYellow, 1.8));
        p.drawLine(QPointF(9, 21), QPointF(9, 27));
        p.drawLine(QPointF(6, 24), QPointF(12, 24));
        break;
    }
    case Icon::Offset:
        p.setPen(stroke(QColor(140, 150, 165), 2.0));
        p.drawArc(QRectF(2, 8, 28, 28), 20 * 16, 140 * 16);
        p.setPen(stroke(kIconStroke, 2.2));
        p.drawArc(QRectF(8, 15, 16, 16), 20 * 16, 140 * 16);
        p.setPen(stroke(kIconYellow, 1.6));
        p.drawLine(QPointF(16, 15), QPointF(16, 9));
        arrowHead(p, {16, 8}, 90, kIconYellow, 4);
        break;
    case Icon::Trim: {
        p.setPen(stroke(kIconStroke, 2.2));
        p.drawLine(QPointF(4, 18), QPointF(16, 18));
        p.drawLine(QPointF(16, 4), QPointF(16, 28));
        QPen dashed = stroke(kIconRed, 2.2);
        dashed.setDashPattern({1.5, 1.5});
        p.setPen(dashed);
        p.drawLine(QPointF(16, 18), QPointF(28, 18));
        p.setPen(stroke(kIconYellow, 1.6));
        p.drawEllipse(QPointF(24, 24), 3, 3);
        p.drawEllipse(QPointF(24, 12), 3, 3);
        break;
    }
    case Icon::Erase: {
        p.setPen(stroke(kIconStroke, 1.6));
        p.setBrush(QColor(230, 120, 150));
        const QPointF body[] = {{6, 20}, {18, 6}, {28, 14}, {16, 28}};
        p.drawPolygon(body, 4);
        p.setBrush(QColor(220, 225, 232));
        const QPointF tip[] = {{6, 20}, {11, 14}, {21, 22}, {16, 28}};
        p.drawPolygon(tip, 4);
        p.setBrush(Qt::NoBrush);
        break;
    }

    case Icon::LayerProperties:
        layers(p, -2);
        p.setPen(stroke(kIconStroke, 1.4));
        p.setBrush(QColor(70, 80, 96));
        p.drawRect(QRectF(19, 18, 11, 12));
        p.drawLine(QPointF(21, 22), QPointF(28, 22));
        p.drawLine(QPointF(21, 26), QPointF(28, 26));
        p.setBrush(Qt::NoBrush);
        break;
    case Icon::LayerMakeCurrent:
        layers(p, -2);
        p.setPen(stroke(kIconGreen, 3.0));
        p.drawLine(QPointF(17, 24), QPointF(21, 28));
        p.drawLine(QPointF(21, 28), QPointF(30, 17));
        break;
    case Icon::LayerAllOn:
        layers(p, -2);
        p.save();
        p.translate(14, 12);
        p.scale(0.55, 0.55);
        bulb(p, true);
        p.restore();
        break;
    case Icon::LayerNew:
        layers(p, -2);
        p.setPen(stroke(kIconYellow, 2.6));
        p.drawLine(QPointF(19, 25), QPointF(29, 25));
        p.drawLine(QPointF(24, 20), QPointF(24, 30));
        break;
    case Icon::LayerDelete:
        layers(p, -2);
        p.setPen(stroke(kIconRed, 2.8));
        p.drawLine(QPointF(19, 20), QPointF(29, 30));
        p.drawLine(QPointF(29, 20), QPointF(19, 30));
        break;
    case Icon::LayerCurrentMark:
        p.setPen(stroke(kIconGreen, 3.6));
        p.drawLine(QPointF(7, 17), QPointF(13, 24));
        p.drawLine(QPointF(13, 24), QPointF(26, 8));
        break;

    case Icon::BulbOn: bulb(p, true); break;
    case Icon::BulbOff: bulb(p, false); break;
    case Icon::Sun:
        p.setPen(stroke(kIconYellow, 2.0));
        p.setBrush(kIconYellow);
        p.drawEllipse(QPointF(16, 16), 6, 6);
        p.setBrush(Qt::NoBrush);
        for (int i = 0; i < 8; ++i) {
            const double a = i * kPi / 4;
            p.drawLine(QPointF(16 + 9 * std::cos(a), 16 + 9 * std::sin(a)),
                       QPointF(16 + 13 * std::cos(a), 16 + 13 * std::sin(a)));
        }
        break;
    case Icon::Snowflake:
        p.setPen(stroke(QColor(140, 200, 255), 2.0));
        for (int i = 0; i < 3; ++i) {
            const double a = i * kPi / 3;
            const QPointF d(12 * std::cos(a), 12 * std::sin(a));
            p.drawLine(QPointF(16, 16) - d, QPointF(16, 16) + d);
        }
        break;
    case Icon::LockOpen: lock(p, false); break;
    case Icon::LockClosed: lock(p, true); break;

    case Icon::ZoomExtents:
        magnifier(p);
        p.setPen(stroke(kIconYellow, 1.6));
        p.drawRect(QRectF(9, 9, 8, 8));
        break;
    case Icon::ZoomIn:
        magnifier(p);
        p.setPen(stroke(kIconYellow, 2.0));
        p.drawLine(QPointF(9, 13), QPointF(17, 13));
        p.drawLine(QPointF(13, 9), QPointF(13, 17));
        break;
    case Icon::ZoomOut:
        magnifier(p);
        p.setPen(stroke(kIconYellow, 2.0));
        p.drawLine(QPointF(9, 13), QPointF(17, 13));
        break;

    case Icon::Properties:
        p.setPen(stroke(kIconStroke, 1.6));
        p.setBrush(QColor(70, 80, 96));
        p.drawRect(QRectF(5, 4, 22, 24));
        p.setBrush(Qt::NoBrush);
        for (int i = 0; i < 4; ++i) {
            const double y = 9 + i * 5;
            grip(p, {10, y}, i == 0 ? kIconYellow : kIconBlue, 1.6);
            p.setPen(stroke(kIconStroke, 1.4));
            p.drawLine(QPointF(14, y), QPointF(24, y));
        }
        break;
    case Icon::CommandLine:
    case Icon::History:
        p.setPen(stroke(kIconStroke, 1.6));
        p.setBrush(QColor(30, 34, 42));
        p.drawRoundedRect(QRectF(3, 6, 26, 20), 2, 2);
        p.setBrush(Qt::NoBrush);
        p.setPen(stroke(kIconYellow, 2.0));
        p.drawLine(QPointF(8, 12), QPointF(12, 16));
        p.drawLine(QPointF(12, 16), QPointF(8, 20));
        p.setPen(stroke(kIconStroke, 2.0));
        p.drawLine(QPointF(15, 20), QPointF(23, 20));
        break;

    case Icon::Grid:
        p.setPen(stroke(kIconStroke, 1.4));
        for (int i = 0; i < 4; ++i) {
            const double v = 6 + i * 6.7;
            p.drawLine(QPointF(v, 5), QPointF(v, 27));
            p.drawLine(QPointF(5, v), QPointF(27, v));
        }
        break;
    case Icon::Snap:
        p.setPen(Qt::NoPen);
        p.setBrush(kIconStroke);
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
                p.drawEllipse(QPointF(6 + i * 6.7, 6 + j * 6.7), 1.3, 1.3);
        p.setBrush(Qt::NoBrush);
        p.setPen(stroke(kIconYellow, 1.6));
        p.drawLine(QPointF(19.4, 13), QPointF(19.4, 26));
        p.drawLine(QPointF(13, 19.4), QPointF(26, 19.4));
        break;
    case Icon::Ortho:
        p.setPen(stroke(kIconStroke, 2.4));
        p.drawLine(QPointF(7, 5), QPointF(7, 26));
        p.drawLine(QPointF(7, 26), QPointF(28, 26));
        p.setPen(stroke(kIconYellow, 1.4));
        p.drawRect(QRectF(7, 19, 7, 7));
        break;
    case Icon::Polar: {
        p.setPen(stroke(kIconStroke, 2.2));
        p.drawLine(QPointF(5, 26), QPointF(28, 26));
        QPen dashed = stroke(QColor(120, 220, 140), 2.0);
        dashed.setDashPattern({2, 1.5});
        p.setPen(dashed);
        p.drawLine(QPointF(5, 26), QPointF(24, 7));
        p.setPen(stroke(kIconYellow, 1.6));
        p.drawArc(QRectF(-5, 16, 20, 20), 0, 45 * 16);
        break;
    }
    case Icon::Osnap:
        p.setPen(stroke(kIconStroke, 1.4));
        p.drawLine(QPointF(16, 3), QPointF(16, 29));
        p.drawLine(QPointF(3, 16), QPointF(29, 16));
        p.setPen(stroke(kIconYellow, 2.2));
        p.drawRect(QRectF(10, 10, 12, 12));
        break;
    case Icon::OTrack: {   // punto agganciato con le linee di allineamento
        QPen dashed = stroke(QColor(120, 220, 140), 1.8);
        dashed.setDashPattern({2, 1.5});
        p.setPen(dashed);
        p.drawLine(QPointF(3, 12), QPointF(29, 12));
        p.drawLine(QPointF(12, 3), QPointF(12, 29));
        p.setPen(stroke(kIconStroke, 1.6));
        p.drawLine(QPointF(9, 12), QPointF(15, 12));
        p.drawLine(QPointF(12, 9), QPointF(12, 15));
        p.setPen(stroke(kIconYellow, 2.0));
        p.drawRect(QRectF(19, 19, 9, 9));
        break;
    }
    case Icon::DynInput:
        p.setPen(stroke(kIconStroke, 1.4));
        p.drawLine(QPointF(10, 3), QPointF(10, 17));
        p.drawLine(QPointF(3, 10), QPointF(17, 10));
        p.setPen(stroke(kIconStroke, 1.4));
        p.setBrush(QColor(30, 34, 42));
        p.drawRect(QRectF(13, 16, 16, 10));
        p.setBrush(Qt::NoBrush);
        p.setPen(stroke(kIconYellow, 1.6));
        p.drawLine(QPointF(16, 21), QPointF(24, 21));
        break;
    case Icon::Lineweight:
        // Tre linee di spessore crescente.
        p.setPen(stroke(kIconStroke, 1.2));
        p.drawLine(QPointF(5, 8), QPointF(27, 8));
        p.setPen(stroke(kIconStroke, 2.6));
        p.drawLine(QPointF(5, 15), QPointF(27, 15));
        p.setPen(stroke(kIconYellow, 4.5));
        p.drawLine(QPointF(5, 24), QPointF(27, 24));
        break;
    }
}

QIcon build(Icon id)
{
    QIcon result;
    for (int size : {16, 20, 24, 32, 40, 48, 64}) {
        QPixmap pm(size, size);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.scale(size / 32.0, size / 32.0);
        draw(p, id);
        p.end();
        result.addPixmap(pm);
    }
    return result;
}

} // namespace

QIcon icon(Icon id)
{
    static QHash<int, QIcon> cache;
    const int key = static_cast<int>(id);
    auto it = cache.find(key);
    if (it == cache.end())
        it = cache.insert(key, build(id));
    return it.value();
}

// Icona fatta da una o due lettere (pulsanti di formattazione: G, C, S, barrato...).
QIcon glyphIcon(const QString& text, bool bold, bool italic, int decoration)
{
    QPixmap pm(32, 32);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);
    QFont f(QStringLiteral("Segoe UI"));
    f.setPixelSize(text.size() > 1 ? 17 : 22);
    f.setBold(bold);
    f.setItalic(italic);
    p.setFont(f);
    p.setPen(QColor(222, 226, 232));
    const QRect r(0, 0, 32, 32);
    p.drawText(r, Qt::AlignCenter, text);
    const QRectF box = p.boundingRect(r, Qt::AlignCenter, text);
    p.setPen(QPen(QColor(222, 226, 232), 2.0));
    if (decoration == 1)   // sottolineato
        p.drawLine(QPointF(box.left() + 2, box.bottom() - 3), QPointF(box.right() - 2, box.bottom() - 3));
    if (decoration == 2)   // sopralineato
        p.drawLine(QPointF(box.left() + 2, box.top() + 4), QPointF(box.right() - 2, box.top() + 4));
    if (decoration == 3)   // barrato
        p.drawLine(QPointF(box.left() + 1, box.center().y() + 1), QPointF(box.right() - 1, box.center().y() + 1));
    return QIcon(pm);
}

} // namespace cartesio
