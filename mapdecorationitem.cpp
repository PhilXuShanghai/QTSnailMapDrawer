#include "mapdecorationitem.h"

#include "mappointitem.h"
#include "maplineitem.h"

#include <QFont>
#include <QGraphicsScene>
#include <QLineF>
#include <QPainter>

MapDecorationItem::MapDecorationItem(MapPointItem *owner)
    : m_owner(owner)
{
    // 子随父动：点被拖动/删除/快照重建时自动跟随，框架免费提供
    setParentItem(owner);
    setAcceptedMouseButtons(Qt::NoButton);   // 纯装饰，不抢任何事件
}

void MapDecorationItem::refresh()
{
    // 类型变了 → wantsBlock 可能翻转 → boundingRect 尺寸变，改前必须告诉框架
    prepareGeometryChange();
    update();
}

bool MapDecorationItem::wantsBlock() const
{
    using PT = MapPointItem::PointType;
    switch (m_owner->pointType()) {
    case PT::Storage:
    case PT::TmpStorage:
    case PT::RobHome:
    case PT::Unload:
        return true;
    default:
        return false;   // 道路点/机器人点：只标 ID，不画方块（道路一般非封闭结构）
    }
}

QColor MapDecorationItem::blockColor() const
{
    using PT = MapPointItem::PointType;
    switch (m_owner->pointType()) {
    case PT::Storage:    return QColor(230, 160, 30, 55);   // 货位：橙
    case PT::TmpStorage: return QColor(160, 120, 210, 55);  // 临时货位：紫
    case PT::RobHome:    return QColor(70, 160, 90, 55);    // 机器人老家：绿
    case PT::Unload:     return QColor(200, 90, 90, 55);    // 卸货点：红
    default:             return QColor(120, 120, 120, 40);
    }
}

QString MapDecorationItem::labelText() const
{
    using PT = MapPointItem::PointType;
    const QString name = m_owner->name();
    switch (m_owner->pointType()) {
    case PT::Storage:    return QStringLiteral("货位 ") + name;
    case PT::TmpStorage: return QStringLiteral("临时 ") + name;
    case PT::RobHome:    return QStringLiteral("老家 ") + name;
    case PT::Unload:     return QStringLiteral("卸货 ") + name;
    case PT::Rob:        return QStringLiteral("机器 ") + name;
    case PT::Road:       return name;   // 道路点：只标位置ID，不占地方
    default:             return name;
    }
}

bool MapDecorationItem::findEnclosure(QRectF &outBlockScene) const
{
    // 以点为中心向左/右/上/下四向发射线段，各找最近的相交线，
    // 四个方向都找到 → 视为被围合，方块 = 四条最近线围出的矩形
    const QPointF c = m_owner->scenePos();
    const QLineF rays[4] = {
        QLineF(c, c + QPointF(-kSearchRadius, 0)),
        QLineF(c, c + QPointF( kSearchRadius, 0)),
        QLineF(c, c + QPointF(0, -kSearchRadius)),
        QLineF(c, c + QPointF(0,  kSearchRadius)),
    };
    qreal dist[4] = {-1, -1, -1, -1};

    // 点自己挂着的线穿过中心（点在墙角时它的两条墙就是），排除掉
    const QVector<MapLineItem *> attached = m_owner->attachedLines();
    const QRectF probe(c - QPointF(kSearchRadius, kSearchRadius),
                       QSizeF(kSearchRadius * 2, kSearchRadius * 2));

    const QList<QGraphicsItem *> nearItems = scene()->items(probe);
    for (QGraphicsItem *it : nearItems) {
        auto *line = qgraphicsitem_cast<MapLineItem *>(it);
        if (!line || attached.contains(line))
            continue;
        const QLineF seg = line->sceneLine();
        for (int i = 0; i < 4; ++i) {
            QPointF ip;
            if (seg.intersects(rays[i], &ip) == QLineF::BoundedIntersection) {
                const qreal d = QLineF(c, ip).length();
                if (d > kMinWallDist && (dist[i] < 0 || d < dist[i]))
                    dist[i] = d;
            }
        }
    }

    for (int i = 0; i < 4; ++i)
        if (dist[i] < 0)
            return false;   // 有方向找不到墙：不围合，退化为只画标签

    outBlockScene = QRectF(c.x() - dist[0], c.y() - dist[2],
                           dist[0] + dist[1], dist[2] + dist[3]);
    return true;
}

QRectF MapDecorationItem::boundingRect() const
{
    if (wantsBlock())
        return QRectF(-kSearchRadius, -kSearchRadius,
                      kSearchRadius * 2, kSearchRadius * 2);
    // 标签类点用小包围盒：省重绘（任何落入大盒子的改动都会触发本图元重算）
    return QRectF(-120, -80, 240, 100);
}

void MapDecorationItem::paint(QPainter *painter, const QStyleOptionGraphicsItem * /*option*/,
                              QWidget * /*widget*/)
{
    // ---- 围合方块（仅货位类；找不到完整围合就跳过，只剩标签）----
    QPointF labelCenter(0, -50);   // 默认：标签悬在点上方
    qreal blockShortSide = 0;      // >0 = 有方块（字号随它缩放）
    if (wantsBlock()) {
        QRectF blockScene;
        if (findEnclosure(blockScene)) {
            const QRectF block = mapRectFromScene(blockScene);
            painter->setPen(Qt::NoPen);
            painter->setBrush(QBrush(blockColor()));
            painter->drawRect(block);
            labelCenter = block.center();   // 有方块时标签居中在方块里：一眼对应"这个格子是这个ID"
            blockShortSide = qMin(block.width(), block.height());
        }
    }

    // ---- 标签：类型 + 位置ID（机器人路线指令图的核心可视化）----
    const QString text = labelText();
    if (text.isEmpty())
        return;

    // 字号随方块短边缩放：500 短边 → 24px 基准，夹在 [16,40]——
    // 大格子字大醒目、小格子不糊成一团；无方块用固定字号
    int fontPx = 24;
    if (blockShortSide > 0)
        fontPx = qBound(16, qRound(blockShortSide * 24.0 / 500.0), 40);

    QFont f = painter->font();
    f.setPixelSize(fontPx);
    f.setBold(true);
    f.setFamily(QStringLiteral("Microsoft YaHei"));
    painter->setFont(f);

    // 标签框尺寸随字号走：高 = 字号 + 12，宽随文字实测 + 两侧 padding
    const qreal tw = painter->fontMetrics().horizontalAdvance(text);
    const qreal boxH = fontPx + 12;
    const QRectF box(labelCenter.x() - tw / 2 - boxH * 0.35,
                     labelCenter.y() - boxH / 2,
                     tw + boxH * 0.7, boxH);
    painter->setPen(QPen(QColor(30, 30, 30, 220), 0));
    painter->setBrush(QBrush(QColor(255, 255, 255, 185)));
    painter->drawRoundedRect(box, 8, 8);
    painter->drawText(box, Qt::AlignCenter, text);
}
