#include "appconfig.h"
#include <QSettings>
#include <QFileInfo>
#include <QDebug>

AppConfig &AppConfig::instance() {
    static AppConfig s;
    return s;
}

void AppConfig::load(const QString &path) {
    if (!QFileInfo::exists(path)) {
        qWarning() << "[AppConfig] 配置文件不存在:" << path
                   << "，使用默认值";
        return;
    }

    QSettings s(path, QSettings::IniFormat);
    s.beginGroup("network");

    m_localBindIp   = s.value("localBindIp",   m_localBindIp).toString();
    m_localBindPort = static_cast<quint16>(
        s.value("localBindPort", m_localBindPort).toUInt());
    m_robotPort     = static_cast<quint16>(
        s.value("robotPort", m_robotPort).toUInt());

    m_heartbeatIntervalMs = s.value("heartbeatIntervalMs", m_heartbeatIntervalMs).toInt();
    m_maxMissedPong       = s.value("maxMissedPong",       m_maxMissedPong).toInt();
    m_reconnectBaseMs     = s.value("reconnectBaseMs",     m_reconnectBaseMs).toInt();
    m_reconnectMaxMs      = s.value("reconnectMaxMs",      m_reconnectMaxMs).toInt();

    s.endGroup();

    qDebug() << "[AppConfig] 已加载:"
             << "bindIp=" << m_localBindIp
             << "robotPort=" << m_robotPort;
}