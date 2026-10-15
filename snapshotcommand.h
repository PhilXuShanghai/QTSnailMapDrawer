#ifndef SNAPSHOTCOMMAND_H
#define SNAPSHOTCOMMAND_H

#include <QUndoCommand>
#include <QByteArray>
#include <QString>
#include <functional>

// 快照式命令：动结构的操作（线上加点/删除/合并/分割…）用它。
// before/after 各存一份全量序列化（GBK 字节），undo/redo = 整场恢复到对应状态。
//
// 为什么快照式管这些操作：它们改的是"既有图元的状态"（端点被改、图元被删），
// 逆操作需要"记住手术前的样子"——全量记录永远不会漏字段。
//
// 与命令式（AddItemsCommand）共存的配合：
//  - restore 由外部注入（MainWindow::restoreSnapshot），本类不认识任何 UI
//  - 恢复=场景整体换血；AddItemsCommand 不持有指针、只认 ID，换血对它无害
class SnapshotCommand : public QUndoCommand
{
public:
    // restorer：把一份快照字节恢复进场景（清场→重建→resync 计数器）
    using Restorer = std::function<void(const QByteArray &)>;

    SnapshotCommand(const QByteArray &before, const QByteArray &after,
                    Restorer restorer, const QString &label);

    void redo() override;
    void undo() override;

private:
    QByteArray m_before;    // 操作前的全量状态
    QByteArray m_after;     // 操作后的全量状态
    Restorer m_restore;
    bool m_firstRedo = true;    // push 前操作已生效，首次 redo 跳过（避免无谓的整场重建）
};

#endif // SNAPSHOTCOMMAND_H
