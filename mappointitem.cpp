#include "mappointitem.h"

#include "maplineitem.h"
#include "mapdecorationitem.h"

#include <QBrush>
#include <QPen>

MapPointItem::MapPointItem(int id, PointType pType, QGraphicsItem *parent)
    : QGraphicsEllipseItem(-Radius, -Radius, Radius * 2, Radius * 2, parent)
    , m_type(pType)
    , m_id(id)
{
    // 框架内置：选中高亮 + 鼠标拖动移动，无需自己处理鼠标事件
    setFlag(QGraphicsItem::ItemIsMovable);
    setFlag(QGraphicsItem::ItemIsSelectable);
    // 移动时通知 itemChange()，"线跟随点"靠它实现
    setFlag(QGraphicsItem::ItemSendsGeometryChanges);

    setPen(QPen(Qt::red, 1));
    setBrush(QBrush(Qt::white));
    // 点显示在线之上
    setZValue(10);

    // 非标记点挂装饰子图元（围合方块 + 类型/ID 标签）：子随父动、删除陪葬、
    // 快照/加载重建点时自动复活——装饰的生命周期永远和点本体一致
    if (pType != PointType::Marker)
        m_decoration = new MapDecorationItem(this);
}

void MapPointItem::setPointType(PointType pType)
{
    if (m_type == pType)
        return;
    m_type = pType;
    if (pType == PointType::Marker) {
        delete m_decoration;            // 转成标记点：装饰撤销
        m_decoration = nullptr;
    } else if (!m_decoration) {
        m_decoration = new MapDecorationItem(this);   // 标记点转回来：装饰重建
    } else {
        m_decoration->refresh();        // 方块/标签/包围盒随类型刷新
    }
    update();
}

MapPointItem::~MapPointItem()
{
    // 删除点时级联删除挂在它身上的线。这里用const是为了确保副本不会被修改。
    // 每条线的析构又会调 unregisterLine 修改 m_lines，
    // 所以必须先复制一份再遍历，迭代器才不会失效
    const QVector<MapLineItem *> lines = m_lines;
    for (MapLineItem *line : lines)
        delete line;
}

QVariant MapPointItem::itemChange(GraphicsItemChange change, const QVariant &value)
{
    if (change == ItemPositionHasChanged) {
        // 本点动了 → 通知所有相连的线重算几何（线里实时取 pos()）
        for (MapLineItem *line : m_lines)
            line->updateGeometry();
    }
    return QGraphicsEllipseItem::itemChange(change, value);
}

void MapPointItem::registerLine(MapLineItem *line)
{
    m_lines.append(line);
}

void MapPointItem::unregisterLine(MapLineItem *line)
{
    m_lines.removeOne(line);
}
