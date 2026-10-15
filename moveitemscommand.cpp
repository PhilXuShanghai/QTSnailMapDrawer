#include "moveitemscommand.h"
#include "mapscene.h"
#include "mappointitem.h"
#include "maplineitem.h"

#include <QGraphicsItem>

MoveItemCommand::MoveItemCommand(MapScene *scene, QList<PointPos> posInfo,const QString &label)
    : m_scene(scene), m_pointSpecs(posInfo)
{
    setText(label);
}

void MoveItemCommand::redo()
{
    // 执行修改 = 挪到"之后"的位置
    for (const PointPos &s : std::as_const(m_pointSpecs)) {
        MapPointItem *p = m_scene->pointById(s.id);
        if (!p)
            continue;   // 防御：栈序保证下不该发生。undo/redo 深处抛异常没人接，跳过优于崩溃
        p->setPos(s.after);
    }
}

void MoveItemCommand::undo()
{
    // 撤销 = 回到"之前"的位置
    for (const PointPos &s : std::as_const(m_pointSpecs)){
        MapPointItem *p = m_scene->pointById(s.id);
        if (!p)
            continue;
        p->setPos(s.before);
    }
}