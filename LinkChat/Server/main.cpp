#include "tcpserver.h"
#include "dbmanager.h"

#include <QCoreApplication>

int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);

    // TcpServer server;
    if(TcpServer::instance().listen(QHostAddress::Any,8080)){
        qDebug()<<"LinkChat Server Started on port 8080...";
    }else{
        qDebug()<<"Server Failed to Start";
        return -1;
    }

    DBManager::instance().connectToDb();

    return a.exec();
}
