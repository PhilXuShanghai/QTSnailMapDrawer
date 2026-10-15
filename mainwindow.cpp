#include "mainwindow.h"
#include "mapIO.h"
#include "mapmath.h"
#include "ui_mainwindow.h"

#include "maproaditem.h"
#include "mapview.h"
#include "moveitemscommand.h"

#include "addItemscommand.h"
#include "snapshotcommand.h"
#include "moveitemscommand.h"

#include <QButtonGroup>
#include <QComboBox>
#include <QGraphicsScene>
#include <QKeyEvent>
#include <QLineEdit>
#include <QStatusBar>
#include <QPushButton>
#include <qfiledialog.h>
#include <qmessagebox.h>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <limits>
#include <qDebug>
#include <QQueue>
#include <QList>
#include <algorithm>
#include <QUndoStack>
#include <QHostAddress>
#include <AppConfig.h>
#include <QNetworkProxy>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    QNetworkProxyFactory::setUseSystemConfiguration(false); // 静态函数
    ui->setupUi(this);

    // 画布：Scene 是逻辑世界，View 是观察它的窗口
    m_scene = new MapScene(this);
    m_scene->setSceneRect(-10000, -10000, 20000, 20000);

    ui->mapView->setScene(m_scene);
    m_view = ui->mapView;

    //初始化所有的绘制button/Comble
    setupTools();
    setupCombo();
    setupTcpUi();   // TCP 模块初始化（RobotManager + 连接表）——之前漏了这一行，
                    // m_robotMgr 保持空指针，点"连接机器人"即崩溃


    ui->addPointButton->setShortcut(QKeySequence("Ctrl+Shift+A"));
    ui->drawWallLineButton->setShortcut(QKeySequence("Ctrl+Shift+W"));
    ui->drawGroundLineButton->setShortcut(QKeySequence("Ctrl+Shift+G"));
    ui->editMoveButton->setShortcut(QKeySequence("Ctrl+Shift+E"));

    // 撤销/重做栈：命令式（纯创建）与快照式（动结构）命令同栈共存
    m_undoStack = new QUndoStack(this);
    m_undoStack->setUndoLimit(100);
    ui->undo_button->setShortcut(QKeySequence::Undo);     // Ctrl+Z
    ui->redo_button->setShortcut(QKeySequence::Redo);     // Ctrl+Y


    // 视图报告交互结果 → 由 MainWindow 统一创建图元
    connect(m_view, &MapView::pointRequested, this, &MainWindow::addPoint);
    connect(m_view,&MapView::lineDrawingClicked,this,&MainWindow::addLine);
    // 取消连线 → 回收"为画线而自动创建、最终一条线都没挂上"的孤儿点
    connect(m_view, &MapView::lineDrawingCancelled, this, [this](MapPointItem *head) {
        if (m_autoChainHead && head == m_autoChainHead && head->lineCount() == 0)
            delete head;
        m_autoChainHead = nullptr;
        statusBar()->showMessage(QStringLiteral("连线已取消"));
    });
    connect(m_view,&MapView::pointPositionDisplay,this,
            [this](const QPointF &scenePos)-> void{statusBar()->showMessage(QStringLiteral("选中位置(%1,%2)")
                                                    .arg(scenePos.x(), 0, 'f', 1).arg(scenePos.y(), 0, 'f', 1));});
    connect(m_scene,&MapScene::itemMoveSignal,this,&MainWindow::movePoint);

    setWindowIcon(QIcon(":/res/icons/icon.png"));
}

MainWindow::~MainWindow()
{
    delete ui;
}


int MainWindow::mapVersionUpdate()
{
    return ++m_mapVersion;
}

void MainWindow::setupTools()
{
    // 所有绘制工具按钮互斥：QButtonGroup 自动处理"按下一个、弹起其余"。
    // 用一个 idClicked 管全部
    ui->addPointButton->setCheckable(true);
    ui->drawWallLineButton->setCheckable(true);
    ui->drawGroundLineButton->setCheckable(true);
    ui->editMoveButton->setCheckable(true);
    ui->generateRoadButton->setCheckable(true);
    ui->newDeskButton->setCheckable(true);

    enum ToolId {
        ToolEditMove,
        ToolDrawPoint,
        ToolDrawWallLine,
        ToolDrawGroundLine,
        ToolGenerateRoad,
        ToolNewDesk
    };

    m_toolButtons = new QButtonGroup(this); // 默认互斥（exclusive）
    m_toolButtons->addButton(ui->editMoveButton, ToolEditMove);
    m_toolButtons->addButton(ui->addPointButton, ToolDrawPoint);
    m_toolButtons->addButton(ui->drawWallLineButton, ToolDrawWallLine);
    m_toolButtons->addButton(ui->drawGroundLineButton, ToolDrawGroundLine);
    m_toolButtons->addButton(ui->generateRoadButton, ToolGenerateRoad);
    m_toolButtons->addButton(ui->newDeskButton, ToolNewDesk);

    connect(m_toolButtons, &QButtonGroup::idClicked, this, [this](int id) {
        // 再次点击已按下的按钮 = 退出该工具，回到选择
        if (!m_toolButtons->checkedButton()) {
            m_view->setTool(MapView::Tool::Select);
            statusBar()->showMessage(QStringLiteral("编辑/移动：拖动图元，Delete 删除选中"));
            return;
        }
        switch (id) {
        case ToolDrawPoint:
            m_view->setTool(MapView::Tool::AddPoint);
            m_pointType = MapPointItem::PointType::Marker;
            statusBar()->showMessage(QStringLiteral("绘制点：单击画布放置"));
            break;
        case ToolDrawWallLine:
            m_lineType = MapLineItem::LineType::Wall;
            m_view->setTool(MapView::Tool::AddLine);
            m_pointType = MapPointItem::PointType::Marker;
            statusBar()->showMessage(QStringLiteral("绘制墙线：依次单击两个点"));
            break;
        case ToolDrawGroundLine:
            m_lineType = MapLineItem::LineType::Ground;
            m_view->setTool(MapView::Tool::AddLine);
            m_pointType = MapPointItem::PointType::Marker;
            statusBar()->showMessage(QStringLiteral("绘制地线：依次单击两个点"));
            break;
        case ToolGenerateRoad:
            m_lineType = ui->setRoadRule->currentData().value<MapLineItem::LineType>();
            m_view->setTool(MapView::Tool::AddLine);
            m_pointType = MapPointItem::PointType::Road;
            statusBar()->showMessage(QStringLiteral("绘制道路：依次单击两个点"));
            break;
        case ToolNewDesk:
            m_view->setTool(MapView::Tool::AddPoint);
            m_pointType = ui->deskTypeRule->currentData().value<MapPointItem::PointType>();
            statusBar()->showMessage(QStringLiteral("绘制桌位"));
            break;
        case ToolEditMove:
            m_view->setTool(MapView::Tool::Select);
            statusBar()->showMessage(QStringLiteral("编辑/移动：拖动图元，Delete 删除选中"));
            break;
        }
    });

    // 默认处于编辑/移动
    ui->editMoveButton->setChecked(true);
}
void MainWindow::setupTcpUi()
{
    m_robotMgr = new RobotManager(this);

    // 表格初始化
    ui->tcpConnectTable->setColumnCount(3);
    ui->tcpConnectTable->setHorizontalHeaderLabels(
        { QStringLiteral("机器人 IP"),
         QStringLiteral("状态"),
         QStringLiteral("备注") });
    ui->tcpConnectTable->horizontalHeader()->setStretchLastSection(true);
    ui->tcpConnectTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->tcpConnectTable->setEditTriggers(QAbstractItemView::NoEditTriggers);


    // Manager -> UI
    connect(m_robotMgr, &RobotManager::robotConnected,
            this, &MainWindow::onRobotConnected);
    connect(m_robotMgr, &RobotManager::robotDisconnected,
            this, &MainWindow::onRobotDisconnected);
    connect(m_robotMgr, &RobotManager::robotError,
            this, &MainWindow::onRobotError);
    connect(m_robotMgr, &RobotManager::robotStartupFailed,
            this, &MainWindow::onRobotStartupFailed);
    connect(m_robotMgr, &RobotManager::robotData,
            this, &MainWindow::onRobotData);
    connect(m_robotMgr, &RobotManager::robotHeartbeatTimeout,
            this, &MainWindow::onRobotHeartbeatTimeout);
}

