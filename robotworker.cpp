#include "robotworker.h"
#include "appconfig.h"

#include <QTcpSocket>
#include <QTimer>
#include <QtEndian>
#include <QDebug>

RobotWorker::RobotWorker(const QString &remoteIp,
                         quint16 remotePort,
                         const QHostAddress &localBindIp,
                         quint16 localBindPort,
                         QObject *parent)
    : QObject(parent)
    , m_remoteIp(remoteIp)
    , m_remotePort(remotePort)
    , m_localBindIp(localBindIp)
    , m_localBindPort(localBindPort)
{
    auto &cfg = AppConfig::instance();
    m_reconnectDelayMs = cfg.reconnectBaseMs();
}

RobotWorker::~RobotWorker()
{
    if (m_socket) {
        m_socket->abort();
    }
}

void RobotWorker::start()
{
    m_socket = new QTcpSocket(this);

    connect(m_socket, &QTcpSocket::connected,
            this, &RobotWorker::onConnected);
    connect(m_socket, &QTcpSocket::disconnected,
            this, &RobotWorker::onDisconnected);
    connect(m_socket, &QTcpSocket::readyRead,
            this, &RobotWorker::onReadyRead);
    connect(m_socket, &QAbstractSocket::errorOccurred,
            this, &RobotWorker::onSocketError);

    auto &cfg = AppConfig::instance();

    m_heartbeatTimer = new QTimer(this);
    m_heartbeatTimer->setInterval(cfg.heartbeatIntervalMs());
    connect(m_heartbeatTimer, &QTimer::timeout,
            this, &RobotWorker::onHeartbeatTick);

    m_reconnectTimer = new QTimer(this);
    m_reconnectTimer->setSingleShot(true);
    connect(m_reconnectTimer, &QTimer::timeout,
            this, &RobotWorker::onReconnectTimeout);

    // 只有配置里指定了具体 IP 才 bind；0.0.0.0 视为"不绑定"
    const QHostAddress anyV4(QStringLiteral("0.0.0.0"));
    const bool needBind = !m_localBindIp.isNull()
                          && m_localBindIp != QHostAddress::Any
                          && m_localBindIp != anyV4;

    if (needBind) {
        if (!m_socket->bind(m_localBindIp, m_localBindPort)) {
            const QString reason = QStringLiteral("绑定本地 %1:%2 失败: %3")
                                       .arg(m_localBindIp.toString())
                                       .arg(m_localBindPort)
                                       .arg(m_socket->errorString());
            qWarning() << "[RobotWorker]" << reason;
            emit startupFailed(m_remoteIp, reason);
            return;
        }
        qDebug() << "[RobotWorker] 已绑定本地"
                 << m_localBindIp.toString() << ":" << m_localBindPort;
    }

    qDebug() << "[RobotWorker] 连接机器人"
             << m_remoteIp << ":" << m_remotePort;
    m_socket->connectToHost(m_remoteIp, m_remotePort);
}

void RobotWorker::stop()
{
    m_stopping = true;

    if (m_heartbeatTimer) m_heartbeatTimer->stop();
    if (m_reconnectTimer) m_reconnectTimer->stop();

    if (m_socket) {
        m_socket->abort();
    }
}

void RobotWorker::sendPacket(const QByteArray &payload)
{
    if (!m_socket || m_socket->state() != QAbstractSocket::ConnectedState) {
        qWarning() << "[RobotWorker] sendPacket 忽略: socket 未连接"
                   << m_remoteIp;
        return;
    }
    writeWithLength(payload);
}

void RobotWorker::onConnected()
{
    qDebug() << "[RobotWorker] 已连接" << m_remoteIp;

    auto &cfg = AppConfig::instance();
    m_reconnectDelayMs = cfg.reconnectBaseMs();
    m_missedPong       = 0;

    m_heartbeatTimer->start();

    emit connected(m_remoteIp);
}

void RobotWorker::onDisconnected()
{
    qDebug() << "[RobotWorker] 断开" << m_remoteIp;

    if (m_heartbeatTimer) m_heartbeatTimer->stop();

    emit disconnected(m_remoteIp);

    if (!m_stopping) {
        scheduleReconnect();
    }
}

void RobotWorker::onReadyRead()
{
    m_buffer.append(m_socket->readAll());

    while (m_buffer.size() >= 4) {
        const quint32 len = qFromBigEndian<quint32>(
            reinterpret_cast<const uchar *>(m_buffer.constData()));

        // 长度保护，防止异常包把内存吃满
        if (len > 64u * 1024u * 1024u) {
            qWarning() << "[RobotWorker] 非法包长:" << len << "，丢弃缓冲";
            m_buffer.clear();
            return;
        }

        if (m_buffer.size() < 4 + static_cast<int>(len)) {
            break; // 半包，等下一次
        }

        const QByteArray payload = m_buffer.mid(4, static_cast<int>(len));
        m_buffer.remove(0, 4 + static_cast<int>(len));

        if (payload == QByteArrayLiteral("PONG")) {
            m_missedPong = 0;
            continue;
        }

        emit packetReceived(m_remoteIp, payload);
    }
}

void RobotWorker::onSocketError()
{
    if (!m_socket) return;
    const QString err = m_socket->errorString();
    qWarning() << "[RobotWorker]" << m_remoteIp << "socket 错误:" << err;
    emit errorOccurred(m_remoteIp, err);
}

void RobotWorker::onHeartbeatTick()
{
    if (!m_socket || m_socket->state() != QAbstractSocket::ConnectedState) {
        return;
    }

    auto &cfg = AppConfig::instance();

    if (m_missedPong >= cfg.maxMissedPong()) {
        qWarning() << "[RobotWorker]" << m_remoteIp
                   << "心跳超时 (" << m_missedPong << "次无响应)";
        emit heartbeatTimeout(m_remoteIp);
        m_socket->abort();   // 触发 disconnected -> 重连
        return;
    }

    ++m_missedPong;
    writeWithLength(QByteArrayLiteral("PING"));
}

void RobotWorker::onReconnectTimeout()
{
    if (!m_socket) return;
    if (m_stopping) return;

    qDebug() << "[RobotWorker] 尝试重连" << m_remoteIp
             << "延时" << m_reconnectDelayMs << "ms";

    m_socket->connectToHost(m_remoteIp, m_remotePort);
}

void RobotWorker::writeWithLength(const QByteArray &payload)
{
    if (!m_socket || m_socket->state() != QAbstractSocket::ConnectedState) {
        return;
    }

    QByteArray out;
    out.resize(4);
    const quint32 len = static_cast<quint32>(payload.size());
    qToBigEndian(len, reinterpret_cast<uchar *>(out.data()));
    out.append(payload);

    m_socket->write(out);
}

void RobotWorker::scheduleReconnect()
{
    auto &cfg = AppConfig::instance();

    m_reconnectTimer->start(m_reconnectDelayMs);

    m_reconnectDelayMs = qMin(m_reconnectDelayMs * 2, cfg.reconnectMaxMs());
}