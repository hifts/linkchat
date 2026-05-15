#include "socketworker.h"

#include "clientsocket.h"
#include "logger.h"
#include "tcpserver.h"

SocketWorker::SocketWorker(QObject *parent)
    : QObject(parent)
{
}

void SocketWorker::addConnection(qintptr socketDescriptor)
{
    auto* socket = new ClientSocket(this);
    if (!socket->setSocketDescriptor(socketDescriptor)) {
        TcpServer::instance().connectionSetupFailed();
        socket->deleteLater();
        return;
    }

    TcpServer::instance().connectionAccepted(socket);
    connect(socket, &QTcpSocket::disconnected, socket, &ClientSocket::deleteLater);
    LOG_DEBUG(QString("New client connected on worker thread: %1").arg(socketDescriptor));
}
