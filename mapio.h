#ifndef MAPIO_H
#define MAPIO_H

#include <QString>
#include <QGraphicsScene>
#include <QGraphicsItem>
#include <QHash>
#include <QList>
#include <concepts>

class MapPointItem;
class MapLineItem;
///
/// \brief MapIO::saveInternal 用于将screne内容写入csv
/// \param path
/// \param scene
/// \param err
/// \return
///
namespace  MapIO
{

    struct LoadResult {
        bool ok = false;
        int pointsLoaded = 0, linesLoaded = 0;
        int skippedRows = 0;      // 坏行计数
        int maxPointId = 0;       // 给 MainWindow 续 m_nextPointId 用
        int mapVersion = 0;
        QString info;
        QString error;            // 打不开文件之类的致命错误
    };

    // 内部工程格式（完整往返）
    bool saveInternal(const QString &path, QGraphicsScene *scene, QString &err, QString mapVersion);
    LoadResult loadInternal(const QString &path, QGraphicsScene *scene);

    // ---- 快照三件套：undo 的 SnapshotCommand 用（文本全量往返，与文件 IO 解耦）----
    // 场景 → GBK 字节（拍快照）。与 saveInternal 同一条序列化路径，只是不落盘
    QByteArray serializeScene(QGraphicsScene *scene, const QString &mapVersion);
    // GBK 字节 → 行（parseCsvLine 的文本部分抽出，文件与内存共用）
    QList<QStringList> parseCsvBytes(const QByteArray &raw);
    // 行 → 场景（loadInternal 的构建部分抽出）。调用方负责先清空场景
    LoadResult restoreScene(QGraphicsScene *scene, const QList<QStringList> &rows);

    QString csvEscape(const QString &field);
    // 把scene翻译成csv格式
    QList<QStringList> getMapCSVRows (QGraphicsScene *scene,QString mapVersion);
    // 每当我需要获取一种图元就添加一个方法
    template<std::derived_from<QGraphicsItem> TItem>
    QList<TItem*> collectItems(QGraphicsScene *scene)
    {
        QList<TItem*> res;
        if (!scene) return res;
        const auto items = scene->items();
        for (QGraphicsItem *it : items) {
            if (auto target = qgraphicsitem_cast<TItem*>(it))
                res << target;
        }
        return res;
    }
    //  每当我需要翻译一种图元就添加一个方法
    QList<QStringList> serializedItems(const QList<MapPointItem*>& mapPoint);
    QList<QStringList> serializedItems(const QList<MapLineItem*>& mapLine);
    QList<QStringList> getMapVersionRow(const QString &mapVersion);

    QList<QStringList> parseCsvLine(const QString &path);

    // ---- 反序列化：行 → 图元 ----
    // 点行 → 点（只用 row，不碰场景）
    MapPointItem *pointFromRow(const QStringList &row);
    // 墙行 → 线。端点坐标到 pointIndex 里找回，找不到由 findOrCreatePoint 补生
    MapLineItem *lineFromRow(const QStringList &row, QGraphicsScene *scene,
                             QHash<QString, MapPointItem *> &pointIndex,
                             int &nextPointId);
    // 坐标索引查点/补生点（"导入旧地图"的关键路径）
    MapPointItem *findOrCreatePoint(const QPointF &pos, QGraphicsScene *scene,
                                    QHash<QString, MapPointItem *> &pointIndex,
                                    int &nextPointId);
};

#endif // MAPIO_H