namespace {
    enum class RoadChangeAction : int {
        Reverse,
        SetSingle,
        SetDouble,
        SetForbidden,
    };
}
 Q_DECLARE_METATYPE(RoadChangeAction)
void MainWindow::setupCombo()
{
    QComboBox *roadRuleCombo = ui->setRoadRule;

    roadRuleCombo->addItem("单行道 起点->终点", QVariant::fromValue(MapLineItem::LineType::single_road_forward));
    roadRuleCombo->addItem("单行道 终点->起点", QVariant::fromValue(MapLineItem::LineType::single_road_backward));
    roadRuleCombo->addItem("双向道 起点<->终点", QVariant::fromValue(MapLineItem::LineType::double_road));
    roadRuleCombo->addItem("禁行道", QVariant::fromValue(MapLineItem::LineType::disable_road));

    QComboBox *deskCombo = ui->deskTypeRule;
    deskCombo->addItem("货位", QVariant::fromValue(MapPointItem::PointType::Storage));
    deskCombo->addItem("临时货位", QVariant::fromValue(MapPointItem::PointType::TmpStorage));
    deskCombo->addItem("道路点", QVariant::fromValue(MapPointItem::PointType::Road));
    deskCombo->addItem("标记点", QVariant::fromValue(MapPointItem::PointType::Marker));
    deskCombo->addItem("机器人老家", QVariant::fromValue(MapPointItem::PointType::RobHome));
    deskCombo->addItem("机器人卸货点", QVariant::fromValue(MapPointItem::PointType::Unload));

    QComboBox *changeRoadRuleCombo = ui->changeRoadRule;
    changeRoadRuleCombo->addItem("反转道路",QVariant::fromValue(RoadChangeAction::Reverse));
    changeRoadRuleCombo->addItem("设置为单行道",QVariant::fromValue(RoadChangeAction::SetSingle));
    changeRoadRuleCombo->addItem("设置为双行道",QVariant::fromValue(RoadChangeAction::SetDouble));
    changeRoadRuleCombo->addItem("设置为禁行道",QVariant::fromValue(RoadChangeAction::SetForbidden));
}

QString MainWindow::takeDeskName()
{
    // deskId 框为 0/负 = 暂不分配：点先空着ID，等"快速分配ID"或"转换桌位"补
    const int deskId = static_cast<int>(ui->deskId->value());
    if (deskId <= 0)
        return QString();
    // 快速分配：取当前号后输入框自增，下一个货位直接可用下一个号
    ui->deskId->setValue(deskId + 1);
    return QString::number(deskId);
}

QString MainWindow::makePointName(int pointId)
{
    using PT = MapPointItem::PointType;
    switch (m_pointType) {
    case PT::Storage:
    case PT::TmpStorage:
    case PT::Unload:
        return takeDeskName();   // 桌位类：业务ID（可能为空 = 待分配）
    default:
        return QStringLiteral("点%1").arg(pointId);   // 道路点/标记点：内部名
    }
}

MapPointItem* MainWindow::addPoint(const QPointF &scenePos,MapLineItem* line)
{
    if (line) {
        // 线上加点：动了既有线的结构（原线终点被改 + 切出新线段）→ 快照式命令。
        // before/after 各拍一份全量，撤销 = 整场回到分割前
        const QByteArray before = MapIO::serializeScene(m_scene, QString::number(m_mapVersion));

        MapPointItem *point = new MapPointItem(this->m_scene->nextPointId(),m_pointType);
        point->setName(makePointName(point->id()));   // 桌位类=业务ID（可空），其余=点N
        point->setPos(scenePos);
        m_scene->addItem(point);
        MapPointItem *endPoint = line->endPoint();
        line->setEndPoint(point);
        MapLineItem *newline = MapLineItem::create(m_scene->nextLineId(), point, endPoint,
                                                   line->lineType());
        m_scene->addItem(newline);

        m_undoStack->push(new SnapshotCommand(
            before,
            MapIO::serializeScene(m_scene, QString::number(m_mapVersion)),
            [this](const QByteArray &s) { restoreSnapshot(s); },
            QStringLiteral("线上加点")));

        statusBar()->showMessage(QStringLiteral("已添加 %1，位置 (%2, %3)")
                                     .arg(point->name())
                                     .arg(scenePos.x(), 0, 'f', 1)
                                     .arg(scenePos.y(), 0, 'f', 1));
        return point;
    }

    // 纯创建 → 命令式：规格进命令，push() 自动执行一次 redo
    AddItemsCommand::PointSpec spec;
    spec.id = m_scene->nextPointId();
    spec.pos = scenePos;
    // 修复：类型跟当前工具走（桌位工具造 Storage、道路工具造 Road），
    // 之前硬编码 Marker 导致桌位/道路在空白处落的点类型全错
    spec.type = static_cast<int>(m_pointType);
    spec.name = makePointName(spec.id);   // 桌位类=业务ID（可空），其余=点N
    m_undoStack->push(new AddItemsCommand(m_scene, {spec}, {}, QStringLiteral("添加点")));

    statusBar()->showMessage(QStringLiteral("已添加 %1，位置 (%2, %3)")
                                 .arg(spec.name)
                                 .arg(scenePos.x(), 0, 'f', 1)
                                 .arg(scenePos.y(), 0, 'f', 1));
    // 指针一律按 ID 现解析，不穿过命令内部
    return m_scene->pointById(spec.id);
}

void MainWindow::movePoint(const QList<MapScene::MoveRecord> &m_pendingMoves)
{
    // 先包撤销逻辑
    QList<MoveItemCommand::PointPos> PointPos;
    for(auto rec : m_pendingMoves){
        PointPos.append(MoveItemCommand::PointPos(rec.id,rec.oldPos,rec.newPos));
    }
    m_undoStack->push(new MoveItemCommand(m_scene,PointPos,QStringLiteral("移动图元")));
}

