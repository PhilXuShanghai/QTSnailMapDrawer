#ifndef MOVEITEMCOMMAND_H
#define MOVEITEMCOMMAND_H

#include <QUndoCommand>
#include <QHash>
#include <QList>
#include <QPointF>
#include <QString>

class MapScene;
class MapPointItem;
class MoveItemCommand : public QUndoCommand
{
public:
    struct PointPos {
        int id = 0;
        QPointF before;
        QPointF after;
    };

    MoveItemCommand(MapScene *scene,QList<PointPos> posInfo,const QString &label);

    void redo() override;      // push() 时自动调用一次；之后每次重做(Ctrl+Y)再调：挪到 after
    void undo() override;      // 撤销(Ctrl+Z)：挪回 before

private:
    MapScene *m_scene;
    QList<PointPos> m_pointSpecs;
};

#endif // MOVEITEMCOMMAND_H
