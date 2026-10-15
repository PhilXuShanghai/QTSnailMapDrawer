#include "mapio.h"
#include <QFile>
#include <QTextStream>
#include <QStringList>
#include <QGraphicsEllipseItem>
#include "MapIOException.h"
#include "maplineitem.h"
#include "mappointitem.h"
#include <QDebug>
#include <QTextCodec>
#include <QSaveFile>
#include <QFileInfo>
bool MapIO::saveInternal(const QString &path, QGraphicsScene *scene, QString &err, QString mapVersion)
{
    const QByteArray data = serializeScene(scene, mapVersion);

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        err = file.errorString();
        return false;
    }

    if (file.write(data) != data.size()) {
        err = file.errorString();
        file.cancelWriting();        // 丢弃临时文件
        return false;
    }

    if (!file.commit()) {            // 真正落地：临时文件 -> path
        err = file.errorString();
        return false;
    }

    return true;
}

QByteArray MapIO::serializeScene(QGraphicsScene *scene, const QString &mapVersion)
{
    const QList<QStringList> rows = getMapCSVRows(scene, mapVersion);

    QString text;
    for (const QStringList &row : rows) {
        QStringList escaped;
        escaped.reserve(row.size());
        for (const QString &f : row)
            escaped << csvEscape(f);
        text += escaped.join(QLatin1Char(',')) + QLatin1Char('\n');
    }

    QTextCodec *gbk = QTextCodec::codecForName("GBK");
    return gbk->fromUnicode(text);   // 与 .rou 文件同一条编码路径
}

QString MapIO::csvEscape(const QString &field)
{
    if (field.contains(QLatin1Char('"')) ||
        field.contains(QLatin1Char(',')) ||
        field.contains(QLatin1Char('\n')) ||
        field.contains(QLatin1Char('\r')))
    {
        QString s = field;
        s.replace(QLatin1String("\""), QLatin1String("\"\""));
        return QLatin1Char('"') + s + QLatin1Char('"');
    }
    return field;
}

QList<QStringList> MapIO::getMapCSVRows(QGraphicsScene *scene, QString mapVersion)
{
    QList<QStringList> mapVersionRow = getMapVersionRow(mapVersion);
    QList<QStringList> collectPoints = serializedItems(MapIO::collectItems<MapPointItem>(scene));
    QList<QStringList> collectRows= serializedItems(MapIO::collectItems<MapLineItem>(scene));
    QList<QStringList> csvResult;
    csvResult.reserve(mapVersionRow.size() + collectPoints.size() + collectRows.size());
    csvResult += mapVersionRow;
    csvResult += collectPoints;
    csvResult += collectRows;
    return csvResult;
}

QList<QStringList> MapIO::serializedItems(const QList<MapPointItem *> &mapPoint)
{
    //拷贝一份指针列表，避免修改 const 原列表
    QList<MapPointItem *> sortedPoints = mapPoint;

    //按 id 升序排序
    std::sort(sortedPoints.begin(), sortedPoints.end(),
              [](MapPointItem *a, MapPointItem *b) {
                  return a->id() < b->id();
              });

    QList<QStringList> result;
    result.reserve(mapPoint.size());
    for (MapPointItem *point : sortedPoints){
        QStringList strPoint;
        strPoint.append(QString::number(point->id()));
        // 定点两位小数：快照 undo/redo 依赖序列化往返，默认 'g' 格式只有 6 位
        // 有效数字，万位坐标会被量化（-16488.25 → -16488.3），位置随每次快照漂移
        strPoint.append(QString::number(point->x(), 'f', 2));
        strPoint.append(QString::number(point->y(), 'f', 2));
        // 道路节点才考虑转向等问题
        strPoint.append("0");
        strPoint.append("0");
        strPoint.append("0");
        strPoint.append("0");
        strPoint.append("0");
        // 节点类型：直接序列化 PointType 枚举值，读档时还原（否则类型信息丢失）
        strPoint.append(QString::number(static_cast<int>(point->pointType())));
        // 名称与设置
        strPoint.append(point->name());
        // 相邻链接，仅有道路节点才考虑，之后重构的时候改（道路节点最多5口，多了机器人无法识别）
        strPoint.append("");
        strPoint.append("");
        strPoint.append("");
        strPoint.append("");
        strPoint.append("");
        result.append(strPoint);
    }
    return result;
}