QList<MapPointItem*> MainWindow::routeThrough(MapPointItem *start, const QPointF &endPos)
{
    // 纯查询（不动任何数据）：start→endPos 线段"精确穿过"的既有图点，按沿线方向排序。
    // 命令的规格构建和将来的穿越预览（drawForeground）共用本函数
    const QLineF seg(start->pos(), endPos);
    const qreal len = seg.length();
    if (len < 1e-9)
        return {};

    // "精确穿过"的判定容差（场景单位；点本体半径是4，100单位=1m → 2cm）。
    // 绝不能用 == 或叉积为零判断——浮点坐标永远不会精确相等，必须用距离容差
    constexpr qreal kCrossTol = 2.0;
    const QPointF dir = (seg.p2() - seg.p1()) / len;   // 单位方向，投影参数稳定

    struct Crossed { MapPointItem *item; qreal t; };    // t = 沿线距离（用于排序）
    QList<Crossed> crossed;

    for (QGraphicsItem *it : m_scene->items()) {
        auto *p = qgraphicsitem_cast<MapPointItem *>(it);
        if (!p || p == start)
            continue;
        const QPointF v = p->pos() - seg.p1();
        const qreal t = v.x() * dir.x() + v.y() * dir.y();
        // 贴着两端（或在线段延长线上）的不算"穿过"，跳过
        if (t < kCrossTol || t > len - kCrossTol)
            continue;
        if (MapMath::distanceToSegment(p->pos(), seg.p1(), seg.p2()) <= kCrossTol)
            crossed.append({p, t});
    }

    std::sort(crossed.begin(), crossed.end(),
              [](const Crossed &a, const Crossed &b) { return a.t < b.t; });

    QList<MapPointItem*> route;
    route.reserve(crossed.size());
    for (const Crossed &c : crossed)
        route.append(c.item);
    return route;
}

QList<MapLineItem*> MainWindow::connectPointsThrough(MapPointItem *start, MapPointItem *end,
                                                     MapLineItem::LineType type)
{
    // 施工壳：路线规划（routeThrough）+ 逐段建线。
    // 注意：交互路径已改走 AddItemsCommand（先规划成规格再 push），
    // 本函数仅供尚未接入 undo 的程序化调用（合并等）使用
    QList<MapLineItem*> created;
    MapPointItem *cur = start;
    for (MapPointItem *p : routeThrough(start, end->pos())) {
        MapLineItem *line = MapLineItem::create(m_scene->nextLineId(), cur, p, type);
        m_scene->addItem(line);
        created.append(line);
        cur = p;
    }
    MapLineItem *last = MapLineItem::create(m_scene->nextLineId(), cur, end, type);
    m_scene->addItem(last);
    created.append(last);
    return created;
}

MapLineItem* MainWindow::releasePointsOnLine(QList<MapPointItem *> &points, MapPointItem *startPoint, MapPointItem *endPoint, MapLineItem::LineType type)
{
    before_document_mutation();
    qDeleteAll(points);
    points.clear();
    // 合并产生的长线同样穿点分段：途中被穿过的无关点纳入为中间节点
    const QList<MapLineItem*> created = connectPointsThrough(startPoint, endPoint, type);
    return created.isEmpty() ? nullptr : created.last();
}

void MainWindow::addLine(MapPointItem *startPoint,const QPointF &scenePos, MapLineItem* hitline, MapPointItem* hitPoint){
    MapPointItem *point = hitPoint;
    // 空白处落点,终点先不建，和线打包成同一个命令
    const bool createEndFresh = !point && !hitline;

    if (!point && hitline)
        point = addPoint(scenePos, hitline);   // 线上加点：动结构 → 快照式范畴，暂不进栈

    // ---- 首击：确定链头 ----
    if(!startPoint){
        if (!point)
            point = addPoint(scenePos, nullptr);   // 空白处：链头本身是独立一步"添加点"
        startPoint = point;
        // 只标记"凭空创建"的链头；命中现有点开始连线则不标记（那是用户自己放的，不能回收）
        m_autoChainHead = hitPoint ? nullptr : point;
        m_view->startPreviewLineDrawing(startPoint);
        statusBar()->showMessage(QStringLiteral("连线中：单击终点完成，Esc 取消，再点起点同样取消"));
        return;
    }

    if (point == startPoint) {    // 再点链头 = 取消（createEndFresh 时 point 为空，不会误入）
        m_view->cancelLineDrawing();
        return;
    }

    // ---- 第二击：终点（可能新建）+ 穿点分段 → 打包成一个 AddItemsCommand ----
    QList<AddItemsCommand::PointSpec> pointSpecs;
    int endId;
    if (point) {
        endId = point->id();
    } else {
        AddItemsCommand::PointSpec spec;
        spec.id = m_scene->nextPointId();
        spec.pos = scenePos;
        // 修复：终点类型跟当前工具走（道路模式下空白处落的终点是 Road 点）
        spec.type = static_cast<int>(m_pointType);
        spec.name = QStringLiteral("点%1").arg(spec.id);
        pointSpecs.append(spec);
        endId = spec.id;
    }

    // 路线规划（纯查询）：start → 途经点… → 终点，逐对生成线规格
    //（每条线在规格构建时就分配好自己的 lineId——身份在规划期诞生，不在施工期）
    QList<AddItemsCommand::LineSpec> lineSpecs;
    int prevId = startPoint->id();
    auto appendSpec = [&](int endPtId) {
        AddItemsCommand::LineSpec s;
        s.lineId = m_scene->nextLineId();
        s.idA = prevId;
        s.idB = endPtId;
        s.type = static_cast<int>(m_lineType);
        lineSpecs.append(s);
        prevId = endPtId;
    };
    for (MapPointItem *p : routeThrough(startPoint, scenePos))
        appendSpec(p->id());
    appendSpec(endId);

    m_undoStack->push(new AddItemsCommand(m_scene, pointSpecs, lineSpecs, QStringLiteral("连接")));

    // 终点对象：命中现有点用现成的，新建的按 ID 现解析（链要继续从它拉皮筋）
    MapPointItem *endPoint = point ? point : m_scene->pointById(endId);
    QString viaMsg;
    if (lineSpecs.size() > 1)
        viaMsg = QStringLiteral("，途经 %1 个点").arg(lineSpecs.size() - 1);
    statusBar()->showMessage(
        QStringLiteral("已连接 %1 — %2，线长%3，实际距离为%4m%5")
            .arg(startPoint->name(), endPoint->name())
            .arg(QLineF(startPoint->pos(), endPoint->pos()).length())
            .arg(QLineF(startPoint->pos(), endPoint->pos()).length()/kPrecision,0,'f',4)
            .arg(viaMsg)
    );
    m_view->finishLineDrawing(endPoint);
    m_autoChainHead = nullptr;   // 新链头此刻已挂上刚建的线，取消时无需回收
}

void MainWindow::keyPressEvent(QKeyEvent *event)
{
    // Delete 删除选中图元。焦点在输入框时不拦截，否则打字删字符会误删地图
    if (event->key() == Qt::Key_Delete
        && !qobject_cast<QLineEdit *>(focusWidget())
        && !qobject_cast<QComboBox *>(focusWidget())) {
        deleteSelectedItems();
        return;
    }
    QMainWindow::keyPressEvent(event);
}

