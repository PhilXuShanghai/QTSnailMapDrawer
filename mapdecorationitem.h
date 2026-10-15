#ifndef MAPDECORATIONITEM_H
#define MAPDECORATIONITEM_H

#include <QGraphicsItem>

class MapPointItem;

// 点的装饰子图元：围合方块底色 + 类型/ID 标签。
//
// 设计要点：
//  - 作为点的【子图元】（setParentItem）——子随父动是框架免费的：点被拖动时自动
//    跟随、点删除时自动陪葬、快照恢复重建点时自动复活，零注册代码
//  - 方块几何在 paint() 里现场探测（四向射线找最近墙线求围合）：墙一挪，
//    场景脏区与本图元的 boundingRect 相交 → 框架自动触发重绘重算。
//    不需要任何通知机制，"paint 期间只读查询"是安全的
//  - 纯视觉，不进序列化：collectItems 按类型过滤，天然排除；
//    不设任何 flag：不抢点击、不参与选中
//  - Marker（墙端点）不挂本装饰——数量最大的种群，标签只会添乱
class MapDecorationItem : public QGraphicsItem
{
public:
    explicit MapDecorationItem(MapPointItem *owner);

    // 点类型被转换后调用：包围盒尺寸可能翻转（标签型↔方块型），
    // 必须先 prepareGeometryChange 再重绘
    void refresh();

    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option,
               QWidget *widget) override;

private:
    // 是否需要围合方块（货位类要；道路点/机器人只要标签）
    bool wantsBlock() const;
    // 四向射线找包围墙，成功时 outBlockScene 返回场景坐标的方块矩形
    bool findEnclosure(QRectF &outBlockScene) const;
    QColor blockColor() const;    // 每种桌位一种底色
    QString labelText() const;    // 类型名 + 业务ID

    MapPointItem *m_owner;
    static constexpr qreal kSearchRadius = 400.0;  // 探测半径：盖住 500 网格角到角
    static constexpr qreal kMinWallDist  = 20.0;   // 距离过近的墙不算（穿过点自身的）
};

#endif // MAPDECORATIONITEM_H
