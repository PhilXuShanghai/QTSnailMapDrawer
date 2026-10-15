#include "mapview.h"

#include "mappointitem.h"
#include "maplineitem.h"
#include "mapmath.h"
#include <QKeyEvent>
#include <QMouseEvent>
#include <QScrollBar>
#include <QWheelEvent>

MapView::MapView(QWidget *parent)
    : QGraphicsView(parent)
{
    // 配置渲染方式：开启抗锯齿
    setRenderHint(QPainter::Antialiasing);
    // 框选模式：空白处按下拖动=框选图元，点在图元上按下=移动图元（默认模式是框选动作）
    setDragMode(QGraphicsView::RubberBandDrag);
    // 缩放时以鼠标位置为锚点
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
}

MapView::MapView(QGraphicsScene *scene, QWidget *parent)
    : MapView(parent)
{
    QGraphicsView::setScene(scene);
}

void MapView::setTool(Tool tool)
{
    if (tool != m_tool) {
        // 切走时若连线画到一半，先取消
        if (tool != Tool::AddLine)
            cancelLineDrawing();
        m_tool = tool;
        // 离开绘制工具时收起吸附指示器
        if (tool == Tool::Select) {
            m_snapActive = false;
            viewport()->update();
        }
    }
    // 光标反馈：绘制类工具显示十字准星
    setCursor(tool == Tool::Select ? Qt::ArrowCursor : Qt::CrossCursor);
}

MapPointItem *MapView::pointAt(const QPointF &scenePos) const
{
    // itemAt 只返回最顶层的一个；点的 zValue(10) 比线(5)高，
    // 重叠处优先命中点——正好是连线想要的
    QGraphicsItem *item = scene()->itemAt(scenePos, QTransform());
    return item ? dynamic_cast<MapPointItem *>(item) : nullptr;
}

void MapView::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::RightButton) {
        // 设置正在平移
        m_panning = true;
        m_lastPanPos = event->position();
        // 设置鼠标准星
        setCursor(Qt::ClosedHandCursor);
        // 向拦截器汇报处理事件已处理
        event->accept();
        return;
    }
    if (m_tool == Tool::AddPoint && event->button() == Qt::LeftButton) {
        // 关键：鼠标事件给的是视图坐标，必须转成场景坐标再落点，
        // 否则缩放/滚动后位置会错位
        const QPointF sp  = mapToScene(event->position().toPoint());
        const qreal   tol = 10.0 / transform().m11(); //获取当前设备屏幕上10px

        SnapHit hit = snapToLine(scene(), sp, tol);
        if (hit.point) {
            // 该位置已有现成的点：不叠新点——预览圈罩着它，点击就该是空操作
        } else {
            emit pointRequested(hit.pos, hit.line);
        }
        event->accept();
        return;
    }
    if (m_tool == Tool::AddLine && event->button() == Qt::LeftButton) {
        const QPointF sp  = mapToScene(event->position().toPoint());
        const qreal   tol = 10.0 / transform().m11(); //获取当前设备屏幕上10px

        SnapHit hit = snapToLine(scene(), sp, tol);
        emit lineDrawingClicked(m_lineStart, hit.pos, hit.line,hit.point);
        event->accept();
        return;
    }
    QGraphicsView::mousePressEvent(event);
}

void MapView::mouseMoveEvent(QMouseEvent *event)
{
    // 发送鼠标位置显示下拉栏
    emit pointPositionDisplay(mapToScene(event->position().toPoint()));
    if (m_panning) {
        // 平移中收起吸附指示器，避免它冻结在原地
        m_snapActive = false;
        viewport()->update();

        QScrollBar *h = horizontalScrollBar();
        QScrollBar *v = verticalScrollBar();
        h->setValue(h->value() - (event->position().x() - m_lastPanPos.x()));
        v->setValue(v->value() - (event->position().y() - m_lastPanPos.y()));
        m_lastPanPos = event->position();
        event->accept();
        return;
    }
    if (m_tool == Tool::AddPoint || m_tool == Tool::AddLine) {
        const QPointF sp = mapToScene(event->position().toPoint());
        const qreal tol = 10.0 / transform().m11();
        const SnapHit hit = snapToLine(scene(), sp, tol);

        updateSnapIndicator(hit);

        if (m_tool == Tool::AddLine && m_lineStart) {
            // 橡皮筋终点磁吸到吸附位置
            m_previewLine->setLine(QLineF(m_lineStart->pos(), hit.pos));
        }
        event->accept();
        return;
    }
    QGraphicsView::mouseMoveEvent(event);
}
/**
 * @brief MapView::mouseReleaseEvent 鼠标离开后切换指针类型
 * @param event
 */
void MapView::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::RightButton && m_panning) {
        m_panning = false;
        setCursor(m_tool == Tool::Select ? Qt::ArrowCursor : Qt::CrossCursor);
        event->accept();
        return;
    }
    QGraphicsView::mouseReleaseEvent(event);
}
/**
 * @brief MapView::keyPressEvent Esc 取消画到一半的线
 * @param event
 */
void MapView::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape && m_lineStart) {
        cancelLineDrawing();
        event->accept();
        return;
    }
    QGraphicsView::keyPressEvent(event);
}
/**
 * @brief MapView::wheelEvent 滚轮缩放
 * @param event
 */
void MapView::wheelEvent(QWheelEvent *event)
{
    // 滚轮缩放，AnchorUnderMouse 保证以鼠标位置为中心
    const double factor = event->angleDelta().y() > 0 ? 1.25 : 0.8;
    // 进行等比缩放
    scale(factor, factor);
    event->accept();
}

