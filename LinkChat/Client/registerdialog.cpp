#include "registerdialog.h"
#include "networkmanager.h"
#include "packet.h"
#include "ui_registerdialog.h"

#include <QMessageBox>
#include <QMouseEvent>

RegisterDialog::RegisterDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::RegisterDialog)
{
    ui->setupUi(this);

    connect(&NetworkManager::instance(),&NetworkManager::sigRegisterResult,this,&RegisterDialog::onSigRegisterResult);

    // 1. 窗口属性：无边框 + 透明
    this->setWindowFlags(Qt::FramelessWindowHint | Qt::Dialog);
    this->setAttribute(Qt::WA_TranslucentBackground);

    // 2. 确保对象名匹配 (防止 Designer 里名字不对)
    ui->frame->setObjectName("RegisterFrame");
    ui->btnOk->setObjectName("btnOk");
    ui->btnCancel->setObjectName("btnCancel");

    // 3. 应用 QSS (直接复用 Login 的高级渐变)
    QString style = R"(
    /* 整体背景：保持与登录窗口一致的蓝紫渐变 */
    QDialog {
        background-color: qlineargradient(spread:pad, x1:0, y1:0, x2:1, y2:1, stop:0 #2c3e50, stop:1 #4ca1af);
    }

    /* 中间卡片：磨砂玻璃 */
    QFrame#RegisterFrame {
        background-color: rgba(0, 0, 0, 0.6);
        border-radius: 15px;
        border: 1px solid rgba(255, 255, 255, 0.1);
    }

    /* 标题 */
    QLabel {
        font-family: "Microsoft YaHei";
        font-size: 26px;
        font-weight: bold;
        color: #ffffff;
        background-color: transparent;
        margin-bottom: 10px;
    }

    /* 输入框 (3个都生效) */
    QLineEdit {
        background-color: rgba(255, 255, 255, 0.1);
        border: none;
        border-radius: 5px;
        color: #ffffff;
        padding: 8px 10px;
        font-family: "Microsoft YaHei";
        font-size: 14px;
    }
    QLineEdit:focus {
        background-color: rgba(255, 255, 255, 0.2);
        border: 1px solid #3498db;
    }
    QLineEdit::placeholder { color: #aaaaaa; }

    /* 确认注册按钮 (主按钮) */
    QPushButton#btnOk {
        background-color: #27ae60; /* 注册用绿色，或者用 #3498db 蓝色也可以 */
        color: white;
        border-radius: 5px;
        font-size: 16px;
        font-family: "Microsoft YaHei";
        font-weight: bold;
        padding: 8px;
        border: none;
    }
    QPushButton#btnOk:hover { background-color: #2ecc71; }
    QPushButton#btnOk:pressed { background-color: #219150; padding-top: 10px; }

    /* 取消/返回按钮 (文字链接风格) */
    QPushButton#btnCancel {
        background: transparent;
        color: #dddddd;
        border: none;
        font-size: 14px;
        font-family: "Microsoft YaHei";
    }
    QPushButton#btnCancel:hover {
        color: #ffffff;
        text-decoration: underline;
    }

    /* 关闭按钮 X */
    QPushButton#btnClose {
        color: rgba(255, 255, 255, 0.7);
        background: transparent;
        font-weight: 900;
        font-family: "Arial";
        font-size: 16px;
        border: none;
    }
    QPushButton#btnClose:hover { color: #e74c3c; }
)";

    this->setStyleSheet(style);

}

RegisterDialog::~RegisterDialog()
{
    delete ui;
}

void RegisterDialog::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragPosition = event->globalPos() - frameGeometry().topLeft();
        event->accept();
    }
}

void RegisterDialog::mouseMoveEvent(QMouseEvent *event)
{
    if (event->buttons() & Qt::LeftButton) {
        move(event->globalPos() - m_dragPosition);
        event->accept();
    }
}


void RegisterDialog::on_btnCancel_clicked()
{
    this->reject();
}


void RegisterDialog::on_btnOk_clicked()
{
    QString user = ui->editUser->text();
    QString pwd = ui->editPwd->text();
    QString confirmPwd = ui->editConfirm->text();

    if(user.isEmpty() || pwd.isEmpty()){
        QMessageBox::warning(this,"提示","账号或密码不能为空");
        return;
    }

    if(pwd != confirmPwd){
        QMessageBox::warning(this,"提示","两次输入的密码不一致");
        return;
    }

    // 打包（登陆/注册包）
    LoginReq req;
    memset(&req,0,sizeof(LoginReq));        // 清空内存
    strncpy(req.userName,user.toStdString().c_str(),32);
    strncpy(req.password,pwd.toStdString().c_str(),32);

    // 发送请求给服务器
    NetworkManager::instance().sendMsg(MSG_REGISTER_REQ,QByteArray((char*)&req,sizeof(LoginReq)));
}


void RegisterDialog::on_btnClose_clicked()
{
    this->reject();
}

void RegisterDialog::onSigRegisterResult(bool success)
{
    if(success){
        QMessageBox::information(this,"提示","注册成功！\n请返回登录");
        this->accept();
    }else{
        QMessageBox::critical(this,"错误","注册失败！\n用户可能已存在");
    }
}

