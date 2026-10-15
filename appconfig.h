#ifndef APPCONFIG_H
#define APPCONFIG_H

#include <QString>
#include <QHostAddress>

class AppConfig
{
public:
    static AppConfig &instance();

    void load(const QString &path = QStringLiteral("config.ini"));

    // 本地绑定
    QString localBindIp() const   { return m_localBindIp; }
    quint16 localBindPort() const { return m_localBindPort; }

    // 默认机器人端口（UI 没填端口时用）
    quint16 robotPort() const     { return m_robotPort; }

    // 心跳/重连参数
    int heartbeatIntervalMs() const { return m_heartbeatIntervalMs; }
    int maxMissedPong() const       { return m_maxMissedPong; }
    int reconnectBaseMs() const     { return m_reconnectBaseMs; }
    int reconnectMaxMs() const      { return m_reconnectMaxMs; }

private:
    AppConfig() = default;

    QString m_localBindIp   = QStringLiteral("0.0.0.0"); // 0.0.0.0 = 不绑定，让系统选
    quint16 m_localBindPort = 0;
    quint16 m_robotPort     = 9200;
    int     m_heartbeatIntervalMs = 10000;
    int     m_maxMissedPong       = 3;
    int     m_reconnectBaseMs     = 1000;
    int     m_reconnectMaxMs      = 30000;
};

#endif // APPCONFIG_H
