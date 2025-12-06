#include "networkmanager.h"
#include "logindialog.h"
#include "mainwindow.h"

#include <QApplication>
#include <QTimer>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    // 本机连接服务器
    NetworkManager::instance().connectToServer("127.0.0.1",8080);

    LoginDialog loginDlg;

    if(loginDlg.exec() == QDialog::Accepted){
        // 登录成功进入主页面
        MainWindow w;
        w.setCurrentUserId(loginDlg.loginUid());
        w.setCurrentUserName(loginDlg.loginUserName());
        w.show();

        return a.exec();
    }else{
        return 0;
    }
}
