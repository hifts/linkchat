#include "tcpserver.h"
#include "dbmanager.h"
#include "logger.h"
#include "configmanager.h"
#include "configkeys.h"

#include <QCoreApplication>

int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);

    // 初始化配置管理器
    if (!ConfigManager::instance().initialize("./server_config.json")) {
        qCritical() << "Failed to initialize config manager";
        return -1;
    }

    // 从配置读取日志级别和文件路径
    QString logLevel = ConfigManager::instance().getString(
        ConfigKeys::Server::Log::LEVEL, "INFO");
    QString logFilePath = ConfigManager::instance().getString(
        ConfigKeys::Server::Log::FILE_PATH, "server.log");

    // 初始化日志系统
    Logger::init(logFilePath, Logger::stringToLevel(logLevel));
    LOG_INFO("LinkChat 服务端启动");
    LOG_INFO("配置文件: " + ConfigManager::instance().getConfigPath());

    // 从配置读取服务器端口
    int port = ConfigManager::instance().getInt(ConfigKeys::Server::PORT, 8080);
    
    // 从配置读取心跳参数
    int heartbeatInterval = ConfigManager::instance().getInt(
        ConfigKeys::Server::HEARTBEAT_INTERVAL, 30000);
    int heartbeatTimeout = ConfigManager::instance().getInt(
        ConfigKeys::Server::HEARTBEAT_TIMEOUT, 90000);
    
    LOG_INFO(QString("服务器配置 - 端口: %1, 心跳间隔: %2ms, 心跳超时: %3ms")
        .arg(port).arg(heartbeatInterval).arg(heartbeatTimeout));

    // 启动 TCP 服务器
    if(TcpServer::instance().listen(QHostAddress::Any, port)){
        LOG_INFO(QString("服务器启动成功，监听端口 %1").arg(port));
    }else{
        LOG_ERROR("服务器启动失败！");
        return -1;
    }

    DBManager::instance().connectToDb();
    LOG_INFO("数据库连接成功");

    return a.exec();
}