void MainWindow::deleteSelectedItems()
{
    // selectedItems 返回的是副本，删除过程中场景变化不影响本次遍历
    m_view->cancelLineDrawing();
    const QList<QGraphicsItem *> selected = m_scene->selectedItems();
    if (selected.isEmpty())
        return;

    // cancelLineDrawing 可能顺手删掉孤儿链头——那不属于本次"删除"的语义，所以 before 在门之后拍，把它排除在撤销范围外
    const QByteArray before = MapIO::serializeScene(m_scene, QString::number(m_mapVersion));

    int removed = 0;
    // 先删点：点的析构会级联删除相连的线
    for (QGraphicsItem *item : selected) {
        if (qgraphicsitem_cast<MapPointItem *>(item)) {
            delete item;
            ++removed;
        }
    }
    // 再删剩下的线（被级联删掉的线已经不在场景里，用 scene() 判活跳过）
    for (QGraphicsItem *item : selected) {
        if (item->scene() == m_scene) {
            delete item;
            ++removed;
        }
    }

    // 删除 = 动结构（级联）→ 快照式命令
    m_undoStack->push(new SnapshotCommand(
        before,
        MapIO::serializeScene(m_scene, QString::number(m_mapVersion)),
        [this](const QByteArray &s) { restoreSnapshot(s); },
        QStringLiteral("删除 %1 个图元").arg(removed)));

    statusBar()->showMessage(QStringLiteral("已删除 %1 个图元").arg(removed));
}

void MainWindow::restoreSnapshot(const QByteArray &state)
{
    // 修改门：恢复=整场换血，先结束一切交互引用（链头等）。
    // 注意：这里绝不能走 clearCanvas()——它会清空 undo 栈，而我们正身在栈的 undo/redo 里
    before_document_mutation();

    m_scene->clear();
    MapIO::restoreScene(m_scene, MapIO::parseCsvBytes(state));
    m_scene->resyncIdCounter();     // 铁规：快照恢复收尾必须对齐 ID 计数器
}

void MainWindow::clearCanvas()
{
    before_document_mutation();
    // 清空后旧命令失去语义，连命令一起清。
    // 命令不持有任何对象（undo 真删、redo 按规格重建），清理零风险
    m_undoStack->clear();
    m_scene->clear();
    m_scene->setNextPointId(1);
    m_scene->setNextLineId(1);
    statusBar()->showMessage(QStringLiteral("画布已清空"));
}

void MainWindow::on_deleteMap_triggered()
{
    before_document_mutation();
    clearCanvas();
}

void MainWindow::before_document_mutation()
{
    m_view->cancelLineDrawing();
    m_scene->clearMovePos();
}


void MainWindow::on_saveMap_triggered()
{
    QString fileName = QFileDialog::getSaveFileName(
        this,
        tr("另存为"),
        QDir::homePath() + "/untitled.rou",
        tr("地图文件 (*.rou);;CSV文件（*.csv）;;所有文件 (*.*)")
        );

    if (fileName.isEmpty())   // 用户取消
        return;

    // 如果用户没写后缀，手动补一个
    QFileInfo fi(fileName);
    if (fi.suffix().isEmpty())
        fileName += ".rou";

    QString err;
    if (!MapIO::saveInternal(fileName, m_scene, err, QString::number(mapVersionUpdate()))) {
        QMessageBox::critical(
            this,
            tr("保存失败"),
            err.isEmpty() ? tr("未知错误") : err
            );
        return;
    }

    QMessageBox::information(
        this,
        tr("保存成功"),
        tr("地图已保存到：\n%1").arg(fileName)
        );
}

void MainWindow::on_loadMap_triggered()
{
    // 0. 记录撤销信息 结束进行中的连线交互
    before_document_mutation();
    const QByteArray before = MapIO::serializeScene(m_scene, QString::number(m_mapVersion));
    clearCanvas();
    // 1. 清空视图开始逻辑
    QString fileName = QFileDialog::getOpenFileName(
        this,
        tr("打开地图"),
        QDir::homePath(),
        tr("地图文件 (*.rou);;CSV文件 (*.csv);;所有文件 (*.*)")
        );

    if (fileName.isEmpty())   // 用户取消
        return;


    bool ok = false;

    MapIO::LoadResult loadResult =MapIO::loadInternal(fileName, m_scene);

    if (!loadResult.ok) {
        QMessageBox::critical(
            this,
            tr("打开失败"),
            loadResult.error.isEmpty() ? tr("未知错误") : loadResult.error
            );
        return;
    }
    m_scene->resyncIdCounter();
    m_mapVersion = loadResult.mapVersion;
    QMessageBox::information(
        this,
        tr("打开成功"),
        tr("已加载：\n%1。").arg(fileName) + loadResult.info
        );

    // 2. 包全局快照方法
    m_undoStack->push(new SnapshotCommand(
        before,
        MapIO::serializeScene(m_scene, QString::number(m_mapVersion)),
        [this](const QByteArray &s) { restoreSnapshot(s); },
        QStringLiteral("加载地图")));

}

