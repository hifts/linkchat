#include "reconnectmanager.h"
#include "logger.h"

#include <QtMath>

ReconnectManager::ReconnectManager(QObject *parent)
    : QObject{parent}
    ,m_state(Disconnected)
    ,m_autoReconnect(true)
    ,m_reconnectAttempts(0)
    ,m_serverPort(0)
    ,m_initialDelay(5000)
    ,m_maxDelay(30000)
    ,m_maxAttempts(0)
{
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
        LOG_ERROR_FMT("已达到最大重连次数%1，停止重连",m_maxAttempts);

        // 发送最大重连次数信号
        emit maxAttemptsReached();
        return;
    }

    // 正在重连中
    m_state = Reconnecting;

    int delay = calculateNextDelay();

    LOG_INFO(QString("将在 %1 秒后尝试第 %2 次重连").arg(delay / 1000.0,0,'f',1).arg(m_reconnectAttempts + 1));

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
        LOG_INFO_FMT("重连成功（尝试了 %1 次）",m_reconnectAttempts);
    }

    m_reconnectAttempts = 0;
    stopReconnect();

    // 如果有保存有登录信息，则发出自动登录信号
    if(hasLoginInfo()){
        LOG_INFO("自动登录中...");
        emit needAutoLogin(m_savedUserName,m_savedPassword);
    }
}

void ReconnectManager::setServerInfo(const QString &ip, uint16_t port)
{
    m_serverIp = ip;
    m_serverPort = port;
    LOG_INFO(QString("设置了服务器信息:ip = %1,port = %2").arg(ip).arg(port));
}

void ReconnectManager::saveLoginInfo(const QString &userName, const QString &password)
{
    m_savedUserName = userName;
    m_savedPassword = password;
    LOG_INFO_FMT("保存了用户登录信息:userName = %1,password = %2",userName,password);
}

void ReconnectManager::clearLoginInfo()
{
    m_savedUserName.clear();
    m_savedPassword.clear();
    LOG_INFO("用户登录信息已清除");
}

void ReconnectManager::doReconnect()
{
    // 检测出超时了，需要重连

    // 重连次数增加1
    m_reconnectAttempts++;

    QString msg = QString("尝试重连%1 : %2 (第%3次)").arg(m_serverIp).arg(m_serverPort).arg(m_reconnectAttempts);
    LOG_INFO(msg);

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
    LOG_INFO_FMT("设置初始重连延迟为 %1 毫秒",ms);
}

void ReconnectManager::setMaxDelay(int ms)
{
    m_maxDelay = ms;
    LOG_INFO_FMT("设置最大重连延迟为 %1 毫秒",ms);
}

void ReconnectManager::setMaxAttempts(int count)
{
    m_maxAttempts = count;
    QString msg = QString("设置最大重连次数为 %1 %2").arg(count).arg(count == 0 ? "(无限制)" : "");
    LOG_INFO(msg);
}
