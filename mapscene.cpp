#include "mapscene.h"
#include <QGraphicsItem>
#include "mappointitem.h"
#include "maplineitem.h"

MapScene::MapScene(QObject *parent): QGraphicsScene(parent) {}

// ---------------- 点身份 ----------------

int MapScene::nextPointId(){
    return m_nextPointId++;
}

MapPointItem *MapScene::pointById(int id) const{
    for (QGraphicsItem *it : items()) {
        if (auto *p = qgraphicsitem_cast<MapPointItem *>(it))
            if (p->id() == id)
                return p;
    }
    return nullptr;
}

int MapScene::maxPointId() const
{
    int max_id = 0;
    for (QGraphicsItem *it : items()) {
        if (auto *p = qgraphicsitem_cast<MapPointItem *>(it))
            if (p->id() > max_id)
                max_id=p->id();
    }
    return max_id;
}

void MapScene::setNextPointId(int id){
    m_nextPointId = id;
}

// ---------------- 线身份 ----------------

int MapScene::nextLineId(){
    return m_nextLineId++;
}

MapLineItem *MapScene::lineById(int id) const{
    // 线性扫描：只返回"此刻真实存在于场景里"的线——藏起来的/删掉的查不到，
    // 这恰好是命令执行时想要的语义（栈序保证被引用者必然在场）
    for (QGraphicsItem *it : items()) {
        if (auto *l = qgraphicsitem_cast<MapLineItem *>(it))
            if (l->id() == id)
                return l;
    }
    return nullptr;
}

int MapScene::maxLineId() const
{
    int max_id = 0;
    for (QGraphicsItem *it : items()) {
        if (auto *l = qgraphicsitem_cast<MapLineItem *>(it))
            if (l->id() > max_id)
                max_id = l->id();
    }
    return max_id;
}

void MapScene::setNextLineId(int id){
    m_nextLineId = id;
}

void MapScene::resyncIdCounter()
{
    // 两套计数器一起对齐。铁规：快照/加载恢复的收尾必须调它，否则续号撞车
    m_nextPointId = maxPointId() + 1;
    m_nextLineId  = maxLineId() + 1;
}

void MapScene::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
    QGraphicsScene::mousePressEvent(event);      // 选中、抓取

    if (event->button() != Qt::LeftButton) return;
    if (!itemAt(event->scenePos(), QTransform())) return;   // 空白处按下=框选，不是拖拽

    m_pendingMoves.clear();
    for (QGraphicsItem *it : selectedItems()) {
        if (auto *p = qgraphicsitem_cast<MapPointItem*>(it))
            m_pendingMoves.append(MoveRecord(p->id(), p, p->pos(), p->pos()));
    }
}

void MapScene::mouseReleaseEvent(QGraphicsSceneMouseEvent *event)
{
    QGraphicsScene::mouseReleaseEvent(event);

    if (m_pendingMoves.isEmpty()) return;

    QList<MoveRecord> moved;
    moved.reserve(m_pendingMoves.size());

    for (const auto& rec : std::as_const(m_pendingMoves)) {
        if (rec.item && rec.item->pos() != rec.oldPos) {
            moved.append(MoveRecord(rec.id, nullptr, rec.oldPos, rec.item->pos()));
        }
    }
    m_pendingMoves.clear();
    if (!moved.isEmpty())
        emit itemMoveSignal(moved);
}

void MapScene::clearMovePos(){
    m_pendingMoves.clear();
}