void MainWindow::on_splitLinebutton_clicked()
{
    // 0. 记录撤销信息
    const QByteArray before = MapIO::serializeScene(m_scene, QString::number(m_mapVersion));

    bool isCalibrate = ui->calibratecRadioButton->isChecked();
    const qreal splitLength = ui->splitSpinBox->value() * kPrecision;
    const qreal kCalTol = 0.2;   // 校准容差：±20%

    if (splitLength <= 0) {
        statusBar()->showMessage(QStringLiteral("分割单位不正确"));
        return;
    }

    QList<MapLineItem*> lines;
    for (QGraphicsItem *it : m_scene->selectedItems()) {
        if (auto *line = qgraphicsitem_cast<MapLineItem*>(it))
            lines.append(line);
    }

    // 1.端点挪动：先存目标位置，全部算完再统一 setPos
    QHash<MapPointItem*, QPointF> endpointMoves;
    int calibrated = 0;

    for (MapLineItem *line : lines) {
        MapPointItem *sp = line->startPoint();
        MapPointItem *ep = line->endPoint();
        const QPointF p0 = sp->pos();
        const QPointF p1 = ep->pos();

        MapPointItem *startPt = (p0.x() + p0.y() <= p1.x() + p1.y()) ? sp : ep;
        const bool lineFromStart = (startPt == sp);
        const QPointF start = startPt->pos();
        const QPointF end   = lineFromStart ? ep->pos() : sp->pos();
        const qreal totalLen = QLineF(start, end).length();

        // 太短：即使校准也要挪动超过20%，直接跳过
        if (totalLen < splitLength * (1.0 - kCalTol))
            continue;

        MapPointItem *movePt = lineFromStart ? ep : sp;  // 非锚点端点，可挪动
        const QPointF dir = (end - start) / totalLen;

        // 校准模式：round 段数 + 挪端点使总长为整数倍
        if (isCalibrate) {
            const int n = qMax(1, static_cast<int>(std::round(totalLen / splitLength)));
            const qreal targetLen = n * splitLength;
            const qreal deviation = totalLen - targetLen;  // 正=多了要缩短，负=少了要延长

            if (qAbs(deviation) <= splitLength * kCalTol) {
                // 偏差在容差内：记录端点目标位置（先不挪）
                const QPointF newEnd = start + dir * targetLen;
                if (!endpointMoves.contains(movePt))
                    endpointMoves.insert(movePt, newEnd);

                // 插入 n-1 个等距点（n=1 时不插入，只挪端点）
                QList<QPointF> positions;
                if (n > 1) {
                    positions.reserve(n - 1);
                    for (int i = 1; i <= n - 1; ++i)
                        positions.append(start + dir * (splitLength * i));
                }

                // 先插入点（此时端点还没挪，插入点在线段上）
                if (lineFromStart) {
                    for (int i = positions.size() - 1; i >= 0; --i)
                        addPoint(positions[i], line);
                } else {
                    for (const QPointF &p : positions)
                        addPoint(p, line);
                }
                calibrated++;
                continue;
            }
            // 偏差超过容差 → 落到下面走普通分割
        }

        // 普通模式（或校准但偏差过大）：floor 分割
        if (splitLength >= totalLen) continue;

        const int n = static_cast<int>((totalLen - 1e-9) / splitLength);
        if (n < 1) continue;

        QList<QPointF> positions;
        positions.reserve(n);
        for (int i = 1; i <= n; ++i)
            positions.append(start + dir * (splitLength * i));

        if (lineFromStart) {
            for (int i = positions.size() - 1; i >= 0; --i)
                addPoint(positions[i], line);
        } else {
            for (const QPointF &p : positions)
                addPoint(p, line);
        }
    }

    // 2.统一挪动端点
    for (auto it = endpointMoves.begin(); it != endpointMoves.end(); ++it)
        it.key()->setPos(it.value());

    if (isCalibrate)
        statusBar()->showMessage(QStringLiteral("分割校准完成：%1 条线已校准，挪动 %2 个端点")
                                     .arg(calibrated).arg(endpointMoves.size()));
    else
        statusBar()->showMessage(QStringLiteral("分割完成"));
    // 3.包撤销逻辑
    m_undoStack->push(new SnapshotCommand(
        before,
        MapIO::serializeScene(m_scene, QString::number(m_mapVersion)),
        [this](const QByteArray &s) { restoreSnapshot(s); },
        QStringLiteral("分割线逻辑完成")));
}

void MainWindow::on_orthogonalizationButton_clicked()
{
    const qreal pi = std::acos(-1.0);

    // 0. 预处理：选中线的端点自动加入选中
    // 处理用户框选不完全：选了线但端点没选上的情况
    // 记录撤销信息
    const QByteArray before = MapIO::serializeScene(m_scene, QString::number(m_mapVersion));

    QSet<MapLineItem*> userSelectedLines;
    const QList<QGraphicsItem*> initialSelected = m_scene->selectedItems();
    for (QGraphicsItem *it : initialSelected) {
        if (auto *line = qgraphicsitem_cast<MapLineItem*>(it))
            userSelectedLines.insert(line);
    }

    // 1. 收集选中的点
    QSet<MapPointItem*> selectedPoints;
    for (QGraphicsItem *it : m_scene->selectedItems()) {
        if (auto *p = qgraphicsitem_cast<MapPointItem*>(it))
            selectedPoints.insert(p);
    }
    for (MapLineItem *line : userSelectedLines) {
        selectedPoints.insert(line->startPoint());
        selectedPoints.insert(line->endPoint());
    }
    if (selectedPoints.size() < 2)
        return;

    // 2. 收集 workLines：两端点都在选中集合内的线
    QSet<MapLineItem*> workLineSet = userSelectedLines; // 用户直接选的线
    for (MapPointItem *p : selectedPoints) {
        for (MapLineItem *line : p->attachedLines()) {
            if (selectedPoints.contains(line->startPoint()) &&
                selectedPoints.contains(line->endPoint()))
                workLineSet.insert(line);
        }
    }
    QList<MapLineItem*> workLines (workLineSet.cbegin(), workLineSet.cend());
    if (workLines.isEmpty())
        return;

    // 3. 建子图邻接表（只包含选中范围内的线
    QHash<MapPointItem*, QList<MapLineItem*>> adj;
    for (MapLineItem *line : workLines) {
        adj[line->startPoint()].append(line);
        adj[line->endPoint()].append(line);
    }

    // 4. BFS 分组：按方向连续性切分直链
    // 规则如下：
    //   子图度数 >= 3 → 硬边界（交叉口），不扩散
    //   子图度数 == 2 且相邻线夹角 < 20° → 同一直链，吸收
    //   子图度数 == 2 但夹角大 → 拐点，切开
    //   子图度数 == 1 → 端点边界
    const qreal kCollinearTol = 20.0 * pi / 180.0;

    QSet<MapLineItem*> visited;
    QList<QList<MapLineItem*>> groups;

    for (MapLineItem *seed : workLines) {
        if (visited.contains(seed)) continue;

        QList<MapLineItem*> group;
        QQueue<MapLineItem*> queue;
        queue.enqueue(seed);
        visited.insert(seed);

        while (!queue.isEmpty()) {
            MapLineItem *cur = queue.dequeue();
            group.append(cur);

            for (MapPointItem *p : {cur->startPoint(), cur->endPoint()}) {
                int degree = adj[p].size();
                if (degree >= 3) continue;          // 交叉口硬边界
                if (degree == 2) {
                    for (MapLineItem *next : adj[p]) {
                        if (next == cur || visited.contains(next)) continue;
                        if (MapMath::angleBetween(cur, next, p) < kCollinearTol) {
                            visited.insert(next);
                            queue.enqueue(next);
                        }
                    }
                }
                // degree == 1 是端点，不扩散
            }
        }
        groups.append(group);
    }

    // 5. 计算目标位置（只读阶段，绝不移动点）==========
    // 分开存储 x / y / 完整位置，避免水平组与垂直组共享拐点时冲突：
    //   水平组只改 y，垂直组只改 x，斜线组内部点投影给完整位置
    const qreal kAxisTol = 10.0 * pi / 180.0;
    QHash<MapPointItem*, qreal>  targetX;   // 垂直组设置
    QHash<MapPointItem*, qreal>  targetY;   // 水平组设置
    QHash<MapPointItem*, QPointF> targetPos; // 斜线组内部点

    for (const auto &group : groups) {
        // 收集组内所有点 + 组内度数
        QSet<MapPointItem*> groupPoints;
        QHash<MapPointItem*, int> groupDegree;
        for (MapLineItem *line : group) {
            groupPoints.insert(line->startPoint());
            groupPoints.insert(line->endPoint());
            groupDegree[line->startPoint()]++;
            groupDegree[line->endPoint()]++;
        }
        if (groupPoints.size() < 2) continue;

        // 端点（组内度数==1）和内部点（组内度数>=2）
        QList<MapPointItem*> endpoints;
        QList<MapPointItem*> interiorPoints;
        for (MapPointItem *p : groupPoints) {
            if (groupDegree[p] == 1) endpoints.append(p);
            else                     interiorPoints.append(p);
        }
        if (endpoints.size() != 2) continue; // 环或异常，跳过

        QPointF a = endpoints[0]->pos();
        QPointF b = endpoints[1]->pos();
        QPointF dir = b - a;
        qreal len = std::hypot(dir.x(), dir.y());
        if (len < 1e-6) continue;

        // 判断水平 / 垂直
        qreal dirAng = std::atan2(dir.y(), dir.x());
        qreal ang = MapMath::normalizeAngleHalfPi(dirAng);
        bool isHorizontal = qMin(ang, pi - ang) < kAxisTol;
        bool isVertical   = std::abs(ang - pi / 2.0) < kAxisTol;

        if (isHorizontal) {
            // 水平：以最靠近 Y 轴（|x| 最小）的点的 y 为基准——靠轴的点通常落在
            // 网格整数位上，对齐后能直接配合校准分割
            MapPointItem *ref = nullptr;
            for (MapPointItem *p : groupPoints) {
                if (!ref || qAbs(p->pos().x()) < qAbs(ref->pos().x()))
                    ref = p;
            }
            const qreal refY = ref->pos().y();
            for (MapPointItem *p : groupPoints)
                if (!targetY.contains(p)) targetY[p] = refY;
        } else if (isVertical) {
            // 垂直：以最靠近 X 轴（|y| 最小）的点的 x 为基准
            MapPointItem *ref = nullptr;
            for (MapPointItem *p : groupPoints) {
                if (!ref || qAbs(p->pos().y()) < qAbs(ref->pos().y()))
                    ref = p;
            }
            const qreal refX = ref->pos().x();
            for (MapPointItem *p : groupPoints)
                if (!targetX.contains(p)) targetX[p] = refX;
        } else {
            // 斜线：端点不动，内部点投影到端点连线
            QPointF lineDir = dir / len;
            for (MapPointItem *p : interiorPoints) {
                if (targetPos.contains(p)) continue;
                QPointF v = p->pos() - a;
                qreal t = v.x() * lineDir.x() + v.y() * lineDir.y();
                targetPos[p] = a + lineDir * t;
            }
        }
    }

    // 6. 统一执行移动
    int moved = 0;
    for (MapPointItem *p : selectedPoints) {
        QPointF pos = p->pos();
        bool changed = false;
        if (targetX.contains(p))   { pos.setX(targetX[p]);   changed = true; }
        if (targetY.contains(p))   { pos.setY(targetY[p]);   changed = true; }
        if (targetPos.contains(p)) { pos = targetPos[p];      changed = true; }
        if (changed) { p->setPos(pos); moved++; }
    }

    statusBar()->showMessage(QStringLiteral("正交化完成：共 %1 组，调整 %2 个点")
                                 .arg(groups.size()).arg(moved));
    // 7. 包全局快照方法
    m_undoStack->push(new SnapshotCommand(
        before,
        MapIO::serializeScene(m_scene, QString::number(m_mapVersion)),
        [this](const QByteArray &s) { restoreSnapshot(s); },
        QStringLiteral("正交化")));
}


