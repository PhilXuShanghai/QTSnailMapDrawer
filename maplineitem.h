#ifndef MAPLINEITEM_H
#define MAPLINEITEM_H

#include <QGraphicsItem>
#include <QLineF>
#include <QPen>

class MapPointItem;

// "线"基类：几何与注册机制的唯一实现，派生类只决定"长什么样"。
//
// 继承体系（行为分类——每种线各自的渲染）：
//   MapLineItem（基类：端点注册/跟随/拖动/命中/几何缓存/ID）
//     ├─ MapWallItem    墙线
//     ├─ MapGroundItem  地线
//     └─ MapRoadItem    道路（将来在这里做查墙生成传送带等复杂渲染）
// LineType 枚举继续存在（数据分类——.rou 的线上格式码，一个值都不能改）。
// 两者分工：枚举是序列化词汇表，类是多态行为表。
//
// 派生类约定：
//   - 全族共享基类 Type = UserType+1：qgraphicsitem_cast<MapLineItem*> 通吃所有线，
//     区分种类用 lineType()。绝不给派生类单独分配 Type 值
//   - 只重写视觉钩子 pen() / paintDetail()，不碰几何与注册
//   - 构造只经工厂 create()（保证 id 分配与种类选择同步），
//     基类构造函数为 protected 正是为此
class MapLineItem : public QGraphicsItem
{
    Q_GADGET
public:
    enum class LineType : int {
        Wall   =  0,   // 墙线（.rou 类型码，不可改值）
        Ground = -1,   // 地线
        single_road_forward = 1, // 单行道（正向）
        single_road_backward = 2,// 单行道（逆向）
        double_road = 3,         // 双行道
        disable_road = 4,        // 禁行道
    };
    Q_ENUM(LineType)

    // 图元类型标识：全族共享一个值，qgraphicsitem_cast<MapLineItem*> 识别所有派生线
    enum { Type = UserType + 1 };
    int type() const override { return Type; }

    // 工厂：唯一合法的构造入口。
    // id 由调用方提供（MapScene 计数器 / 命令规格 / 文件行序号列）
    static MapLineItem *create(int id, MapPointItem *start, MapPointItem *end, LineType type);

    ~MapLineItem() override;

    // --- QGraphicsItem 纯虚函数 ---
    // 视图用 boundingRect 决定"什么时候需要重绘这块区域"，必须完全覆盖线+笔宽
    QRectF boundingRect() const override { return m_boundingRect; }
    // 命中检测区域（点击/框选是否算"碰到"了这条线）——线太细难点中，加宽到 12
    QPainterPath shape() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option,
               QWidget *widget) override;

    MapPointItem *startPoint() const { return m_start; }
    MapPointItem *endPoint() const { return m_end; }
    void setStartPoint(MapPointItem *startPoint);
    void setEndPoint(MapPointItem *endPoint);
    // 线的唯一身份：与点 ID 同级的身份系统（命令规格/差分快照用）
    int id() const { return m_id; }
    // 业务语义上的线种类（序列化用）。不能叫 type()：与基类虚函数撞车
    void setLineType(LineType type);
    LineType lineType() const { return m_type; }
    // 当前线段几何（两端场景坐标）。必须是 public：MapView 的吸附解析要读它
    QLineF sceneLine() const { return m_line; }

protected:
    // protected：只允许派生类与工厂构造，防止绕过 id 分配直接 new
    MapLineItem(int id, MapPointItem *start, MapPointItem *end, LineType type,
                QGraphicsItem *parent = nullptr);

    // ---- 派生类的视觉钩子（只许重写这两个，别碰几何/注册）----
    virtual QPen pen() const = 0;                              // 本种线的主笔刷
    virtual void paintDetail(QPainter * /*painter*/) const {}  // 主线之上的附加装饰
    // 注意：基类构造期不会调用 pen()——虚函数在构造函数里派发不到派生类，
    // 这正是外接矩形边距用常量 kBoundingMargin 而非笔宽的原因

    // 拖动线段 = 平移它的两个端点。不能设 ItemIsMovable：那会改线自己的 pos()，
    // 而线的几何取自端点——等于把线从点上"扯下来"。端点一动，线经通知链自动重算
    void mousePressEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent *event) override;

protected:
    void updateGeometry();      // 刷新渲染
private:
    friend class MapPointItem;  // 允许点直接调 updateGeometry
    void refreshGeometry();     // 只重算缓存，不管场景登记（构造时用）

    // 外接矩形固定边距：覆盖最粗笔宽一半 + 选中高亮外扩。
    // 用常量而非 pen().widthF() 是为了避开"基类构造期调用虚函数"陷阱
    static constexpr qreal kBoundingMargin = 8.0;

    int m_id;                   // 线身份（序列化到 .rou 墙表序号列，随文件往返）
    MapPointItem *m_start;      // 起点（弱引用：点被删时线会被级联删除）
    MapPointItem *m_end;
    LineType m_type;
    QLineF m_line;              // 缓存的线段（两端为场景坐标）
    QRectF m_boundingRect;      // 缓存的外接矩形
};

#endif // MAPLINEITEM_H
