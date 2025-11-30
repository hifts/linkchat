#include "tcpserver.h"


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
    qDebug()<<"[Online] User ID:"<<uid;
}

void TcpServer::userLogout(int uid)
{
    m_onlineUsers.remove(uid);
    qDebug()<<"[Offline] User ID:"<<uid;
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

    // 用户不在线
    return nullptr;
}

void TcpServer::incomingConnection(qintptr socketDescriptor)
{
    // 有新客户连接就创建一个套接字连接
    ClientSocket *socket = new ClientSocket(this);

    // 设置文件描述符
    if(!socket->setSocketDescriptor(socketDescriptor)){
        socket->deleteLater();
        return;
    }

    // connect(socket,&ClientSocket::signalMsgReceived,this,[=](uint32_t msgType, const QByteArray &data){
    //     qDebug()<<"type:"<<msgType<<"body:"<<data;
    //     socket->write(makePacket(msgType,"server:"+data));
    // });

    // 处理断开
    connect(socket,&QTcpSocket::disconnected,socket,&ClientSocket::deleteLater);

    qDebug()<< "New Client Connected:"<<socketDescriptor;
}
