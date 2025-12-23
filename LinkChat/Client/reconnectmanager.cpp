#include "reconnectmanager.h"
#include "logger.h"
#include "configmanager.h"
#include "configkeys.h"

#include <QtMath>

ReconnectManager::ReconnectManager(QObject *parent)
    : QObject{parent}
    ,m_state(Disconnected)
    ,m_reconnectAttempts(0)
    ,m_serverPort(0)
{
    // 从 ConfigManager 读取重连参数
    m_autoReconnect = ConfigManager::instance().getBool(
        ConfigKeys::Client::AUTO_RECONNECT, true);
    
    m_maxAttempts = ConfigManager::instance().getInt(
        ConfigKeys::Client::RECONNECT_MAX_ATTEMPTS, 5);
    
    m_initialDelay = ConfigManager::instance().getInt(
        ConfigKeys::Client::RECONNECT_INTERVAL, 5000);
    
    // 设置最大延迟为初始延迟的 6 倍（指数退避算法）
    m_maxDelay = m_initialDelay * 6;
    
    LOG_INFO(QString("重连管理器初始化: 自动重连=%1, 最大尝试次数=%2, 初始延迟=%3ms")
        .arg(m_autoReconnect ? "启用" : "禁用")
        .arg(m_maxAttempts)
        .arg(m_initialDelay));
    
    m_reconnectTimer = new QTimer(this);
    m_reconnectTimer->setSingleShot(true);
    connect(m_reconnectTimer,&QTimer::timeout,this,&ReconnectManager::doReconnect);
}

void ReconnectManager::setConnectionState(ConnectionState state)
{
    if(m_state == state){
        return;
    }

    ConnectionState odlState = m_state;
    m_state = state;

    // 状态变化日志
    switch (state) {
    case Disconnected:
        LOG_INFO("状态变更->已断开");
        if(m_autoReconnect && !m_serverIp.isEmpty()){
            startReconnect();
        }
        break;
    case Connecting:
        LOG_INFO("状态变更->连接中");
        break;
    case Connected:
        LOG_INFO("状态变更->已连接");
        onReconnectSuccess();
        break;
    case Reconnecting:
        LOG_INFO("状态变更->重连中");
        break;
    default:
        LOG_INFO("未知状态");
        break;
    }
}

void ReconnectManager::setAutoConnect(bool enable)
{
    m_autoReconnect = enable;
    LOG_INFO(QString("%1自动重连").arg(enable ? "启用" : "禁用"));

    if(!enable){
        // 停止重连
        stopReconnect();
    }
}

void ReconnectManager::startReconnect()
{
    if (!m_autoReconnect) {
        LOG_WARN("自动重连已禁用,跳过重连");
        return;
    }

    if (m_serverIp.isEmpty()) {
        LOG_ERROR("服务器地址为空,无法重连");
        return;
    }

    if (m_state == Reconnecting) {
        LOG_WARN("已在重连中,跳过");
        return;
    }

    // 检查是否到达最大重连次数
    if(m_maxAttempts > 0 && m_reconnectAttempts >= m_maxAttempts){
        LOG_ERROR(QString("已达到最大重连次数%1，停止重连").arg(m_maxAttempts));

        // 发送最大重连次数信号
        emit maxAttemptsReached();
        return;
    }

    // 正在重连中
    m_state = Reconnecting;

    int delay = calculateNextDelay();

    emit reconnectStateChanged(m_reconnectAttempts + 1,delay);

    // 启动重连定时器
    m_reconnectTimer->start(delay);
}

void ReconnectManager::stopReconnect()
{
    if(m_reconnectTimer->isActive()){
        LOG_INFO("停止重连");
        m_reconnectTimer->stop();
    }

    if(m_state == Reconnecting){
        m_state = Disconnected;
    }
}

void ReconnectManager::onReconnectSuccess()
{
    if(m_reconnectAttempts > 0){
        // Reconnection successful
    }

    m_reconnectAttempts = 0;
    stopReconnect();

    // 如果有保存有登录信息，则发出自动登录信号
    if(hasLoginInfo()){
        emit needAutoLogin(m_savedUserName,m_savedPassword);
    }
}

void ReconnectManager::setServerInfo(const QString &ip, uint16_t port)
{
    m_serverIp = ip;
    m_serverPort = port;
}

void ReconnectManager::saveLoginInfo(const QString &userName, const QString &password)
{
    m_savedUserName = userName;
    m_savedPassword = password;
}

void ReconnectManager::clearLoginInfo()
{
    m_savedUserName.clear();
    m_savedPassword.clear();
}

void ReconnectManager::doReconnect()
{
    // 检测出超时了，需要重连

    // 重连次数增加1
    m_reconnectAttempts++;

    // 发送重连信号，通知NetworkManager执行
    emit needReconnect(m_serverIp,m_serverPort);
}

int ReconnectManager::calculateNextDelay()
{
    // 使用指数退避算法计算延迟时间
    // delay = min(initialDelay * 2^attempts, maxDelay)

    // 第一次
    if(m_reconnectAttempts == 0){
        return m_initialDelay;
    }

    // 计算指数避退延迟
    int exponentialDelay = m_initialDelay * qPow(2,m_reconnectAttempts - 1);

    // 限制在最大延迟范围内
    return qMin(exponentialDelay,m_maxDelay);
}

void ReconnectManager::setInitialDelay(int ms)
{
    m_initialDelay = ms;
}

void ReconnectManager::setMaxDelay(int ms)
{
    m_maxDelay = ms;
}

void ReconnectManager::setMaxAttempts(int count)
{
    m_maxAttempts = count;
}
