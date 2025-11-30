#ifndef TCPSERVER_H
#define TCPSERVER_H

#include "clientsocket.h"
#include <QTcpServer>

class TcpServer : public QTcpServer
{
    Q_OBJECT
public:
    static TcpServer &instance();

    // 维护在线用户
    void userLogin(int uid, ClientSocket* socket);
    void userLogout(int uid);

    // 用户是否在线
    bool isOnline(int uid);

    // 获取在线用户的套接字socket(转发消息或文件用)
    ClientSocket *getUserSocket(int uid);

protected:
    // 监听客户端连接
    void incomingConnection(qintptr socketDescriptor) override;

private:
    explicit TcpServer(QObject *parent = nullptr);

    // 记录在线用户: Key=用户ID, Value=Socket指针
    QMap<int,ClientSocket*> m_onlineUsers;

signals:
};

#endif // TCPSERVER_H
