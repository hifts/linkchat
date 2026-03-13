#ifndef TCPSERVER_H
#define TCPSERVER_H

#include "clientsocket.h"
#include <QTcpServer>

class TcpServer : public QTcpServer
{
    Q_OBJECT
public:
    static TcpServer &instance();

    void userLogin(int uid, ClientSocket* socket);
    void userLogout(int uid);

    bool isOnline(int uid);

    ClientSocket *getUserSocket(int uid);

protected:
    void incomingConnection(qintptr socketDescriptor) override;

private:
    explicit TcpServer(QObject *parent = nullptr);

    QMap<int,ClientSocket*> m_onlineUsers;

signals:
};

#endif // TCPSERVER_H
