#ifndef ROBOTWORKER_H
#define ROBOTWORKER_H
#include <QObject>
#include <QHostAddress>
#include <QByteArray>
#include <QString>

class QTcpSocket;
class QTimer;

class RobotWorker : public QObject
{
    Q_OBJECT
public:
    RobotWorker(const QString &remoteIp,
                quint16 remotePort,
                const QHostAddress &localBindIp,
                quint16 localBindPort,
                QObject *parent = nullptr);
    ~RobotWorker() override;

    QString remoteIp() const   { return m_remoteIp; }
    quint16 remotePort() const { return m_remotePort; }

public slots:
    void start();                             // 线程启动后调用
    void stop();                              // 主动断开
    void sendPacket(const QByteArray &payload);

signals:
    void connected(const QString &ip);
    void disconnected(const QString &ip);
    void startupFailed(const QString &ip, const QString &reason);
    void errorOccurred(const QString &ip, const QString &msg);
    void packetReceived(const QString &ip, const QByteArray &payload);
    void heartbeatTimeout(const QString &ip);

private slots:
    void onConnected();
    void onDisconnected();
    void onReadyRead();
    void onSocketError();
    void onHeartbeatTick();
    void onReconnectTimeout();

private:
    void writeWithLength(const QByteArray &payload);
    void scheduleReconnect();

    QString      m_remoteIp;
    quint16      m_remotePort;
    QHostAddress m_localBindIp;
    quint16      m_localBindPort;

    QTcpSocket  *m_socket         = nullptr;
    QTimer      *m_heartbeatTimer = nullptr;
    QTimer      *m_reconnectTimer = nullptr;

    QByteArray   m_buffer;
    int          m_missedPong       = 0;
    int          m_reconnectDelayMs = 1000;

    bool         m_stopping = false;
};
#endif // ROBOTWORKER_H
