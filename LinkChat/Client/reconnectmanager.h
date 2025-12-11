#ifndef RECONNECTMANAGER_H
#define RECONNECTMANAGER_H

#include <QObject>
#include <QTimer>

/**
 * @brief 断线重连管理器
 * 职责:
 * 1. 检测网络断线
 * 2. 自动重连(指数退避算法)
 * 3. 保存登录状态,重连后自动登录
 * 4. 管理重连次数和延迟
 */

class ReconnectManager : public QObject
{
    Q_OBJECT
public:
    // 连接状态
    enum ConnectionState {
        Disconnected,       // 已断开
        Connecting,         // 连接中
        Connected,          // 已连接
        Reconnecting        // 重连中
    };

    explicit ReconnectManager(QObject *parent = nullptr);

    // 设置连接状态和获取连接状态
    void setConnectionState(ConnectionState state);
    ConnectionState getConnectState() const{return m_state;}

    // 是否正在重连
    bool isReconnecting() const {return m_state == Connecting;}

    // 启动/禁用自动重连
    void setAutoConnect(bool enable);

    // 开始重连
    void startReconnect();

    // 停止重连
    void stopReconnect();

    // 连接成功（重置重连次数）
    void onReconnectSuccess();

    // 获取重连次数
    int getReconnectAttempts() const {return m_reconnectAttempts;}

    // 设置和获取服务器信息
    void setServerInfo(const QString &ip,uint16_t port);
    QString getServerIp() const{return m_serverIp;}
    uint16_t getServerPort() const {return m_serverPort;}

    // 登录信息（用于自动重连后自动登录）
    void saveLoginInfo(const QString &userName,const QString &password);

    // 清楚登录消息
    void clearLoginInfo();

    // 是否有保存的登录信息
    bool hasLoginInfo() const {return !m_savedUserName.isEmpty();}

    // 获取登录信息
    QString getSavedUsername() const { return m_savedUserName; }
    QString getSavedPassword() const { return m_savedPassword; }

    // 设置配置信息
    void setInitialDelay(int ms);
    void setMaxDelay(int ms);
    void setMaxAttempts(int count);

private slots:
    // 执行重连
    void doReconnect();

signals:
    // 需要自动登录信号
    void needAutoLogin(const QString &userName,const QString &password);

    // 重连状态改变
    void reconnectStateChanged(int attempts, int delayMs);

    // 达到最大重连次数
    void maxAttemptsReached();

    // 需要重连信号
    void needReconnect(const QString &ip,uint16_t port);
private:
    // 计算下一次延迟时间
    int calculateNextDelay();

private:
    // 状态
    ConnectionState m_state;        // 当前连接状态
    bool m_autoReconnect;           // 是否自动重连
    int m_reconnectAttempts;        // 尝试重连次数

    // 服务器信息
    QString m_serverIp;
    uint16_t m_serverPort;

    // 登录信息
    QString m_savedUserName;
    QString m_savedPassword;

    int m_initialDelay;             // 重连延迟(默认5s)
    int m_maxDelay;                 // 最大重连延迟(默认30s)
    int m_maxAttempts;              // 最大重连次数(默认0=无限)

    // 重连定时器
    QTimer *m_reconnectTimer;
};

#endif // RECONNECTMANAGER_H
