#include "maplineitem.h"

#include "mappointitem.h"
#include "mapwallitem.h"
#include "mapgrounditem.h"
#include "maproaditem.h"

#include <QColor>
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QSet>
#include <QStyleOptionGraphicsItem>
#include <utility>

// 工厂方法
// 新增线种类 = 加一个派生类 + 这里一个 case（兼容改造前代码）
MapLineItem *MapLineItem::create(int id, MapPointItem *start, MapPointItem *end, LineType type)
{
    switch (type) {
    case LineType::Wall:
        return new MapWallItem(id, start, end);
    case LineType::Ground:
        return new MapGroundItem(id, start, end);
    case LineType::single_road_forward:
    case LineType::single_road_backward:
    case LineType::double_road:
    case LineType::disable_road:
        return new MapRoadItem(id, start, end, type);
    }
    return new MapWallItem(id, start, end);
}

MapLineItem::MapLineItem(int id, MapPointItem *start, MapPointItem *end, LineType type,
                         QGraphicsItem *parent)
    : QGraphicsItem(parent)
    , m_id(id)
    , m_start(start)
    , m_end(end)
    , m_type(type)
{
    // 线可以被框选/点选（配合 Delete 键删除）
    setFlag(QGraphicsItem::ItemIsSelectable);
    // 点的 zValue 是 10，线放在点下面，端点圆始终可见
    setZValue(5);

    // 构造时 item 还没进场景，直接算好缓存即可（不需要 prepareGeometryChange）
    refreshGeometry();

    // 双向注册：把自己登记到两个端点上，端点移动时才会通知到本线
    m_start->registerLine(this);
    m_end->registerLine(this);
}

MapLineItem::~MapLineItem()
{
    // 从两个端点的注册表中把自己摘掉。
    // 注意：如果端点先被删，它的析构会先级联 delete 本线，不会走到这里悬空访问
    if (m_start)
        m_start->unregisterLine(this);
    if (m_end)
        m_end->unregisterLine(this);
}

void MapLineItem::refreshGeometry()
{
    m_line = QLineF(m_start->pos(), m_end->pos());
    m_boundingRect = QRectF(m_line.p1(), m_line.p2()).normalized()
                         .adjusted(-kBoundingMargin, -kBoundingMargin,
                                    kBoundingMargin,  kBoundingMargin);
}

void MapLineItem::updateGeometry()
{
    // boundingRect 可能变小也可能变大，改之前必须先告诉框架
    prepareGeometryChange();
    refreshGeometry();
}

QPainterPath MapLineItem::shape() const
{
    // 默认 shape = boundingRect（整块矩形都可点击），不适合细线。
    // 用 stroker 把线"撑粗"成 12 单位宽的路径，点击线附近就算选中
    QPainterPath path;
    path.moveTo(m_line.p1());
    path.lineTo(m_line.p2());
    QPainterPathStroker stroker;
    stroker.setWidth(12.0);
    return stroker.createStroke(path);
}

void MapLineItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option,
                        QWidget * /*widget*/)
{
    const QPen basePen = pen();   // 派生类决定的主笔刷

    // 选中时先垫一层半透明粗线做高亮
    if (option->state & QStyle::State_Selected) {
        QColor highlight = basePen.color();
        highlight.setAlpha(100);
        painter->setPen(QPen(highlight, basePen.widthF() + 6));
        painter->drawLine(m_line);
    }

    painter->setPen(basePen);
    painter->drawLine(m_line);

    // 派生类的附加装饰（道路的箭头/传送带效果将来画在这里）
    paintDetail(painter);
}

void MapLineItem::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
    // 只接管按下以获得鼠标抓取（选择高亮由场景负责，ItemIsSelectable 已开）
    event->accept();
}

void MapLineItem::mouseMoveEvent(QGraphicsSceneMouseEvent *event)
{
    const QPointF delta = event->scenePos() - event->lastScenePos();
    if (delta.isNull())
        return;

    // 组移动：框选多个图元后拖其中一条 = 整组一起平移（与拖动点的内置组行为一致）。
    // "移动一条线"的实质 = 平移它的两个端点；QSet 去重防止
    // "点和它的线同时被选中"时同一端点被平移两次
    QSet<MapPointItem *> targets;
    if (scene()) {
        for (QGraphicsItem *it : scene()->selectedItems()) {
            if (auto *p = qgraphicsitem_cast<MapPointItem *>(it)) {
                targets.insert(p);
            } else if (auto *l = qgraphicsitem_cast<MapLineItem *>(it)) {
                targets.insert(l->m_start);
                targets.insert(l->m_end);
            }
        }
    }
    targets.insert(m_start);
    targets.insert(m_end);

    for (MapPointItem *p : std::as_const(targets))
        p->setPos(p->pos() + delta);
    event->accept();
}

void MapLineItem::setStartPoint(MapPointItem *startPoint) {
    if (m_start == startPoint)
        return;
    if (m_start)
        m_start->unregisterLine(this);   // 从旧端点注销
    m_start = startPoint;
    if (m_start)
        m_start->registerLine(this);     // 登记到新端点
    updateGeometry();
}

void MapLineItem::setEndPoint(MapPointItem *endPoint) {
    if (m_end == endPoint)
        return;
    if (m_end)
        m_end->unregisterLine(this);   // 从旧端点注销
    m_end = endPoint;
    if (m_end)
        m_end->registerLine(this);     // 登记到新端点
    updateGeometry();
}

void MapLineItem::setLineType(LineType type) {
    m_type = type;
    updateGeometry(); //刷新渲染
}
