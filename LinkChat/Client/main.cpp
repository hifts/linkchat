#include "networkmanager.h"
#include "logindialog.h"
#include "mainwindow.h"
#include "logger.h"
#include "heartbeatmanager.h"
#include "reconnectmanager.h"
#include "filetransfermanager.h"

#include <QApplication>
#include <QTimer>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    // 初始化日志系统
    Logger::init("client.log", Logger::DEBUG);
    LOG_INFO("LinkChat 客户端启动");

    NetworkManager &netMgr = NetworkManager::instance();

    HeartbeatManager *heartbeatMgr = netMgr.getHeartbeatManager();
    ReconnectManager *reconnectMgr = netMgr.getReconnectManager();

    // 配置心跳参数
    heartbeatMgr->setHeartbeatInterval(5000);       // 5秒发送一次心跳
    heartbeatMgr->setHeartbeatTimeout(15000);       // 15秒未响应视为超时
    heartbeatMgr->setMaxMissedHeartbeats(3);

    // 配置重连参数
    reconnectMgr->setAutoConnect(true);
    reconnectMgr->setInitialDelay(5);
    reconnectMgr->setMaxDelay(30000);
    reconnectMgr->setMaxAttempts(0);

    // 本机连接服务器
    netMgr.connectToServer("127.0.0.1",8080);

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