void MainWindow::on_orthogonalizationMergeButton_clicked()
{
    // 0. 记录撤销信息
    const QByteArray before = MapIO::serializeScene(m_scene, QString::number(m_mapVersion));
    // 1. 收集选中的点线计算linetype
    QList<MapPointItem*> points;
    QSet<MapPointItem*> pointsSet;
    QList<MapLineItem*> lines;
    QList<MapLineItem*> selectLines;
    for (QGraphicsItem *it : m_scene->selectedItems()) {
        if (auto *point = qgraphicsitem_cast<MapPointItem*>(it))
            points.append(point);
        if (auto *line = qgraphicsitem_cast<MapLineItem*>(it))
            selectLines.append(line);
    }
    // 我们必须考虑到用户可能会多选到线，所以我们要筛掉所有没有两端都注册选中点的线。
    pointsSet = QSet<MapPointItem*>(points.begin(),points.end());
    for (auto selectline : selectLines){
        if(pointsSet.contains(selectline->startPoint()) && pointsSet.contains(selectline->endPoint()))
            lines.append(selectline);
    }
    if(lines.size()==0 || points.size()==0){
        statusBar()->showMessage(QStringLiteral("请框选需要合并的线段以及它全部的控制点"));
        return;
    }
    MapLineItem::LineType lineType = lines[0]->lineType();

    for(auto line : lines){
        if (lineType != line->lineType()){
            statusBar()->showMessage(QStringLiteral("选中线类型不一致！无法合并"));
            return;
        }
    }

    // 少于 3 个点没有“中间点”可删，直接返回
    if (points.size() < 3)
        return;

    QList<QPointF> pts;
    pts.reserve(points.size());
    for (const MapPointItem *p : points)
        pts.append(p->pos());

    // 2. TLS 拟合，得到质心和主轴方向
    QPointF center, dir;
    MapMath::fitLineTLS(pts, center, dir);

    // 3. 找首尾点：沿主轴方向投影参数最小 / 最大
    qreal tMin =  std::numeric_limits<qreal>::max();
    qreal tMax = -std::numeric_limits<qreal>::max();
    int idxStart = -1, idxEnd = -1;

    for (int i = 0; i < pts.size(); ++i) {
        const QPointF v = pts[i] - center;
        const qreal t = v.x() * dir.x() + v.y() * dir.y();
        if (t < tMin) { tMin = t; idxStart = i; }
        if (t > tMax) { tMax = t; idxEnd   = i; }
    }
    if (idxStart < 0 || idxEnd < 0 || idxStart == idxEnd)
        return;

    const qreal lineLen = tMax - tMin;
    if (lineLen < 1e-6)
        return;                           // 所有点重合

    // 4. 严格共线判定
    //    用首尾连线作为参考线，检查所有中间点到它的垂直距离。
    //    阈值 = 线段长度的 0.5%，且不小于一个极小绝对值兜底。
    const QPointF &pA = pts[idxStart];
    const QPointF &pB = pts[idxEnd];

    const qreal kStrictTol = qMax(lineLen * 0.005, 0.5);

    MapPointItem *startPoint = points[idxStart];
    MapPointItem *endPoint   = points[idxEnd];

    for (int i = 0; i < pts.size(); ++i) {
        if (points[i] == startPoint || points[i] == endPoint)
            continue;
        // 到 pA-pB 所在直线的垂直距离
        const qreal d = MapMath::distanceToSegment(pts[i], pA, pB);
        if (d > kStrictTol){
            statusBar()->showMessage(QStringLiteral("存在偏离直线的点，无法合并"));
            return;
        }
    }

    // 5. 通过严格判定，收集要删的中间点

    QList<MapPointItem*> toRemove;
    toRemove.reserve(points.size() - 2);
    for (auto* p : points){
        if (p == startPoint || p == endPoint)
            continue;
        toRemove.append(p);
    }

    // 6. 调用既有 API
    releasePointsOnLine(toRemove, startPoint, endPoint,lineType);
    statusBar()->showMessage(QStringLiteral("合并线段完成"));

    // 7. 包全局快照方法
    m_undoStack->push(new SnapshotCommand(
        before,
        MapIO::serializeScene(m_scene, QString::number(m_mapVersion)),
        [this](const QByteArray &s) { restoreSnapshot(s); },
        QStringLiteral("删除正交化多余点")));
}


