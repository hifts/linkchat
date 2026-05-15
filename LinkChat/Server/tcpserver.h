#ifndef TCPSERVER_H
#define TCPSERVER_H

#include "clientsocket.h"
#include <QTcpServer>
#include <QHash>
#include <QMutex>
#include <QSet>

class SocketWorkerPool;

class TcpServer : public QTcpServer
{
    Q_OBJECT
public:
    static TcpServer &instance();

    bool tryUserLogin(int uid, ClientSocket* socket);
    void userLogin(int uid, ClientSocket* socket);
    void userLogout(int uid);
    void userLogout(int uid, ClientSocket* socket);

    bool isOnline(int uid);

    ClientSocket *getUserSocket(int uid);
    bool sendToUser(int uid, uint32_t type, const QByteArray& body, uint32_t src = 0, uint32_t dest = 0);
    void notifyUserOffline(int uid);
    void connectionAccepted(ClientSocket* socket);
    void connectionSetupFailed();
    void socketDisconnected(ClientSocket* socket);
    void setMaxConnections(int maxConnections);
    void setWorkerCount(int workerCount);
    void setHeartbeatTimeout(int timeoutMs);
    int currentConnectionCount() const;
    void shutdown();

protected:
    void incomingConnection(qintptr socketDescriptor) override;

private:
    explicit TcpServer(QObject *parent = nullptr);

    mutable QMutex m_mutex;
    QHash<int,ClientSocket*> m_onlineUsers;
    QHash<ClientSocket*, int> m_socketUsers;
    QSet<ClientSocket*> m_connections;
    SocketWorkerPool* m_workerPool = nullptr;
    int m_maxConnections = 1000;
    int m_workerCount = 1;
    int m_heartbeatTimeoutMs = 90000;
    int m_pendingConnections = 0;

signals:
};

#endif // TCPSERVER_H
