#include "maproaditem.h"

#include <QPainter>
#include <QPainterPath>

// 道路的方向/禁行装饰（占位版）：
// 单行道画中点箭头指示通行方向，禁行道画叉，双行道只画基线。
// TODO(道路渲染升级)：查周围墙生成传送带效果——场景查询用 scene()->items(QRectF)，
// 注意 paint 期间禁止增删图元，只读查询是安全的
void MapRoadItem::reverseRoad(){
    if (lineType() == MapLineItem::LineType::single_road_forward)
        setLineType(MapLineItem::LineType::single_road_backward);
    else if (lineType() == MapLineItem::LineType::single_road_backward)
        setLineType(MapLineItem::LineType::single_road_forward);
    updateGeometry();
}

void MapRoadItem::paintDetail(QPainter *painter) const
{
    const QLineF l = sceneLine();
    const QPointF mid = l.pointAt(0.5);
    const QPointF dir = (l.p2() - l.p1()) / l.length();
    // 垂直方向（画箭头两翼用）
    const QPointF normal(-dir.y(), dir.x());

    QPen pen(QColor(70, 110, 200), 1, Qt::SolidLine);
    pen.setCosmetic(true);
    painter->setPen(pen);

    switch (lineType()) {
    case LineType::single_road_forward: {
        // 起点指向终点的箭头
        const QPointF tip = mid + dir * 8.0;
        QPainterPath path;
        path.moveTo(tip);
        path.lineTo(mid - dir * 6.0 + normal * 5.0);
        path.lineTo(mid - dir * 6.0 - normal * 5.0);
        path.closeSubpath();
        painter->setBrush(QColor(70, 110, 200));
        painter->drawPath(path);
        break;
    }
    case LineType::single_road_backward: {
        // 终点指向起点的箭头
        const QPointF tip = mid - dir * 8.0;
        QPainterPath path;
        path.moveTo(tip);
        path.lineTo(mid + dir * 6.0 + normal * 5.0);
        path.lineTo(mid + dir * 6.0 - normal * 5.0);
        path.closeSubpath();
        painter->setBrush(QColor(70, 110, 200));
        painter->drawPath(path);
        break;
    }
    case LineType::disable_road: {
        // 禁行：中点一个叉
        const qreal r = 6.0;
        QPen pen = painter->pen();
        pen.setColor("red");
        pen.setWidthF(3.0);             // 加粗
        pen.setCapStyle(Qt::RoundCap);  // 圆头
        painter->setPen(pen);

        painter->drawLine(mid - QPointF(r, r), mid + QPointF(r, r));
        painter->drawLine(mid + QPointF(r, -r), mid + QPointF(-r, r));
        break;
    }
    default:
        break;   // 双行道：只有基线
    }
}
