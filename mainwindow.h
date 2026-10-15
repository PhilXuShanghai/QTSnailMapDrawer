#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <RobotManager.h>

#include "mappointitem.h"
#include "maplineitem.h"
#include "mapscene.h"

QT_BEGIN_NAMESPACE

class MapView;
class QButtonGroup;
class QUndoStack;

namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class QGraphicsScene;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    // 地图编辑器原则上每100单位距离为1m
    static constexpr qreal kPrecision  = 100.0;
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;
    int mapVersion() const { return m_mapVersion; }
    int mapVersionUpdate();

protected:
    void keyPressEvent(QKeyEvent *event) override;

private slots:
    void on_deleteMap_triggered();

    void before_document_mutation();

    void on_saveMap_triggered();

    void on_loadMap_triggered();

    void on_splitLinebutton_clicked();

    void on_orthogonalizationButton_clicked();

    void on_orthogonalizationMergeButton_clicked();

    void on_undo_button_triggered();

    void on_redo_button_triggered();

    void movePoint(const QList<MapScene::MoveRecord>& m_pendingMoves);

    void on_editRoadButton_clicked();

    void on_changeDeskButton_clicked();

    void on_quickDeckIdButton_clicked();

    void on_uploadRouteButton_clicked();

    void onRobotConnected(const QString &ip);

    void onRobotDisconnected(const QString &ip);

    void onRobotError(const QString &ip, const QString &msg);

    void onRobotStartupFailed(const QString &ip, const QString &reason);

    void onRobotData(const QString &ip, const QByteArray &payload);

    void onRobotHeartbeatTimeout(const QString &ip);

    void on_connectRobButton_clicked();

    void on_connectivityTestButton_clicked();

    void on_uploadMapButton_clicked();

private:
    // 工具按钮组初始化（绘制点/墙线/地线/编辑移动）
    void setupTools();
    // 初始化下拉框
    void setupCombo();
    // 在场景坐标 scenePos 处放置一个点
    MapPointItem* addPoint(const QPointF &scenePos,MapLineItem* line = nullptr);
    // 纯查询：start→endPos 线段"精确穿过"（容差内）的既有图点，按沿线方向排序。
    // 不动任何数据；undo 的规格构建和将来的穿越预览共用它

    QList<MapPointItem*> routeThrough(MapPointItem *start, const QPointF &endPos);
    // 连接两点；若线段"精确穿过"途中的既有图点（容差内），以它们为中间节点分段连接。
    // 返回实际创建的所有线
    QList<MapLineItem*> connectPointsThrough(MapPointItem *start, MapPointItem *end,
                                             MapLineItem::LineType type);
    // 删掉线上全部的多余的点
    MapLineItem* releasePointsOnLine(QList<MapPointItem *> &points, MapPointItem* startPoint, MapPointItem* endPoint,MapLineItem::LineType type);

    // 连接两个已存在的点
    void addLine(MapPointItem *startPoint,const QPointF &scenePos, MapLineItem* hitline= nullptr,  MapPointItem* hitPoint = nullptr);
    // 删除当前选中的图元（删点会级联删除相连的线）
    void deleteSelectedItems();
    // 点命名统一入口：桌位类 → 业务ID（存 name 字段，.rou 编号列语义，
    // deskId 框为 0 = 空ID待分配，等"快速分配ID"或"转换桌位"补）；
    // 非桌位类 → 内部名"点N"
    QString makePointName(int pointId);
    // 桌位类取号：deskId 输入框取值并自增；0/负 = 不分配（返回空串）
    QString takeDeskName();
    // 把一份快照字节恢复进场景（SnapshotCommand 的 restore 入口）。
    // 注意：绝不能调 clearCanvas——它会清 undo 栈，而我们正身在栈的 undo/redo 里
    void restoreSnapshot(const QByteArray &state);
    // 清空画布并重置点计数（调试用，后续会被"新建/打开"取代）
    void clearCanvas();

    Ui::MainWindow *ui;
    MapScene *m_scene = nullptr;
    MapView *m_view = nullptr;
    QButtonGroup *m_toolButtons = nullptr;
    QUndoStack *m_undoStack = nullptr;   // 混合式 undo：纯创建命令 + 快照命令同栈共存
    // 当前连线工具画的是哪种线（由哪个按钮进入决定）
    MapLineItem::LineType m_lineType = MapLineItem::LineType::Wall;
    // 当前连线工具画的是哪种点
    MapPointItem::PointType m_pointType = MapPointItem::PointType::Marker;
    // 连线首击空白处时"自动创建"的链头。取消连线时若它一条线都没挂上则回收
    // 后续链头不可能无线（成为链头的瞬间就连上了线）
    MapPointItem *m_autoChainHead = nullptr;
    int m_mapVersion = 0;

    RobotManager *m_robotMgr = nullptr;
    // 最近一次上传地图的字节数：机器人 ack 的 size 与它对上 → 显示"地图已确认"
    int m_lastMapUploadBytes = -1;

    void setupTcpUi();
    int  tableRowOfRobot(const QString &ip) const;
    void ensureTableRow(const QString &ip);
    void setRowStatus(const QString &ip, const QString &status);
    void setRowNote(const QString &ip, const QString &note);

    QString uiRobotIp() const;
    quint16 uiRobotPort() const;
};
#endif // MAINWINDOW_H
