#ifndef ADDITEMSCOMMAND_H
#define ADDITEMSCOMMAND_H

#include <QUndoCommand>
#include <QHash>
#include <QList>
#include <QPointF>
#include <QString>

class MapScene;
class MapPointItem;
class MapLineItem;

// "一步添加"命令：把一次用户操作新增的全部点 + 全部线打包成一个 undo 单元
//（连线穿点分段产生的多段线也在内——所以不需要 QUndoStack 宏，一个命令即一个语义单元）。
//
// 存储【规格】而非指针/文本行：
//  - 点规格含 id；线规格含自己的 lineId + 两端点 id + 类型
//    （线身份 = 线自己的 id，端点引用 = 点 id——内存世界里流通的货币全是 ID）
//  - .rou 文本里线的端点用坐标表示——坐标匹配只活在文本边界（加载/导出），
//    命令的世界里不需要任何坐标哈希
//
// 生命周期纪律（与快照命令共存的关键）：
//  - 本命令不长期持有任何图元指针——undo 真删除、redo 按规格重建。
//    身份住在规格（ID）里，对象本身是可牺牲的皮囊；快照恢复随便换血，
//    只要 ID 还在，redo 就能按图施工
class AddItemsCommand : public QUndoCommand
{
public:
    struct PointSpec {
        int id = 0;
        QPointF pos;
        int type = 0;          // MapPointItem::PointType 的 int 形式（避免头文件互相 include）
        QString name;
    };
    struct LineSpec {
        int lineId = 0;        // 线自己的 id（孪生线也能精确区分）
        int idA = 0, idB = 0;  // 两端点的 id
        int type = 0;          // MapLineItem::LineType 的 int 形式
    };

    AddItemsCommand(MapScene *scene, QList<PointSpec> points, QList<LineSpec> lines,
                    const QString &label);

    void redo() override;      // push() 时自动调用一次；之后每次重做(Ctrl+Y)再调
    void undo() override;      // 撤销(Ctrl+Z)：真删除，与 Delete 键同语义（级联）

private:
    MapScene *m_scene;
    QList<PointSpec> m_pointSpecs;
    QList<LineSpec> m_lineSpecs;
};

#endif // ADDITEMSCOMMAND_H
