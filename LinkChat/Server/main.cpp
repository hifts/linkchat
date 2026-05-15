#include "tcpserver.h"
#include "dbworkerpool.h"
#include "logger.h"
#include "serverstats.h"
#include "configmanager.h"
#include "configkeys.h"

#include <QCoreApplication>
#include <QDir>
#include <QMetaType>
#include <QThread>

int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);
    qRegisterMetaType<qintptr>("qintptr");
    qRegisterMetaType<uint32_t>("uint32_t");
    qRegisterMetaType<QByteArray>("QByteArray");

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

    const QString logLevel = ConfigManager::instance().getString(
        ConfigKeys::Server::Log::LEVEL, "WARN");
    const QString configuredLogFilePath = ConfigManager::instance().getString(
        ConfigKeys::Server::Log::FILE_PATH, "server.log");

    Logger::init(QDir(appDir).filePath(configuredLogFilePath), Logger::stringToLevel(logLevel));
    LOG_WARN("LinkChat server starting");
    LOG_WARN("Config file: " + ConfigManager::instance().getConfigPath());

    const int port = ConfigManager::instance().getInt(ConfigKeys::Server::PORT, 8080);
    const int maxConnections = ConfigManager::instance().getInt(ConfigKeys::Server::MAX_CONNECTIONS, 1000);
    const int idealThreads = QThread::idealThreadCount() > 0 ? QThread::idealThreadCount() : 4;
    const int socketWorkerThreads = ConfigManager::instance().getInt(
        ConfigKeys::Server::SOCKET_WORKER_THREADS, qMin(4, idealThreads));
    const int dbWorkerThreads = ConfigManager::instance().getInt(
        ConfigKeys::Server::Database::WORKER_THREADS, qMin(4, idealThreads));

    const int heartbeatInterval = ConfigManager::instance().getInt(
        ConfigKeys::Server::HEARTBEAT_INTERVAL, 30000);
    const int heartbeatTimeout = ConfigManager::instance().getInt(
        ConfigKeys::Server::HEARTBEAT_TIMEOUT, 90000);

    LOG_WARN(QString("Server config - port=%1 max_connections=%2 socket_workers=%3 db_workers=%4 heartbeat_interval=%5ms heartbeat_timeout=%6ms")
                 .arg(port)
                 .arg(maxConnections)
                 .arg(socketWorkerThreads)
                 .arg(dbWorkerThreads)
                 .arg(heartbeatInterval)
                 .arg(heartbeatTimeout));

    TcpServer::instance().setMaxConnections(maxConnections);
    TcpServer::instance().setWorkerCount(socketWorkerThreads);
    TcpServer::instance().setHeartbeatTimeout(heartbeatTimeout);
    DbWorkerPool::instance().start(dbWorkerThreads);
    if (!DbWorkerPool::instance().healthCheck()) {
        LOG_ERROR("Database connection failed, server exiting");
        DbWorkerPool::instance().stop();
        Logger::shutdown();
        return -1;
    }
    ServerStats::instance().start(5000);

    if (TcpServer::instance().listen(QHostAddress::Any, port)) {
        LOG_WARN(QString("Server started, listening on port %1").arg(port));
    } else {
        LOG_ERROR("Server failed to start");
        return -1;
    }

    const int rc = a.exec();
    TcpServer::instance().shutdown();
    DbWorkerPool::instance().stop();
    Logger::shutdown();
    return rc;
}
