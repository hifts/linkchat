#ifndef HEARTBEATMANAGER_H
#define HEARTBEATMANAGER_H

#include <QObject>
#include <QTimer>

/**
 * @brief 心跳管理器
 *
 * 职责:
 * 1. 定时发送心跳包到服务器
 * 2. 监控心跳响应,检测连接超时
 * 3. 连接异常时通知上层
 */

class HeartbeatManager : public QObject
{
    Q_OBJECT
public:
    explicit HeartbeatManager(QObject *parent = nullptr);

    // 启动心跳
    void start();

    // 停止心跳
    void stop();

    // 收到心跳响应时调用(重置超时计数)
    void onHeartbeatReceived();

    bool isActive() const;
    int missedHeartbeats() const;

    // 设置心跳间隔
    void setHeartbeatInterval(int ms);

    // 设置心跳超时间隔
    void setHeartbeatTimeout(int ms);

    // 设置最大允许错过次数
    void setMaxMissedHeartbeats(int newMaxMissedHeartbeats);

private slots:
    // 发送心跳
    void onSendHeartbeat();

    // 心跳超时检查
    void onCheckTimeout();

signals:
    // 需要发送心跳包
    void needSendHeartbeat();

    // 心跳包超时信号（通知断线重连管理端）
    void heartbeatTimeout(int missedCount);
private:
    bool m_isActive;                // 心跳是否激活
    int m_missedHeartbeats;         // 错过的心跳次数

    QTimer *m_heartbeatTimer;       // 心跳发送定时器
    QTimer *m_timeoutCheckTimer;    // 超时检查定时器

    int m_heartbeatInterval;        // 心跳间隔（默认5s）
    int m_heartbeatTimeout;         // 心跳超时(默认15s)
    int m_maxMissedHeartbeats;      // 最大允许错过次数（默认3次）
};

#endif // HEARTBEATMANAGER_H
