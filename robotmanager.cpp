#include "robotmanager.h"
#include "robotworker.h"
#include "appconfig.h"

#include <QThread>
#include <QHostAddress>
#include <QDebug>

RobotManager::RobotManager(QObject *parent)
    : QObject(parent)
{
}

RobotManager::~RobotManager()
{
    disconnectAll();
}

void RobotManager::connectRobot(const QString &remoteIp, quint16 remotePort)
{
    if (m_robots.contains(remoteIp)) {
        qWarning() << "[RobotManager]" << remoteIp << "已存在，忽略";
        return;
    }

    QHostAddress addr;
    if (!addr.setAddress(remoteIp)) {
        emit robotStartupFailed(remoteIp, QStringLiteral("IP 格式不正确"));
        return;
    }

    auto &cfg = AppConfig::instance();
    const QHostAddress bindIp(cfg.localBindIp());

    QThread *th = new QThread(this);
    RobotWorker *w = new RobotWorker(remoteIp, remotePort,
                                     bindIp, cfg.localBindPort());
    w->moveToThread(th);

    // 线程启动后自动执行 worker->start()
    connect(th, &QThread::started, w, &RobotWorker::start);

    // 转发 worker 信号
    connect(w, &RobotWorker::connected,        this, &RobotManager::robotConnected);
    connect(w, &RobotWorker::disconnected,     this, &RobotManager::robotDisconnected);
    connect(w, &RobotWorker::errorOccurred,    this, &RobotManager::robotError);
    connect(w, &RobotWorker::packetReceived,   this, &RobotManager::robotData);
    connect(w, &RobotWorker::heartbeatTimeout, this, &RobotManager::robotHeartbeatTimeout);

    // 启动失败：直接清理该 Entry
    connect(w, &RobotWorker::startupFailed,
            this, [this](const QString &ip, const QString &reason) {
                emit robotStartupFailed(ip, reason);
                destroyEntry(ip);
            });

    m_robots.insert(remoteIp, {th, w});
    th->start();
}

void RobotManager::disconnectRobot(const QString &remoteIp)
{
    if (!m_robots.contains(remoteIp)) return;

    RobotWorker *w = m_robots.value(remoteIp).worker;
    if (w) {
        QMetaObject::invokeMethod(w, "stop", Qt::QueuedConnection);
    }
    destroyEntry(remoteIp);
}

void RobotManager::disconnectAll()
{
    const QStringList keys = m_robots.keys();
    for (const QString &ip : keys) {
        disconnectRobot(ip);
    }
}

void RobotManager::sendTo(const QString &remoteIp, const QByteArray &payload)
{
    auto it = m_robots.find(remoteIp);
    if (it == m_robots.end()) {
        qWarning() << "[RobotManager] sendTo 未知机器人" << remoteIp;
        return;
    }
    QMetaObject::invokeMethod(it->worker, "sendPacket", Qt::QueuedConnection,
                              Q_ARG(QByteArray, payload));
}

void RobotManager::broadcast(const QByteArray &payload)
{
    for (auto it = m_robots.begin(); it != m_robots.end(); ++it) {
        QMetaObject::invokeMethod(it->worker, "sendPacket", Qt::QueuedConnection,
                                  Q_ARG(QByteArray, payload));
    }
}

bool RobotManager::isKnown(const QString &remoteIp) const
{
    return m_robots.contains(remoteIp);
}

QStringList RobotManager::knownRobots() const
{
    return m_robots.keys();
}

void RobotManager::destroyEntry(const QString &ip)
{
    if (!m_robots.contains(ip)) return;
    Entry e = m_robots.take(ip);

    if (e.thread) {
        e.thread->quit();
        if (!e.thread->wait(3000)) {
            qWarning() << "[RobotManager] 线程未及时退出" << ip;
            e.thread->terminate();
            e.thread->wait(1000);
        }
    }

    if (e.worker) e.worker->deleteLater();
    if (e.thread) e.thread->deleteLater();
}