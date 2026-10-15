#ifndef MAPWALLITEM_H
#define MAPWALLITEM_H

#include "maplineitem.h"

// 墙线：深色实线。种类由类本身决定，构造时不接受 LineType（防止"墙线配地线类型"）
class MapWallItem : public MapLineItem
{
public:
    MapWallItem(int id, MapPointItem *start, MapPointItem *end,
                QGraphicsItem *parent = nullptr)
        : MapLineItem(id, start, end, LineType::Wall, parent) {}

protected:
    QPen pen() const override { return QPen(QColor(60, 60, 60), 3); }
};

#endif // MAPWALLITEM_H
