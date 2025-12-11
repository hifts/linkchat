#include "tcpserver.h"
#include "dbmanager.h"
#include "logger.h"

#include <QCoreApplication>

int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);

    // 初始化日志系统
    Logger::init("server.log", Logger::DEBUG);
    LOG_INFO("LinkChat 服务端启动");

    // TcpServer server;
    if(TcpServer::instance().listen(QHostAddress::Any,8080)){
        LOG_INFO("服务器启动成功，监听端口 8080");
    }else{
        LOG_ERROR("服务器启动失败！");
        return -1;
    }

    DBManager::instance().connectToDb();
    LOG_INFO("数据库连接成功");

    return a.exec();
}
