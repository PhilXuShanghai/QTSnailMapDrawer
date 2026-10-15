#ifndef MAPGROUNDITEM_H
#define MAPGROUNDITEM_H

#include "maplineitem.h"

// 地线：绿色虚线。种类由类本身决定，构造时不接受 LineType
class MapGroundItem : public MapLineItem
{
public:
    MapGroundItem(int id, MapPointItem *start, MapPointItem *end,
                  QGraphicsItem *parent = nullptr)
        : MapLineItem(id, start, end, LineType::Ground, parent) {}

protected:
    QPen pen() const override { return QPen(QColor(0, 150, 60), 2, Qt::DashLine); }
};

#endif // MAPGROUNDITEM_H
