#ifndef MAPROADITEM_H
#define MAPROADITEM_H

#include "maplineitem.h"

// 道路线：基线 + 方向/禁行装饰。
// 与墙/地不同：道路有四种 LineType（正向单行/逆向单行/双行/禁行），
// 所以构造时接受 roadKind，方向语义存进 lineType()（序列化词汇表的一部分）
class MapRoadItem : public MapLineItem
{
public:
    MapRoadItem(int id, MapPointItem *start, MapPointItem *end, LineType roadKind,
                QGraphicsItem *parent = nullptr)
        : MapLineItem(id, start, end, roadKind, parent) {}

    void reverseRoad();

protected:
    QPen pen() const override
    {
        // 道路基线（暂用蓝色占位，方向差异在 paintDetail 里表达）
        return QPen(QColor(70, 110, 200), 4);
    }

    void paintDetail(QPainter *painter) const override;

private:
    // 道路专属渲染辅助（将来：查周围墙生成传送带效果/方向箭头/禁行标记）
    // 注意：paintDetail 在 paint 内被调，painter 坐标已是场景坐标
};

#endif // MAPROADITEM_H
