#include "addItemscommand.h"

#include "mapscene.h"
#include "mappointitem.h"
#include "maplineitem.h"

#include <QGraphicsItem>

AddItemsCommand::AddItemsCommand(MapScene *scene, QList<PointSpec> points,
                                 QList<LineSpec> lines, const QString &label)
    : m_scene(scene)
    , m_pointSpecs(std::move(points))
    , m_lineSpecs(std::move(lines))
{
    setText(label);   // 菜单显示"撤销: 添加点"之类
}

void AddItemsCommand::redo()
{
    // 局部索引：本命令本次建的点，供后面的线 O(1) 找端点——
    // 这就是"重建期临时索引"模式：随 redo 生、随 redo 灭，无同步负担
    QHash<int, MapPointItem *> local;

    for (const PointSpec &s : std::as_const(m_pointSpecs)) {
        MapPointItem *p = m_scene->pointById(s.id);
        if (!p) {
            // 幂等：场里没有才建，且【复用规格里的 id】——绝不在 redo 里重新分配
            // （"ID 永不复用"铁规的反面：同一命令 redo 一百次，点 id 不变）
            p = new MapPointItem(s.id, static_cast<MapPointItem::PointType>(s.type));
            p->setName(s.name);
            p->setPos(s.pos);
            m_scene->addItem(p);
        }
        local.insert(s.id, p);
    }

    for (const LineSpec &s : std::as_const(m_lineSpecs)) {
        // 幂等：按线自己的 id 查在场性——孪生线（同端点同类型）也能精确区分
        if (m_scene->lineById(s.lineId))
            continue;

        MapPointItem *a = local.value(s.idA, nullptr);
        if (!a) a = m_scene->pointById(s.idA);
        MapPointItem *b = local.value(s.idB, nullptr);
        if (!b) b = m_scene->pointById(s.idB);
        if (!a || !b)
            continue;   // 防御：端点不在场（栈序保证下不该发生），跳过优于崩溃

        // 工厂按类型选派生类（墙/地/道路），lineId 复用规格里的
        m_scene->addItem(MapLineItem::create(s.lineId, a, b,
                                             static_cast<MapLineItem::LineType>(s.type)));
    }
}

void AddItemsCommand::undo()
{
    // 先删线再删点：点删除会级联删掉仍挂在它身上的线（与 Delete 键同语义）
    for (const LineSpec &s : std::as_const(m_lineSpecs))
        if (MapLineItem *line = m_scene->lineById(s.lineId))
            delete line;

    for (const PointSpec &s : std::as_const(m_pointSpecs))
        if (MapPointItem *p = m_scene->pointById(s.id))
            delete p;
}