void MainWindow::on_undo_button_triggered()
{
    // 先走修改门
    before_document_mutation();
    m_undoStack->undo();
}

void MainWindow::on_redo_button_triggered()
{
    before_document_mutation();
    m_undoStack->redo();
}


void MainWindow::on_editRoadButton_clicked()
{
    // 统一先包全局快照 然后慢慢优化
    const QByteArray before = MapIO::serializeScene(m_scene, QString::number(m_mapVersion));

    bool haveRoad = false;
    const auto action = ui->changeRoadRule->currentData().value<RoadChangeAction>();
    for (QGraphicsItem *it : m_scene->selectedItems()) {
        // 所有线类型共用同一个 type()，qgraphicsitem_cast 无法区分，只能用 RTTI
        if (auto *road = dynamic_cast<MapRoadItem*>(it)){
            haveRoad = true;
            // 方法有限不搞策略模式了没必要
            switch(action){
            case RoadChangeAction::Reverse:
                road->reverseRoad();
                break;
            case RoadChangeAction::SetSingle:
                road->setLineType(MapLineItem::LineType::single_road_forward);
                break;
            case RoadChangeAction::SetDouble:
                road->setLineType(MapLineItem::LineType::double_road);
                break;
            case RoadChangeAction::SetForbidden:
                road->setLineType(MapLineItem::LineType::disable_road);
                break;
            default:
                Q_UNREACHABLE();
                break;
            }
        }
    }
    if(!haveRoad){
        statusBar()->showMessage(QStringLiteral("未选中道路！"));
        return;   // 没有实际改动：不 push，避免一个"空撤销步"污染栈
    }
    statusBar()->showMessage(QStringLiteral("道路修改完成"));

    m_undoStack->push(new SnapshotCommand(
        before,
        MapIO::serializeScene(m_scene, QString::number(m_mapVersion)),
        [this](const QByteArray &s) { restoreSnapshot(s); },
        QStringLiteral("道路修改")));
}

void MainWindow::on_changeDeskButton_clicked()
{
    // 转换桌位：单点操作——多选时容易误伤，只允许选中一个
    MapPointItem *target = nullptr;
    int pointCount = 0;
    for (QGraphicsItem *it : m_scene->selectedItems()) {
        if (auto *p = qgraphicsitem_cast<MapPointItem *>(it)) {
            target = p;
            ++pointCount;
        }
    }
    if (pointCount == 0) {
        statusBar()->showMessage(QStringLiteral("未选中桌位点"));
        return;
    }
    if (pointCount > 1) {
        statusBar()->showMessage(QStringLiteral("转换桌位一次只能选中一个点"));
        return;
    }
    // 目标类型取"桌位类型"下拉框（与新建桌位共用选择器）
    const auto newType = ui->deskTypeRule->currentData().value<MapPointItem::PointType>();
    if (newType == target->pointType()) {
        statusBar()->showMessage(QStringLiteral("该点已是此类型"));
        return;
    }

    // 类型变了装饰要换（方块/标签/包围盒随类型）→ 快照式命令
    const QByteArray before = MapIO::serializeScene(m_scene, QString::number(m_mapVersion));
    target->setPointType(newType);

    // ID 修改：deskId 框 > 0 时一并设为框里的值（取号自增，支持连续转换补号）；
    // 框为 0 = 只改类型，ID 保持原样（含空ID——留给"快速分配ID"）
    bool idSet = false;
    if (const int deskId = static_cast<int>(ui->deskId->value()); deskId > 0) {
        target->setName(QString::number(deskId));
        ui->deskId->setValue(deskId + 1);
        idSet = true;
    }

    m_undoStack->push(new SnapshotCommand(
        before,
        MapIO::serializeScene(m_scene, QString::number(m_mapVersion)),
        [this](const QByteArray &s) { restoreSnapshot(s); },
        QStringLiteral("转换桌位")));
    statusBar()->showMessage(idSet
        ? QStringLiteral("转换完成，ID 已设为 %1").arg(target->name())
        : QStringLiteral("转换完成（ID 未变，deskId 框为 0）"));
}

void MainWindow::on_quickDeckIdButton_clicked()
{
    using PT = MapPointItem::PointType;

    // 1. 收集所有"空ID"的非道路/非Marker点
    QList<MapPointItem *> targets;
    for (QGraphicsItem *it : m_scene->items()) {
        auto *p = qgraphicsitem_cast<MapPointItem *>(it);
        if (!p || !p->name().isEmpty())
            continue;
        const PT t = p->pointType();
        if (t == PT::Road || t == PT::Marker)
            continue;
        targets.append(p);
    }
    if (targets.isEmpty()) {
        statusBar()->showMessage(QStringLiteral("没有待分配ID的桌位点"));
        return;
    }

    // 2. 阅读顺序排序：y 量化成行（100 单位一格，吸收网格抖动），行内按 x。
    //    量化比较保证排序的传递性——直接用 |dy|<容差 的比较器不是合法弱序
    std::sort(targets.begin(), targets.end(),
              [](const MapPointItem *a, const MapPointItem *b) {
        const qint64 rowA = qRound64(a->y() / 100.0);
        const qint64 rowB = qRound64(b->y() / 100.0);
        if (rowA != rowB)
            return rowA < rowB;   // 行间：从上到下
        return a->x() < b->x();   // 行内：从左到右
    });

    const QByteArray before = MapIO::serializeScene(m_scene, QString::number(m_mapVersion));

    // 3. 各类型独立流水号：首位 = 枚举值（10001/20001/30001/40001/50001 系列），
    //    从该类型现有 5 位号的最大序号续发，避免与手输ID撞车
    QHash<int, int> nextSeq;
    for (QGraphicsItem *it : m_scene->items()) {
        auto *p = qgraphicsitem_cast<MapPointItem *>(it);
        if (!p)
            continue;
        const QString name = p->name();
        if (name.size() != 5)
            continue;
        bool ok = false;
        const int num = name.toInt(&ok);
        if (!ok || num < 10000 || num > 59999)
            continue;   // 只认本方案格式的号，其他命名一律不碰
        const int typeDigit = num / 10000;
        nextSeq[typeDigit] = qMax(nextSeq.value(typeDigit, 1), num % 10000 + 1);
    }

    for (MapPointItem *p : std::as_const(targets)) {
        const int typeDigit = static_cast<int>(p->pointType());
        p->setName(QString::number(typeDigit * 10000 + nextSeq.value(typeDigit, 1)));
        nextSeq[typeDigit] = nextSeq.value(typeDigit, 1) + 1;
    }

    m_undoStack->push(new SnapshotCommand(
        before,
        MapIO::serializeScene(m_scene, QString::number(m_mapVersion)),
        [this](const QByteArray &s) { restoreSnapshot(s); },
        QStringLiteral("快速分配ID")));
    statusBar()->showMessage(
        QStringLiteral("已按阅读顺序为 %1 个点分配ID").arg(targets.size()));
}


