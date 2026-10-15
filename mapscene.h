#ifndef MAPSCENE_H
#define MAPSCENE_H

#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>

class MapPointItem;
class MapLineItem;

class MapScene : public QGraphicsScene {
    Q_OBJECT
public:
    explicit MapScene(QObject *parent = nullptr);

    // ---- 点身份 ----
    int  nextPointId();                       // 分配并递增
    MapPointItem *pointById(int id) const;    // 命令式 undo 的 ID 解析入口
    int  maxPointId() const;                  // 快照恢复后续号用
    void setNextPointId(int id);              // 预留id后门

    // ---- 线身份（与点 ID 同级的第二套身份系统）----
    int  nextLineId();                        // 分配并递增
    MapLineItem *lineById(int id) const;      // 命令规格/差分快照的线解析入口
    int  maxLineId() const;
    void setNextLineId(int id);

    // 两套计数器对齐到当前场景现实（加载/快照恢复后必须调用)
    void resyncIdCounter();

    void mousePressEvent(QGraphicsSceneMouseEvent *event);

    void mouseReleaseEvent(QGraphicsSceneMouseEvent *event);

    struct MoveRecord {
        int id;
        MapPointItem* item;
        QPointF oldPos;
        QPointF newPos;
    };


    void clearMovePos();

signals:
    void itemMoveSignal(const QList<MoveRecord>& m_pendingMoves);

private:
    int m_nextPointId = 1;
    int m_nextLineId = 1;
    QList<MoveRecord> m_pendingMoves; // 记录移动点移动起始位置与结束位置
};

#endif // MAPSCENE_H