QList<QStringList> MapIO::serializedItems(const QList<MapLineItem *>& mapLine)
{
    QList<QStringList> result;
    result.reserve(mapLine.size());
    for (MapLineItem *line : mapLine){
        QStringList strLine;
        strLine.append(QString::number(line->id()));
        strLine.append(QString::number(line->startPoint()->x(), 'f', 2));
        strLine.append(QString::number(line->startPoint()->y(), 'f', 2));
        strLine.append(QString::number(line->endPoint()->x(), 'f', 2));
        strLine.append(QString::number(line->endPoint()->y(), 'f', 2));
        strLine.append("");
        strLine.append("");
        strLine.append(QString::number(static_cast<int>(line->lineType())));//line->LineType结果判断 Wall = 0 Ground=-1
        result.append(strLine);
    }
    return result;
}


QList<QStringList> MapIO::getMapVersionRow(const QString &mapVersion)
{
    QList<QStringList> result;
    QStringList strLine;
    strLine.append("Version");
    strLine.append(mapVersion);
    strLine.append("");
    result.append(strLine);
    return result;
}

namespace {
// 坐标 → 哈希索引键。建表和查表必须经过同一个函数格式化，
// 保证两侧键完全一致（防浮点表示差异导致的匹配失败）
QString coordKey(qreal x, qreal y)
{
    return QString::number(x, 'f', 2) + QLatin1Char('|')
         + QString::number(y, 'f', 2);
}
} // namespace

// 坐标索引中查点；找不到就补生一个新点。
// 补生是"导入旧地图"的关键路径：旧 .rou 只有墙段没有点表，
// 每个墙段端点都从这里凭空长出一个点来
MapPointItem *MapIO::findOrCreatePoint(const QPointF &pos, QGraphicsScene *scene,
                                       QHash<QString, MapPointItem *> &pointIndex,
                                       int &nextPointId)
{
    const QString key = coordKey(pos.x(), pos.y());
    if (MapPointItem *p = pointIndex.value(key, nullptr))
        return p;

    MapPointItem *p = new MapPointItem(nextPointId++);
    p->setPos(pos);
    scene->addItem(p);
    pointIndex.insert(key, p);
    return p;
}

MapPointItem *MapIO::pointFromRow(const QStringList &row)
{
    MapPointItem *point = new MapPointItem(
        row[0].toInt(), static_cast<MapPointItem::PointType>(row[8].toInt()));
    // 注意 toDouble：坐标可能带小数，toInt 遇到 "100.5" 会静默返回 0
    point->setPos(row[1].toDouble(), row[2].toDouble());
    point->setName(row[9]);
    return point;
}

MapLineItem *MapIO::lineFromRow(const QStringList &row, QGraphicsScene *scene,
                                QHash<QString, MapPointItem *> &pointIndex,
                                int &nextPointId)
{
    // 墙行只存端点坐标、不存点 id：到坐标索引里找回端点，找不到就补生。
    // 不能用 scene->itemAt 之类命中测试——它回答"这附近最上面是谁"，
    // 会因 z 值/距离产生歧义；这里要回答的是"这个坐标精确对应哪个点"
    MapPointItem *start = findOrCreatePoint(QPointF(row[1].toDouble(), row[2].toDouble()),
                                            scene, pointIndex, nextPointId);
    MapPointItem *end = findOrCreatePoint(QPointF(row[3].toDouble(), row[4].toDouble()),
                                          scene, pointIndex, nextPointId);
    // 序号列（row[0]）= 线 ID，随文件往返；种类走工厂选派生类
    return MapLineItem::create(row[0].toInt(), start, end,
                               static_cast<MapLineItem::LineType>(row[7].toInt()));
}