void MapView::startPreviewLineDrawing(MapPointItem *start)
{
    m_lineStart = start;

    // 预览虚线：宽度 0 = 装饰笔，缩放时屏幕上恒 1px
    m_previewLine = new QGraphicsLineItem;
    m_previewLine->setPen(QPen(QColor(100, 100, 100), 0, Qt::DashLine));
    m_previewLine->setZValue(15); // 盖在线和点之上，但抢不走点击（未设任何 flag）
    m_previewLine->setLine(QLineF(start->pos(), start->pos()));
    scene()->addItem(m_previewLine);
}

void MapView::finishLineDrawing(MapPointItem *endPoint)
{
    // QGraphicsItem 析构时会自动把自己从场景移除
    if(m_previewLine){
        delete m_previewLine;
        m_previewLine = nullptr;
    }

    // 把之前的结束作为自己的开始
    m_lineStart = endPoint;
    startPreviewLineDrawing(m_lineStart);
}

void MapView::cancelLineDrawing()
{
    if (!m_lineStart)
        return;
    MapPointItem *head = m_lineStart;
    delete m_previewLine;
    m_previewLine = nullptr;
    m_lineStart = nullptr;
    // 交给 MainWindow 决定是否回收自动创建的孤儿点（图元删除权在数据侧）
    emit lineDrawingCancelled(head);
}

void MapView::drawBackground(QPainter *painter, const QRectF &exposed)
{
    const qreal gridStep = 100;

    painter->save();

    QPen gridPen;
    gridPen.setColor(QColor(120,120,120,80));
    gridPen.setWidth(0.0);
    painter->setPen(gridPen);

    qreal left = qFloor(exposed.left()/gridStep) * gridStep;
    qreal right = exposed.right();
    qreal top = qFloor(exposed.top()/gridStep)* gridStep;
    qreal bottom = exposed.bottom();
    // 绘制栅格线
    for(qreal x = left; x <= right ; x += gridStep){
        painter->drawLine(QPointF(x,exposed.top()),QPointF(x,exposed.bottom()));
    }

    for(qreal y = top; y <= bottom ; y += gridStep){
        painter->drawLine(QPointF(exposed.left(),y),QPointF(exposed.right(),y));
    }

    gridPen.setColor(QColor("Black"));
    painter->setPen(gridPen);
    // 绘制原点坐标
    painter->drawLine(QPointF(0,exposed.top()),QPointF(0,exposed.bottom()));
    painter->drawLine(QPointF(exposed.left(),0),QPointF(exposed.right(),0));

    painter -> restore();
}

void MapView::updateSnapIndicator(const SnapHit &hit)
{
    m_snapActive = (hit.point != nullptr || hit.line != nullptr);
    m_snapToPoint = (hit.point != nullptr);
    m_snapPos = hit.pos;
    // 指示器不是场景图元！框架不会感知它的变化，主动请求一次重绘！
    viewport()->update();
}

void MapView::drawForeground(QPainter *painter, const QRectF &exposed)
{
    Q_UNUSED(exposed);
    if (!m_snapActive)
        return;
    // 笔宽 0 = 装饰笔：无论缩放多少，屏幕上恒 1px
    if (m_snapToPoint) {
        // 吸到点：灰色空心圈罩住点本体（点半径 4，圈 7）
        painter->setPen(QPen(QColor(90, 90, 90), 0));
        painter->setBrush(Qt::NoBrush);
        painter->drawEllipse(m_snapPos, kSnapRingRadius, kSnapRingRadius);
    } else {
        // 吸到线：实心小灰点落在投影位置，标示"新点会落在这"
        painter->setPen(Qt::NoPen);
        painter->setBrush(QBrush(QColor(128, 128, 128, 160)));
        painter->drawEllipse(m_snapPos, 3.0, 3.0);
    }
}

void MapView::leaveEvent(QEvent *event)
{
    // 鼠标离开画布：收起吸附指示器
    m_snapActive = false;
    viewport()->update();
    QGraphicsView::leaveEvent(event);
}

MapView::SnapHit MapView::snapToLine(QGraphicsScene *scene, const QPointF &scenePos, qreal tol)
{
    SnapHit best;
    best.dist = tol;
    // 默认为原始位置未吸附
    best.pos = scenePos;

    bool pointFlag = false;

    // 粗筛：只取光标附近一个 2*tol 见方的区域内的 item
    const QRectF probe(scenePos - QPointF(tol, tol), QSizeF(2 * tol, 2 * tol));

    for (QGraphicsItem *it : scene->items(probe)) {
        auto *point = qgraphicsitem_cast<MapPointItem *>(it); // 找到MapPointItem
        auto *line = qgraphicsitem_cast<MapLineItem *>(it);   // 找出MapLineItem
        if(point){
            pointFlag = true;
            const qreal d = QLineF(point->pos(), scenePos).length();
            if(d < best.dist){
                best.point = point;
                best.dist = d;
                best.pos  = point->pos();   // 吸到点时，吸附位置=点中心
            }
        }
        if (!line) continue;

        if(!pointFlag){
            const QLineF l  = line->sceneLine();
            const QPointF cp = MapMath::closestPointOnSegment(scenePos, l.p1(), l.p2());
            const qreal d    = QLineF(scenePos, cp).length();

            if (d < best.dist) {
                best.dist = d;
                best.line = line;
                best.pos  = cp;
            }
        }
    }
    return best;
}

