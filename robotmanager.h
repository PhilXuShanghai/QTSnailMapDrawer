#pragma once
#include <QObject>
#include <QHash>

#include <QObject>
#include <QHash>
#include <QString>

class QThread;
class RobotWorker;

class RobotManager : public QObject
{
    Q_OBJECT
public:
    explicit RobotManager(QObject *parent = nullptr);
    ~RobotManager() override;

    void connectRobot(const QString &remoteIp, quint16 remotePort);
    void disconnectRobot(const QString &remoteIp);
    void disconnectAll();

    void sendTo(const QString &remoteIp, const QByteArray &payload);
    void broadcast(const QByteArray &payload);

    bool isKnown(const QString &remoteIp) const;
    QStringList knownRobots() const;

signals:
    void robotConnected(const QString &ip);
    void robotDisconnected(const QString &ip);
    void robotStartupFailed(const QString &ip, const QString &reason);
    void robotError(const QString &ip, const QString &msg);
    void robotData(const QString &ip, const QByteArray &payload);
    void robotHeartbeatTimeout(const QString &ip);

private:
    struct Entry {
        QThread     *thread = nullptr;
        RobotWorker *worker = nullptr;
    };

    void destroyEntry(const QString &ip);

    QHash<QString, Entry> m_robots;
};