#ifndef MAPPOINTITEM_H
#define MAPPOINTITEM_H

#include <QGraphicsEllipseItem>
#include <QVector>

class MapLineItem;
class MapDecorationItem;

// 画布上的一个"点"图元。
// 椭圆以 (0,0) 为中心，因此 pos() 即点的场景坐标，CSV 序列化时直接取 pos()。
// 点还维护一张"相连线"的注册表，移动时通知它们跟随（见 itemChange）。
class MapPointItem : public QGraphicsEllipseItem
{
 Q_GADGET
public:
    enum class PointType : int {
        Rob =  5,   // 机器人点
        TmpStorage =  4,   // 临时货位
        Unload   =  3,   // 卸货点
        RobHome   =  2,   // 机器人老家
        Storage   =  1,   // 库位点
        Road   =  0,   // 道路点
        Marker = -1,   // 标记点
    };
    Q_ENUM(PointType)

    enum { Type = UserType + 2 };
    int type() const override { return Type; }

    static constexpr qreal Radius = 4.0;

    explicit MapPointItem(int id , PointType pType = PointType::Marker, QGraphicsItem *parent = nullptr);
    ~MapPointItem() override;

    int id() const { return m_id; }

    PointType pointType() const { return m_type; }

    // 转换类型（转换桌位功能用）：装饰子图元随类型重建/刷新——
    // 方块、标签文案、包围盒尺寸全部依赖类型
    void setPointType(PointType pType);

    // 挂在本点上的线数（判断"孤儿点"用：0 = 没有任何线经过它）
    int lineCount() const { return m_lines.size(); }
    // 挂在本点上的所有线（副本；合并/序列化/undo 等需要遍历时用）
    QVector<MapLineItem *> attachedLines() const { return m_lines; }

    QString name() const { return m_name; }
    void setName(const QString &name) { m_name = name; }

protected:
    // 开启了 ItemSendsGeometryChanges 后，位置/选中状态等变化时框架会回调这里
    QVariant itemChange(GraphicsItemChange change, const QVariant &value) override;

private:
    friend class MapLineItem;  // 线在构造/析构时维护自己的注册表
    void registerLine(MapLineItem *line);
    void unregisterLine(MapLineItem *line);

    int m_id;
    PointType m_type;
    QString m_name;
    MapDecorationItem *m_decoration = nullptr;  // 装饰子图元（Marker 为空）
    QVector<MapLineItem *> m_lines;  // 连接到本点的所有线
};

#endif // MAPPOINTITEM_H
