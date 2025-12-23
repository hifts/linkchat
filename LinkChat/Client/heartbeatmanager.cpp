#include "heartbeatmanager.h"
#include "logger.h"
#include "../common/configmanager.h"
#include "../common/configkeys.h"

HeartbeatManager::HeartbeatManager(QObject *parent)
    : QObject{parent}
    ,m_isActive(false)
    ,m_missedHeartbeats(0)
    ,m_heartbeatInterval(5000)
    ,m_heartbeatTimeout(15000),
    m_maxMissedHeartbeats(3)
{
    // 从 ConfigManager 读取心跳间隔和超时时间
    m_heartbeatInterval = ConfigManager::instance().getInt(
        ConfigKeys::Client::HEARTBEAT_INTERVAL, 5000);
    m_heartbeatTimeout = ConfigManager::instance().getInt(
        ConfigKeys::Client::HEARTBEAT_TIMEOUT, 15000);
    
    LOG_INFO(QString("HeartbeatManager initialized with interval=%1ms, timeout=%2ms")
                 .arg(m_heartbeatInterval)
                 .arg(m_heartbeatTimeout));
    
    // 初始化心跳发送定时器
    m_heartbeatTimer = new QTimer(this);
    m_heartbeatTimer->setInterval(m_heartbeatInterval);
    connect(m_heartbeatTimer,&QTimer::timeout,this,&HeartbeatManager::onSendHeartbeat);

    // 初始化超时检查定时器
    m_timeoutCheckTimer = new QTimer(this);
    m_timeoutCheckTimer->setInterval(m_heartbeatTimeout);
    connect(m_timeoutCheckTimer,&QTimer::timeout,this,&HeartbeatManager::onCheckTimeout);
    
    // 订阅 ConfigManager 的配置变更信号以支持热加载
    connect(&ConfigManager::instance(), &ConfigManager::configChanged,
            this, &HeartbeatManager::onConfigChanged);
}

void HeartbeatManager::start()
{
    if(m_isActive){
        LOG_WARN("Heartbeat is running...");
        return;
    }

    LOG_INFO("start heart manager");
    m_isActive = true;
    m_missedHeartbeats = 0;

    // 启动心跳定时器和超时检查定时器
    m_heartbeatTimer->start();
    m_timeoutCheckTimer->start();
}

void HeartbeatManager::stop()
{
    if(!m_isActive){
        LOG_INFO("Heartbeat has stopped");
        return;
    }

    LOG_INFO("Stop heart");
    m_isActive = false;

    m_heartbeatTimer->stop();
    m_timeoutCheckTimer->stop();
}

void HeartbeatManager::onHeartbeatReceived()
{
    if(!m_isActive){
        return;
    }

    // 收到心跳响应,重置计数
    m_missedHeartbeats = 0;

    // 重启超时定时器
    m_timeoutCheckTimer->start();
    LOG_INFO("收到心跳响应，重置计数");
}

void HeartbeatManager::onSendHeartbeat()
{
    if(!m_isActive){
        return;
    }

    // 增加错过心跳包次数
    m_missedHeartbeats++;

    // 发送信号
    emit needSendHeartbeat();
    LOG_INFO(QString("发送心跳包，当前错过次数：%1").arg(m_missedHeartbeats));
}

void HeartbeatManager::onCheckTimeout()
{
    if(!m_isActive){
        return;
    }

    // 检查是否超过最大允许错过次数
    if(m_missedHeartbeats >= m_maxMissedHeartbeats){
        emit heartbeatTimeout(m_missedHeartbeats);
        LOG_WARN(QString("心跳超时，连续错过超过%1次心跳响应").arg(m_missedHeartbeats));
    }
}

bool HeartbeatManager::isActive() const
{
    return m_isActive;
}

int HeartbeatManager::missedHeartbeats() const
{
    return m_missedHeartbeats;
}

void HeartbeatManager::setHeartbeatInterval(int ms)
{
    m_heartbeatInterval = ms;
    m_heartbeatTimer->setInterval(m_heartbeatInterval);
    LOG_INFO(QString("设置心跳间隔为%1毫秒").arg(ms));
}

void HeartbeatManager::setHeartbeatTimeout(int ms)
{
    m_heartbeatTimeout = ms;
    m_timeoutCheckTimer->setInterval(ms);
    LOG_INFO(QString("设置了心跳超时检测为%1毫秒").arg(ms));
}

void HeartbeatManager::setMaxMissedHeartbeats(int newMaxMissedHeartbeats)
{
    m_maxMissedHeartbeats = newMaxMissedHeartbeats;
}

void HeartbeatManager::onConfigChanged(const QString& key)
{
    // 检查是否是心跳相关的配置变更
    if (key == ConfigKeys::Client::HEARTBEAT_INTERVAL) {
        int newInterval = ConfigManager::instance().getInt(
            ConfigKeys::Client::HEARTBEAT_INTERVAL, 5000);
        
        if (newInterval != m_heartbeatInterval) {
            setHeartbeatInterval(newInterval);
            LOG_INFO(QString("心跳间隔已更新为%1毫秒（配置热加载）").arg(newInterval));
        }
    }
    else if (key == ConfigKeys::Client::HEARTBEAT_TIMEOUT) {
        int newTimeout = ConfigManager::instance().getInt(
            ConfigKeys::Client::HEARTBEAT_TIMEOUT, 15000);
        
        if (newTimeout != m_heartbeatTimeout) {
            setHeartbeatTimeout(newTimeout);
            LOG_INFO(QString("心跳超时已更新为%1毫秒（配置热加载）").arg(newTimeout));
        }
    }
}
