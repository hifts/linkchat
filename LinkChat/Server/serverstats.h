#ifndef SERVERSTATS_H
#define SERVERSTATS_H

#include <QObject>
#include <QTimer>
#include <atomic>

class ServerStats : public QObject
{
    Q_OBJECT
public:
    static ServerStats& instance();

    void start(int intervalMs = 5000);

    void connectionAccepted();
    void connectionRejected();
    void connectionClosed();
    void userLoggedIn();
    void userLoggedOut();
    void packetReceived();
    void packetSent();
    void heartbeatReceived();
    void loginSucceeded();
    void loginFailed();
    void messageHandled(uint32_t msgType);
    void dbTaskQueued();
    void dbTaskStarted();
    void dbTaskFinished(qint64 elapsedMs);

    int currentConnections() const;
    int currentUsers() const;
    int pendingDbTasks() const;

private slots:
    void report();

private:
    explicit ServerStats(QObject *parent = nullptr);

    QTimer* m_reportTimer = nullptr;

    std::atomic<int> m_currentConnections{0};
    std::atomic<int> m_currentUsers{0};
    std::atomic<int> m_pendingDbTasks{0};

    std::atomic<quint64> m_totalAccepted{0};
    std::atomic<quint64> m_totalRejected{0};
    std::atomic<quint64> m_totalClosed{0};
    std::atomic<quint64> m_totalPacketsIn{0};
    std::atomic<quint64> m_totalPacketsOut{0};
    std::atomic<quint64> m_totalHeartbeats{0};
    std::atomic<quint64> m_totalLoginSuccess{0};
    std::atomic<quint64> m_totalLoginFailed{0};
    std::atomic<quint64> m_totalDbTasks{0};
    std::atomic<quint64> m_totalDbTimeMs{0};

    quint64 m_lastPacketsIn = 0;
    quint64 m_lastPacketsOut = 0;
    quint64 m_lastHeartbeats = 0;
    quint64 m_lastLoginSuccess = 0;
    quint64 m_lastLoginFailed = 0;
    quint64 m_lastDbTasks = 0;
};

#endif // SERVERSTATS_H
