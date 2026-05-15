#include "tcpserver.h"

#include "logger.h"
#include "serverstats.h"
#include "socketworker.h"
#include "socketworkerpool.h"

#include <QMetaObject>
#include <QMutexLocker>
#include <QTcpSocket>

TcpServer::TcpServer(QObject *parent)
    : QTcpServer{parent}
{
    m_workerPool = new SocketWorkerPool(this);
}

TcpServer &TcpServer::instance()
{
    static TcpServer instance;
    return instance;
}

bool TcpServer::tryUserLogin(int uid, ClientSocket *socket)
{
    {
        QMutexLocker locker(&m_mutex);
        if (m_onlineUsers.contains(uid)) {
            return false;
        }
        m_onlineUsers.insert(uid, socket);
        m_socketUsers.insert(socket, uid);
    }

    ServerStats::instance().userLoggedIn();
    LOG_INFO_FMT("[Online] User ID:%1", uid);
    return true;
}

void TcpServer::userLogin(int uid, ClientSocket *socket)
{
    bool inserted = false;
    {
        QMutexLocker locker(&m_mutex);
        if (!m_onlineUsers.contains(uid)) {
            inserted = true;
        }
        m_onlineUsers.insert(uid, socket);
        m_socketUsers.insert(socket, uid);
    }

    if (inserted) {
        ServerStats::instance().userLoggedIn();
    }
    LOG_INFO_FMT("[Online] User ID:%1", uid);
}

void TcpServer::userLogout(int uid)
{
    ClientSocket* socket = nullptr;
    bool removed = false;
    {
        QMutexLocker locker(&m_mutex);
        socket = m_onlineUsers.value(uid, nullptr);
        removed = m_onlineUsers.remove(uid) > 0;
        if (socket) {
            m_socketUsers.remove(socket);
        }
    }

    if (removed) {
        ServerStats::instance().userLoggedOut();
    }
    if (socket) {
        QMetaObject::invokeMethod(socket, "markLoggedOut", Qt::QueuedConnection);
    }
    LOG_INFO_FMT("[Offline] User ID:%1", uid);
}

void TcpServer::userLogout(int uid, ClientSocket* socket)
{
    bool removed = false;
    {
        QMutexLocker locker(&m_mutex);
        if (m_onlineUsers.value(uid, nullptr) != socket) {
            return;
        }
        removed = m_onlineUsers.remove(uid) > 0;
        m_socketUsers.remove(socket);
    }

    if (removed) {
        ServerStats::instance().userLoggedOut();
        LOG_INFO_FMT("[Offline] User ID:%1", uid);
    }
}

bool TcpServer::isOnline(int uid)
{
    QMutexLocker locker(&m_mutex);
    return m_onlineUsers.contains(uid);
}

ClientSocket *TcpServer::getUserSocket(int uid)
{
    QMutexLocker locker(&m_mutex);
    return m_onlineUsers.value(uid, nullptr);
}

bool TcpServer::sendToUser(int uid, uint32_t type, const QByteArray& body, uint32_t src, uint32_t dest)
{
    ClientSocket* socket = getUserSocket(uid);
    if (!socket) {
        return false;
    }

    QMetaObject::invokeMethod(socket, "sendPacket",
                              Qt::QueuedConnection,
                              Q_ARG(uint32_t, type),
                              Q_ARG(QByteArray, body),
                              Q_ARG(uint32_t, src),
                              Q_ARG(uint32_t, dest));
    return true;
}

void TcpServer::notifyUserOffline(int uid)
{
    ClientSocket* socket = getUserSocket(uid);
    if (!socket) {
        return;
    }

    QMetaObject::invokeMethod(socket, "notifyOfflineToFriends", Qt::QueuedConnection);
}

void TcpServer::connectionAccepted(ClientSocket* socket)
{
    {
        QMutexLocker locker(&m_mutex);
        if (m_pendingConnections > 0) {
            --m_pendingConnections;
        }
        m_connections.insert(socket);
    }
    socket->setHeartbeatTimeout(m_heartbeatTimeoutMs);
    ServerStats::instance().connectionAccepted();
}

void TcpServer::connectionSetupFailed()
{
    {
        QMutexLocker locker(&m_mutex);
        if (m_pendingConnections > 0) {
            --m_pendingConnections;
        }
    }
    ServerStats::instance().connectionRejected();
}

void TcpServer::socketDisconnected(ClientSocket* socket)
{
    int uid = 0;
    bool connectionRemoved = false;
    bool userRemoved = false;
    {
        QMutexLocker locker(&m_mutex);
        connectionRemoved = m_connections.remove(socket) > 0;
        uid = m_socketUsers.take(socket);
        if (uid != 0 && m_onlineUsers.value(uid, nullptr) == socket) {
            userRemoved = m_onlineUsers.remove(uid) > 0;
        }
    }

    if (connectionRemoved) {
        ServerStats::instance().connectionClosed();
    }
    if (userRemoved) {
        ServerStats::instance().userLoggedOut();
        LOG_INFO_FMT("[Offline] User ID:%1", uid);
    }
}

void TcpServer::setMaxConnections(int maxConnections)
{
    m_maxConnections = qMax(1, maxConnections);
    setMaxPendingConnections(m_maxConnections);
}

void TcpServer::setWorkerCount(int workerCount)
{
    m_workerCount = qMax(1, workerCount);
}

void TcpServer::setHeartbeatTimeout(int timeoutMs)
{
    m_heartbeatTimeoutMs = qMax(1000, timeoutMs);
}

int TcpServer::currentConnectionCount() const
{
    QMutexLocker locker(&m_mutex);
    return m_connections.size() + m_pendingConnections;
}

void TcpServer::shutdown()
{
    close();
    if (m_workerPool) {
        m_workerPool->stop();
    }
}

void TcpServer::incomingConnection(qintptr socketDescriptor)
{
    if (m_workerPool && m_workerPool->workerCount() == 0) {
        m_workerPool->start(m_workerCount);
    }

    {
        QMutexLocker locker(&m_mutex);
        if (m_connections.size() + m_pendingConnections >= m_maxConnections) {
            ServerStats::instance().connectionRejected();
            LOG_WARN(QString("Rejecting connection %1: max_connections=%2 reached")
                         .arg(socketDescriptor)
                         .arg(m_maxConnections));
            QTcpSocket rejectSocket;
            rejectSocket.setSocketDescriptor(socketDescriptor);
            rejectSocket.disconnectFromHost();
            return;
        }
        ++m_pendingConnections;
    }

    SocketWorker* worker = m_workerPool ? m_workerPool->nextWorker() : nullptr;
    if (!worker) {
        connectionSetupFailed();
        QTcpSocket rejectSocket;
        rejectSocket.setSocketDescriptor(socketDescriptor);
        rejectSocket.disconnectFromHost();
        return;
    }

    QMetaObject::invokeMethod(worker, "addConnection",
                              Qt::QueuedConnection,
                              Q_ARG(qintptr, socketDescriptor));
}
