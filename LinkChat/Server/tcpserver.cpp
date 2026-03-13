#include "tcpserver.h"
#include "logger.h"


TcpServer::TcpServer(QObject *parent)
    : QTcpServer{parent}
{}

TcpServer &TcpServer::instance()
{
    static TcpServer instance;
    return instance;
}

void TcpServer::userLogin(int uid, ClientSocket *socket)
{
    m_onlineUsers.insert(uid,socket);
    LOG_INFO_FMT("[Online] User ID:%1",uid);
}

void TcpServer::userLogout(int uid)
{
    m_onlineUsers.remove(uid);
    LOG_INFO_FMT("[Offline] User ID:%1",uid);
}

bool TcpServer::isOnline(int uid)
{
    return m_onlineUsers.contains(uid);
}

ClientSocket *TcpServer::getUserSocket(int uid)
{
    if(m_onlineUsers.contains(uid)){
        return m_onlineUsers.value(uid);
    }

    return nullptr;
}

void TcpServer::incomingConnection(qintptr socketDescriptor)
{
    ClientSocket *socket = new ClientSocket(this);

    if(!socket->setSocketDescriptor(socketDescriptor)){
        socket->deleteLater();
        return;
    }

    connect(socket,&QTcpSocket::disconnected,socket,&ClientSocket::deleteLater);

    qDebug()<< "New Client Connected:"<<socketDescriptor;
}