MapIO::LoadResult MapIO::loadInternal(const QString &path, QGraphicsScene *scene)
{
    LoadResult result;

    QFileInfo fi(path);
    const QString suffix = fi.suffix().toLower();
    if (suffix != QStringLiteral("rou") && suffix != QStringLiteral("csv")) {
        result.error = QStringLiteral("文件格式错误");
        return result;
    }
    if (suffix == QStringLiteral("csv"))
        result.info = QStringLiteral("地图格式为csv，为编辑模式，机器人不能直接读");

    try {
        const QList<QStringList> fileInfo = parseCsvLine(path);
        result = restoreScene(scene, fileInfo);
        if (suffix == QStringLiteral("csv"))
            result.info = QStringLiteral("地图格式为csv，为编辑模式，机器人不能直接读");
    } catch (const MapIOException &e) {
        result.error = e.message();
        return result;   // 错误路径必须在此返回，否则 ok 仍为 true
    }

    result.ok = result.error.isEmpty();
    return result;
}

MapIO::LoadResult MapIO::restoreScene(QGraphicsScene *scene, const QList<QStringList> &fileInfo)
{
    LoadResult result;

    // ---- 段1：分类校验（调用方负责保证此刻场景已清空，此处失败场景保持空）----
    QList<QStringList> pointRows, lineRows;
    for (const QStringList &row : fileInfo) {
        if (row.length() == 3) {
            result.mapVersion = row.at(1).toInt();
        } else if (row.length() == 15) {
            pointRows.append(row);
        } else if (row.length() == 8) {
            lineRows.append(row);
        } else {
            throw MapIOException(
                QStringLiteral("无法识别的行（%1 列）").arg(row.length()));
        }
    }

    // ---- 段2：先建点，登记坐标索引 ----
    QHash<QString, MapPointItem *> pointIndex;   // "x|y" → 点（重建期临时索引）
    int nextPointId = 0;
    for (const QStringList &row : pointRows) {
        MapPointItem *point = pointFromRow(row);
        scene->addItem(point);
        pointIndex.insert(coordKey(point->x(), point->y()), point);
        nextPointId = qMax(nextPointId, point->id() + 1);
        ++result.pointsLoaded;
    }

    // ---- 段3：再建线（与行序无关，点必然先全部在场）----
    for (const QStringList &row : lineRows) {
        MapLineItem *line = lineFromRow(row, scene, pointIndex, nextPointId);
        scene->addItem(line);
        ++result.linesLoaded;
    }
    result.maxPointId = nextPointId - 1;
    result.ok = true;
    return result;
}

QList<QStringList> MapIO::parseCsvLine(const QString &path)
{
    QList<QStringList> result;

    if (path.isEmpty())
        throw MapIOException(QStringLiteral("CSV 路径为空"));

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        throw MapIOException(
            QStringLiteral("打开文件失败: %1 (%2)")
                .arg(path, file.errorString()));
    }

    const QByteArray raw = file.readAll();
    file.close();

    return parseCsvBytes(raw);
}

QList<QStringList> MapIO::parseCsvBytes(const QByteArray &raw)
{
    QList<QStringList> result;

    QTextCodec *gbk = QTextCodec::codecForName("GBK");
    if (!gbk)
        throw MapIOException(QStringLiteral("找不到 GBK 编解码器"));

    QString text = gbk->toUnicode(raw);

    if (text.startsWith(QChar(0xFEFF)))
        text.remove(0, 1);

    const QStringList lines = text.split('\n');

    for (int i = 0; i < lines.size(); ++i) {
        QString line = lines.at(i);
        if (line.endsWith('\r'))
            line.chop(1);

        if (line.trimmed().isEmpty())
            continue;

        result.append(line.split(','));
    }

    return result;
}
