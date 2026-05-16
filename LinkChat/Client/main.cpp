#include "networkmanager.h"
#include "logindialog.h"
#include "registerdialog.h"
#include "mainwindow.h"
#include "logger.h"
#include "heartbeatmanager.h"
#include "reconnectmanager.h"
#include "filetransfermanager.h"
#include "filereceiver.h"
#include "configmanager.h"
#include "configkeys.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QTimer>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    const QString appDir = QCoreApplication::applicationDirPath();
    QDir::setCurrent(appDir);

    const QString logFilePath = QDir(appDir).filePath("client.log");
    const QString configFilePath = QDir(appDir).filePath("client_config.json");

    Logger::init(logFilePath, Logger::INFO);
    LOG_INFO(QString("Client startup directory: %1").arg(appDir));
    LOG_INFO(QString("Client config path: %1").arg(configFilePath));

    if (!ConfigManager::instance().initialize(configFilePath)) {
        LOG_ERROR("Failed to initialize config manager");
        return -1;
    }

    LOG_INFO("LinkChat client starting");

    NetworkManager &netMgr = NetworkManager::instance();

    HeartbeatManager *heartbeatMgr = netMgr.getHeartbeatManager();
    ReconnectManager *reconnectMgr = netMgr.getReconnectManager();

    int heartbeatInterval = ConfigManager::instance().getInt(
        ConfigKeys::Client::HEARTBEAT_INTERVAL, 5000);
    int heartbeatTimeout = ConfigManager::instance().getInt(
        ConfigKeys::Client::HEARTBEAT_TIMEOUT, 15000);

    heartbeatMgr->setHeartbeatInterval(heartbeatInterval);
    heartbeatMgr->setHeartbeatTimeout(heartbeatTimeout);
    heartbeatMgr->setMaxMissedHeartbeats(3);

    bool autoReconnect = ConfigManager::instance().getBool(
        ConfigKeys::Client::AUTO_RECONNECT, true);
    int reconnectMaxAttempts = ConfigManager::instance().getInt(
        ConfigKeys::Client::RECONNECT_MAX_ATTEMPTS, 0);
    int reconnectInterval = ConfigManager::instance().getInt(
        ConfigKeys::Client::RECONNECT_INTERVAL, 5000);

    reconnectMgr->setAutoConnect(autoReconnect);
    reconnectMgr->setInitialDelay(reconnectInterval);
    reconnectMgr->setMaxDelay(30000);
    reconnectMgr->setMaxAttempts(reconnectMaxAttempts);

    QString serverAddress = ConfigManager::instance().getString(
        ConfigKeys::Client::SERVER_ADDRESS, "127.0.0.1");
    int serverPort = ConfigManager::instance().getInt(
        ConfigKeys::Client::SERVER_PORT, 8080);

    QTimer::singleShot(0, &a, [&netMgr, serverAddress, serverPort]() {
        LOG_INFO(QString("Connecting to server %1:%2").arg(serverAddress).arg(serverPort));
        netMgr.connectToServer(serverAddress, serverPort);
    });

    const bool shouldQuitOnLastWindowClosed = a.quitOnLastWindowClosed();
    a.setQuitOnLastWindowClosed(false);

    while (true) {
        LoginDialog loginDlg;
        LOG_INFO("Login dialog created");

        const int loginResult = loginDlg.exec();

        if (loginResult == LoginDialog::RegisterRequested) {
            RegisterDialog registerDlg;
            registerDlg.move(loginDlg.frameGeometry().center()
                             - QPoint(registerDlg.width() / 2, registerDlg.height() / 2));
            registerDlg.exec();
            continue;
        }

        if(loginResult == QDialog::Accepted){
            LOG_INFO(QString("User login success, UID: %1, username: %2")
                     .arg(loginDlg.loginUid())
                     .arg(loginDlg.loginUserName()));

            reconnectMgr->saveLoginInfo(loginDlg.loginUserName(),loginDlg.loginCredentialHash());

            MainWindow w;
            w.setCurrentUserId(loginDlg.loginUid());
            w.setCurrentUserName(loginDlg.loginUserName());

            NetworkManager::instance().setCurrentUserId(loginDlg.loginUid());

            FileTransferManager::instance().setCurrentUserId(loginDlg.loginUid());

            FileReceiver::instance().setCurrentUserId(loginDlg.loginUid());

            w.show();
            a.setQuitOnLastWindowClosed(shouldQuitOnLastWindowClosed);

            return a.exec();
        }

        LOG_INFO("User canceled login, exiting");
        a.setQuitOnLastWindowClosed(shouldQuitOnLastWindowClosed);
        netMgr.disconnectFromServer();
        return 0;
    }
}