void MainWindow::on_connectRobButton_clicked()
{
    const QString ip = uiRobotIp();
    if (ip.isEmpty()) {
        QMessageBox::warning(this, tr("提示"), tr("请输入机器人 IP"));
        return;
    }

    QHostAddress addr;
    if (!addr.setAddress(ip)) {
        QMessageBox::warning(this, tr("提示"), tr("机器人 IP 格式不正确"));
        return;
    }

    const quint16 port = uiRobotPort() > 0
                             ? uiRobotPort()
                             : AppConfig::instance().robotPort();

    ensureTableRow(ip);
    setRowStatus(ip, tr("连接中..."));

    m_robotMgr->connectRobot(ip, port);
}

QString MainWindow::uiRobotIp() const
{
    return ui->robId->text().trimmed();
}

quint16 MainWindow::uiRobotPort() const
{
    // 你的 UI 里暂时没有端口输入框，用配置默认值
    return 0;
}

void MainWindow::on_uploadRouteButton_clicked()
{
    const QString ip = uiRobotIp();
    if (ip.isEmpty() || !m_robotMgr->isKnown(ip)) {
        QMessageBox::warning(this, tr("提示"), tr("请先连接该机器人"));
        return;
    }

    // TODO(路线业务)：路线 = 位置ID链，等路线编辑功能做完后序列化真正的路线
    QByteArray routeData = QByteArrayLiteral("{\"cmd\":\"upload_route\"}");

    m_robotMgr->sendTo(ip, routeData);
    setRowNote(ip, tr("已发送路线 (%1 字节)").arg(routeData.size()));
}

void MainWindow::onRobotConnected(const QString &ip)
{
    setRowStatus(ip, tr("已连接"));
    setRowNote(ip, QString());
}

void MainWindow::onRobotDisconnected(const QString &ip)
{
    setRowStatus(ip, tr("断开(重连中)"));
}

void MainWindow::onRobotError(const QString &ip, const QString &msg)
{
    setRowNote(ip, msg);
}

void MainWindow::onRobotStartupFailed(const QString &ip, const QString &reason)
{
    setRowStatus(ip, tr("启动失败"));
    setRowNote(ip, reason);
}

void MainWindow::onRobotData(const QString &ip, const QByteArray &payload)
{
    qDebug() << "[MainWindow] 收到" << ip << "数据:" << payload;

    // 按机器人协议解析（JSON）。PONG 由 worker 心跳内部消化，不会到这里
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(payload, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        setRowNote(ip, tr("收到非JSON数据 %1 字节").arg(payload.size()));
        return;
    }
    const QJsonObject obj = doc.object();
    const QString type = obj.value(QStringLiteral("type")).toString();

    if (type == QStringLiteral("hello")) {
        // 机器人上线首包
        setRowStatus(ip, tr("已连接"));
        setRowNote(ip, tr("机器人 %1 已就绪")
                       .arg(obj.value(QStringLiteral("robot_id")).toString()));
    } else if (type == QStringLiteral("ack")) {
        if (obj.contains(QStringLiteral("size"))) {
            const int size = obj.value(QStringLiteral("size")).toInt();
            // 地图上传的回执：字节数与最近一次上传对上 → 明确报"地图已确认"
            if (m_lastMapUploadBytes > 0 && size == m_lastMapUploadBytes) {
                setRowNote(ip, tr("✔ 地图已确认收到（%1 字节）").arg(size));
                m_lastMapUploadBytes = -1;   // 消费掉，避免同尺寸包误报
            } else {
                setRowNote(ip, tr("机器人确认收到 %1 字节").arg(size));
            }
        } else {
            setRowNote(ip, tr("机器人确认: %1")
                           .arg(obj.value(QStringLiteral("cmd")).toString()));
        }
    } else {
        setRowNote(ip, tr("收到 %1 字节").arg(payload.size()));
    }
}

void MainWindow::onRobotHeartbeatTimeout(const QString &ip)
{
    setRowStatus(ip, tr("心跳超时"));
}

// ------------------------------------------------------------------
// 表格辅助
// ------------------------------------------------------------------
int MainWindow::tableRowOfRobot(const QString &ip) const
{
    for (int r = 0; r < ui->tcpConnectTable->rowCount(); ++r) {
        auto *it = ui->tcpConnectTable->item(r, 0);
        if (it && it->text() == ip) return r;
    }
    return -1;
}

void MainWindow::ensureTableRow(const QString &ip)
{
    if (tableRowOfRobot(ip) >= 0) return;

    const int row = ui->tcpConnectTable->rowCount();
    ui->tcpConnectTable->insertRow(row);
    ui->tcpConnectTable->setItem(row, 0, new QTableWidgetItem(ip));
    ui->tcpConnectTable->setItem(row, 1, new QTableWidgetItem(tr("未连接")));
    ui->tcpConnectTable->setItem(row, 2, new QTableWidgetItem(QString()));
}

void MainWindow::setRowStatus(const QString &ip, const QString &status)
{
    ensureTableRow(ip);
    const int row = tableRowOfRobot(ip);
    if (row < 0) return;
    if (!ui->tcpConnectTable->item(row, 1)) {
        ui->tcpConnectTable->setItem(row, 1, new QTableWidgetItem);
    }
    ui->tcpConnectTable->item(row, 1)->setText(status);
}

void MainWindow::setRowNote(const QString &ip, const QString &note)
{
    ensureTableRow(ip);
    const int row = tableRowOfRobot(ip);
    if (row < 0) return;
    if (!ui->tcpConnectTable->item(row, 2)) {
        ui->tcpConnectTable->setItem(row, 2, new QTableWidgetItem);
    }
    ui->tcpConnectTable->item(row, 2)->setText(note);
}

void MainWindow::on_uploadMapButton_clicked()
{
    const QString ip = uiRobotIp();
    if (ip.isEmpty() || !m_robotMgr->isKnown(ip)) {
        QMessageBox::warning(this, tr("提示"), tr("请先连接该机器人"));
        return;
    }

    // 真地图：复用序列化管线（GBK 字节，与 .rou 文件同一格式），
    // Python 假机器人解析同一格式即可。长度前缀封帧由 RobotWorker 负责
    QByteArray mapData = MapIO::serializeScene(m_scene, QString::number(m_mapVersion));
    m_lastMapUploadBytes = mapData.size();   // ack 回执按字节数对账

    m_robotMgr->sendTo(ip, mapData);
    setRowNote(ip, tr("已发送地图 (%1 字节)").arg(mapData.size()));
}


void MainWindow::on_connectivityTestButton_clicked()
{
    const QString ip = uiRobotIp();
    if (ip.isEmpty() || !m_robotMgr->isKnown(ip)) {
        QMessageBox::warning(this, tr("提示"), tr("请先连接该机器人"));
        return;
    }

    QByteArray probe = QByteArrayLiteral("{\"cmd\":\"ping\"}");
    m_robotMgr->sendTo(ip, probe);
    setRowNote(ip, tr("已发送连通性探测"));
}

