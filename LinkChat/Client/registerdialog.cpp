#include "registerdialog.h"
#include "networkmanager.h"
#include "packet.h"
#include "encryptionmanager.h"
#include "ui_registerdialog.h"

#include <QMessageBox>
#include <QMouseEvent>
#include <QPainterPath>
#include <QDebug>
#include <QRegion>
#include <QResizeEvent>

RegisterDialog::RegisterDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::RegisterDialog)
{
    ui->setupUi(this);

    connect(&NetworkManager::instance(),&NetworkManager::sigRegisterResult,this,&RegisterDialog::onSigRegisterResult);

    this->setWindowFlags(Qt::FramelessWindowHint | Qt::Dialog);

    ui->frame->setObjectName("RegisterFrame");
    ui->btnOk->setObjectName("btnOk");
    ui->btnCancel->setObjectName("btnCancel");

    QString style = R"(
    /* 整体背景：保持与登录窗口一致的蓝紫渐变 */
    QDialog {
        background-color: #2b2f39;
    }

    /* 中间卡片：磨砂玻璃 */
    QFrame#RegisterFrame {
        background-color: #2b2f39;
        border: 1px solid #3a4050;
        border-radius: 16px;
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

    /* 输入框 */
    QLineEdit {
        background-color: #222631;
        border: 1px solid #3a4050;
        border-radius: 8px;
        color: #f4f7fb;
        padding: 10px 12px;
        font-family: "Microsoft YaHei";
        font-size: 14px;
        min-height: 20px;
        selection-background-color: #5865f2;
    }
    QLineEdit:focus {
        background-color: #252b36;
        border: 1px solid #5865f2;
    }
    QLineEdit::placeholder { color: #8e96a8; }

    /* 确认注册按钮 */
    QPushButton#btnOk {
        background-color: #27ae60; /* 注册用绿色，或者用 #3498db 蓝色也可以 */
        color: white;
        border-radius: 8px;
        font-size: 16px;
        font-family: "Microsoft YaHei";
        font-weight: bold;
        padding: 10px;
        border: none;
    }
    QPushButton#btnOk:hover { background-color: #2ecc71; }
    QPushButton#btnOk:pressed { background-color: #219150; }
    QPushButton#btnOk:disabled {
        background-color: #3f5f4c;
        color: #a8b8ae;
    }

    /* 取消/返回按钮 */
    QPushButton#btnCancel {
        background: transparent;
        color: #e8edf2;
        border: none;
        font-size: 14px;
        font-family: "Microsoft YaHei";
        padding: 8px;
    }
    QPushButton#btnCancel:hover {
        color: #ffffff;
        text-decoration: underline;
    }

    /* 关闭按钮 X */
    QPushButton#btnClose {
        color: #b9c0cf;
        background: transparent;
        font-weight: 900;
        font-family: "Arial";
        font-size: 16px;
        border: none;
        outline: none;
        border-radius: 4px;
    }
    QPushButton#btnClose:hover {
        color: #ffffff;
        background-color: #e74c3c;
    }
)";

    this->setStyleSheet(style);
    applyRoundedMask();
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

void RegisterDialog::resizeEvent(QResizeEvent *event)
{
    QDialog::resizeEvent(event);
    applyRoundedMask();
}

void RegisterDialog::applyRoundedMask()
{
    QPainterPath path;
    path.addRoundedRect(rect(), 16, 16);
    setMask(QRegion(path.toFillPolygon().toPolygon()));
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
    
    // 打包注册请求
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

