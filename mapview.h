#ifndef MAPVIEW_H
#define MAPVIEW_H

#include <QGraphicsView>

class MapPointItem;
class QGraphicsLineItem;
class MapLineItem;

// 画布视图：负责工具模式切换、鼠标事件入口和缩放。
// 图元的增删由 MainWindow 统一管理，本类只发信号报告"用户在哪点了"。
class MapView : public QGraphicsView
{
    Q_OBJECT

public:
    enum class Tool {
        Select,   // 选择/拖动已有图元
        AddPoint, // 点击画布放置点
        AddLine,  // 依次点击两个点连线（起点→终点，带橡皮筋预览）
    };

    struct SnapHit {
        MapLineItem *line = nullptr;
        MapPointItem *point = nullptr;
        QPointF      pos;            // 线上最近点（场景坐标）
        qreal        dist = 0;
        bool valid() const { return line != nullptr; }
    };

    explicit MapView(QWidget *parent = nullptr);
    MapView(QGraphicsScene *scene, QWidget *parent = nullptr);

    void setTool(Tool tool);
    Tool tool() const { return m_tool; }
    // 连线交互三段式：开始 / 提交 / 取消
    void startPreviewLineDrawing(MapPointItem *start);
    void finishLineDrawing(MapPointItem *endPoint = nullptr);
    void cancelLineDrawing();
    // 场景坐标处的图元是点则返回它（itemAt 返回最顶层的，点 z 值高，优先命中）
    MapPointItem *pointAt(const QPointF &scenePos) const;

signals:
    // 用户在 AddPoint 模式下点击画布，scenePos 已转换为场景坐标
    // 发出的坐标一律为场景坐标，视图坐标不出本层
    void pointRequested(const QPointF &scenePos,MapLineItem* line = nullptr);
    // AddLine 模式下用户点完第二个点（start/end 都保证有效）
    //void lineCreationRequested(MapPointItem *start, MapPointItem *end);
    // 连线交互状态提示（状态栏文案用）
    void lineDrawingClicked(MapPointItem *m_point,const QPointF &scenePos, MapLineItem* line = nullptr,MapPointItem* point = nullptr);
    // 链被取消（Esc/再点链头/切工具/数据修改门）。head 为取消前的链头，可能为 null。
    // MainWindow 借此决定是否回收"为画线而自动创建的孤儿点"——删除权在数据侧
    void lineDrawingCancelled(MapPointItem *head);
    void pointPositionDisplay(const QPointF &scenePos);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void drawBackground(QPainter *painter, const QRectF &exposed) override;
    // 吸附指示器画在这里（所有图元之上）：它是纯瞬态装饰，不做成场景图元，
    // 就不会被 scene->clear() 删除、不占 z 值、不污染命中测试
    void drawForeground(QPainter *painter, const QRectF &exposed) override;

private:

    Tool m_tool = Tool::Select;
    bool m_panning = false;         // 右键平移中
    QPointF m_lastPanPos;           // 上一次鼠标位置（视图坐标）
    MapPointItem *m_lineStart = nullptr;      // 连线的起点（nullptr=未开始连线）
    QGraphicsLineItem *m_previewLine = nullptr; // 橡皮筋预览虚线
    SnapHit snapToLine(QGraphicsScene *scene, const QPointF &scenePos, qreal tol); //吸附判断逻辑
    // 更新吸附指示器状态并请求重绘（实际绘制在 drawForeground）
    void updateSnapIndicator(const SnapHit &hit);

    // 吸附指示器状态（不是图元，只是一组绘制参数）
    bool m_snapActive = false;
    bool m_snapToPoint = false;   // true=吸到点(空心圈)，false=吸到线(实心灰点)
    QPointF m_snapPos;
    static constexpr qreal kSnapRingRadius = 7.0; // 空心圈半径，略大于点本体(4)
};

#endif // MAPVIEW_H
