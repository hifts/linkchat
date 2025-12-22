#include "networkmanager.h"
#include "logindialog.h"
#include "mainwindow.h"
#include "logger.h"
#include "heartbeatmanager.h"
#include "reconnectmanager.h"
#include "filetransfermanager.h"
#include "configmanager.h"
#include "configkeys.h"

#include <QApplication>
#include <QTimer>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    // 初始化配置管理器
    if (!ConfigManager::instance().initialize("client_config.json")) {
        LOG_ERROR("Failed to initialize config manager");
        return -1;
    }

    // 初始化日志系统
    Logger::init("client.log", Logger::DEBUG);
    LOG_INFO("LinkChat 客户端启动");

    NetworkManager &netMgr = NetworkManager::instance();

    HeartbeatManager *heartbeatMgr = netMgr.getHeartbeatManager();
    ReconnectManager *reconnectMgr = netMgr.getReconnectManager();

    // 从配置文件读取心跳参数
    int heartbeatInterval = ConfigManager::instance().getInt(
        ConfigKeys::Client::HEARTBEAT_INTERVAL, 5000);
    int heartbeatTimeout = ConfigManager::instance().getInt(
        ConfigKeys::Client::HEARTBEAT_TIMEOUT, 15000);

    // 配置心跳参数
    heartbeatMgr->setHeartbeatInterval(heartbeatInterval);
    heartbeatMgr->setHeartbeatTimeout(heartbeatTimeout);
    heartbeatMgr->setMaxMissedHeartbeats(3);

    // 从配置文件读取重连参数
    bool autoReconnect = ConfigManager::instance().getBool(
        ConfigKeys::Client::AUTO_RECONNECT, true);
    int reconnectMaxAttempts = ConfigManager::instance().getInt(
        ConfigKeys::Client::RECONNECT_MAX_ATTEMPTS, 0);
    int reconnectInterval = ConfigManager::instance().getInt(
        ConfigKeys::Client::RECONNECT_INTERVAL, 5000);

    // 配置重连参数
    reconnectMgr->setAutoConnect(autoReconnect);
    reconnectMgr->setInitialDelay(reconnectInterval / 1000);  // 转换为秒
    reconnectMgr->setMaxDelay(30000);
    reconnectMgr->setMaxAttempts(reconnectMaxAttempts);

    // 从配置文件读取服务器地址和端口
    QString serverAddress = ConfigManager::instance().getString(
        ConfigKeys::Client::SERVER_ADDRESS, "127.0.0.1");
    int serverPort = ConfigManager::instance().getInt(
        ConfigKeys::Client::SERVER_PORT, 8080);

    // 连接到服务器
    netMgr.connectToServer(serverAddress, serverPort);

    LoginDialog loginDlg;
    if(loginDlg.exec() == QDialog::Accepted){
        // 登录成功进入主页面
        LOG_INFO(QString("用户登录成功，UID: %1, 用户名: %2")
                 .arg(loginDlg.loginUid())
                 .arg(loginDlg.loginUserName()));
        
        // 保存登录信息用于后面自动登录
        reconnectMgr->saveLoginInfo(loginDlg.loginUserName(),loginDlg.loginPassword());

        MainWindow w;
        w.setCurrentUserId(loginDlg.loginUid());
        w.setCurrentUserName(loginDlg.loginUserName());
        
        // 设置网络管理器的当前用户ID（用于消息加密）
        NetworkManager::instance().setCurrentUserId(loginDlg.loginUid());
        
        // 设置文件传输管理器的当前用户ID（用于文件加密）
        FileTransferManager::instance().setCurrentUserId(loginDlg.loginUid());
        
        w.show();

        return a.exec();
    }else{
        LOG_INFO("用户取消登录，程序退出");
        netMgr.disconnectFromServer();
        return 0;
    }
}
