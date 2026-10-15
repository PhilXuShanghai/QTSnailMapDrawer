#include "snapshotcommand.h"

SnapshotCommand::SnapshotCommand(const QByteArray &before, const QByteArray &after,
                                 Restorer restorer, const QString &label)
    : m_before(before)
    , m_after(after)
    , m_restore(std::move(restorer))
{
    setText(label);
}

void SnapshotCommand::redo()
{
    if (m_firstRedo) {
        // push() 会立刻调一次 redo，但操作在构造前后之间已经生效——
        // 跳过这次，避免同状态无意义的整场重建
        m_firstRedo = false;
        return;
    }
    m_restore(m_after);
}

void SnapshotCommand::undo()
{
    m_restore(m_before);
}
