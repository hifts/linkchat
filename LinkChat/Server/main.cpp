#include "tcpserver.h"
#include "dbmanager.h"
#include "logger.h"
#include "configmanager.h"
#include "configkeys.h"

#include <QCoreApplication>
#include <QDir>

int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);

    const QString appDir = QCoreApplication::applicationDirPath();
    QDir::setCurrent(appDir);

    const QString logFilePath = QDir(appDir).filePath("server.log");
    const QString configFilePath = QDir(appDir).filePath("server_config.json");

    Logger::init(logFilePath, Logger::INFO);
    LOG_INFO(QString("Server startup directory: %1").arg(appDir));
    LOG_INFO(QString("Server config path: %1").arg(configFilePath));

    if (!ConfigManager::instance().initialize(configFilePath)) {
        qCritical() << "Failed to initialize config manager";
        LOG_ERROR("Failed to initialize config manager");
        return -1;
    }

    QString logLevel = ConfigManager::instance().getString(
        ConfigKeys::Server::Log::LEVEL, "INFO");
    QString configuredLogFilePath = ConfigManager::instance().getString(
        ConfigKeys::Server::Log::FILE_PATH, "server.log");

    Logger::init(QDir(appDir).filePath(configuredLogFilePath), Logger::stringToLevel(logLevel));
    LOG_INFO("LinkChat server starting");
    LOG_INFO("Config file: " + ConfigManager::instance().getConfigPath());

    int port = ConfigManager::instance().getInt(ConfigKeys::Server::PORT, 8080);
    
    int heartbeatInterval = ConfigManager::instance().getInt(
        ConfigKeys::Server::HEARTBEAT_INTERVAL, 30000);
    int heartbeatTimeout = ConfigManager::instance().getInt(
        ConfigKeys::Server::HEARTBEAT_TIMEOUT, 90000);
    
    LOG_INFO(QString("Server config - port: %1, heartbeat interval: %2ms, heartbeat timeout: %3ms")
        .arg(port).arg(heartbeatInterval).arg(heartbeatTimeout));

    if(TcpServer::instance().listen(QHostAddress::Any, port)){
        LOG_INFO(QString("Server started, listening on port %1").arg(port));
    }else{
        LOG_ERROR("Server failed to start");
        return -1;
    }

    if (!DBManager::instance().connectToDb()) {
        LOG_ERROR("Database connection failed, server exiting");
        return -1;
    }
    LOG_INFO("Database connected");

    return a.exec();
}
