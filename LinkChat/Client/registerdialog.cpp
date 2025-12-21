#include "registerdialog.h"
#include "networkmanager.h"
#include "packet.h"
#include "encryptionmanager.h"
#include "ui_registerdialog.h"

#include <QMessageBox>
#include <QMouseEvent>
#include <QDebug>

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

    // 生成随机盐值
    QByteArray salt = EncryptionManager::instance().generateSalt();
    if(salt.isEmpty()){
        qCritical() << "[RegisterDialog] Failed to generate salt";
        QMessageBox::critical(this,"错误","生成盐值失败，请重试");
        return;
    }
    
    // 对密码进行哈希
    QByteArray passwordHash = EncryptionManager::instance().hashPassword(pwd, salt);
    if(passwordHash.isEmpty()){
        qCritical() << "[RegisterDialog] Failed to hash password";
        QMessageBox::critical(this,"错误","密码哈希失败，请重试");
        return;
    }
    
    // 打包注册请求（使用新的RegisterReq结构）
    RegisterReq req;
    memset(&req, 0, sizeof(RegisterReq));
    
    // 复制用户名
    strncpy(req.userName, user.toStdString().c_str(), 31);
    req.userName[31] = '\0';
    
    // Base64编码哈希值和盐值
    QByteArray hashBase64 = passwordHash.toBase64();
    QByteArray saltBase64 = salt.toBase64();
    
    // 确保不超过字段限制
    if(hashBase64.length() >= 64){
        qWarning() << "[RegisterDialog] Password hash too long:" << hashBase64.length();
        hashBase64 = hashBase64.left(63);
    }
    if(saltBase64.length() >= 64){
        qWarning() << "[RegisterDialog] Salt too long:" << saltBase64.length();
        saltBase64 = saltBase64.left(63);
    }
    
    // 复制Base64编码的哈希值和盐值
    strncpy(req.passwordHash, hashBase64.constData(), 63);
    req.passwordHash[63] = '\0';
    strncpy(req.salt, saltBase64.constData(), 63);
    req.salt[63] = '\0';
    
    // 发送请求给服务器
    NetworkManager::instance().sendMsg(MSG_REGISTER_REQ, QByteArray((char*)&req, sizeof(RegisterReq)));
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

