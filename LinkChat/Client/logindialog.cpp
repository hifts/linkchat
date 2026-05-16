#include "logindialog.h"

#include "encryptionmanager.h"
#include "networkmanager.h"
#include "packet.h"
#include "ui_logindialog.h"

#include <QLineEdit>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainterPath>
#include <QRegion>
#include <QResizeEvent>
#include <cstring>

LoginDialog::LoginDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::LoginDialog)
{
    ui->setupUi(this);

    connect(&NetworkManager::instance(), &NetworkManager::sigLoginResult,
            this, &LoginDialog::onSigLoginResult);
    connect(&NetworkManager::instance(), &NetworkManager::sigLoginSaltReceived,
            this, &LoginDialog::onSigLoginSaltReceived);

    ui->btnClose->setAutoDefault(false);
    ui->btnClose->setDefault(false);
    ui->btnReg->setAutoDefault(false);
    ui->btnReg->setDefault(false);
    ui->btnLogin->setAutoDefault(false);
    ui->btnLogin->setDefault(false);

    connect(ui->editUser, &QLineEdit::returnPressed, this, [this]() {
        if (ui->editPwd->text().isEmpty()) {
            ui->editPwd->setFocus();
            return;
        }
        on_btnLogin_clicked();
    });

    connect(ui->editPwd, &QLineEdit::returnPressed, this, [this]() {
        on_btnLogin_clicked();
    });

    setWindowFlags(Qt::FramelessWindowHint);

    ui->LoginFrame->setObjectName("LoginFrame");
    ui->btnLogin->setObjectName("btnLogin");
    ui->btnReg->setObjectName("btnReg");

    const QString style = R"(
    QDialog {
        background-color: #2b2f39;
    }

    QFrame#LoginFrame {
        background-color: #2b2f39;
        border: 1px solid #3a4050;
        border-radius: 16px;
    }

    QLabel#lblTitle {
        font-family: "Microsoft YaHei";
        font-size: 28px;
        font-weight: bold;
        color: #ffffff;
        background: transparent;
    }

    QLineEdit {
        background-color: #222631;
        border: 1px solid #3a4050;
        border-radius: 8px;
        color: #f4f7fb;
        padding: 10px 12px;
        font-family: "Microsoft YaHei";
        font-size: 14px;
        selection-background-color: #5865f2;
    }

    QLineEdit:focus {
        background-color: #252b36;
        border: 1px solid #5865f2;
    }

    QLineEdit::placeholder {
        color: #8e96a8;
    }

    QPushButton#btnLogin {
        background-color: #5865f2;
        color: white;
        border: none;
        border-radius: 8px;
        padding: 10px;
        font-family: "Microsoft YaHei";
        font-size: 18px;
        font-weight: bold;
    }

    QPushButton#btnLogin:hover {
        background-color: #6772f4;
    }

    QPushButton#btnLogin:pressed {
        background-color: #4752c4;
    }

    QPushButton#btnLogin:disabled {
        background-color: #444b62;
        color: #9aa3b8;
    }

    QPushButton#btnReg {
        background: transparent;
        color: #e8edf2;
        border: none;
        font-family: "Microsoft YaHei";
        font-size: 13px;
        padding: 6px;
    }

    QPushButton#btnReg:hover {
        color: #ffffff;
        text-decoration: underline;
    }

    QPushButton#btnClose {
        color: #b9c0cf;
        background: transparent;
        border: none;
        outline: none;
        font-weight: 900;
        font-family: "Arial";
        font-size: 16px;
        border-radius: 4px;
    }

    QPushButton#btnClose:hover {
        color: #ffffff;
        background-color: #e74c3c;
    }
    )";

    setStyleSheet(style);
    applyRoundedMask();
}

LoginDialog::~LoginDialog()
{
    delete ui;
}

void LoginDialog::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragPosition = event->globalPos() - frameGeometry().topLeft();
        event->accept();
    }
}

void LoginDialog::mouseMoveEvent(QMouseEvent *event)
{
    if (event->buttons() & Qt::LeftButton) {
        move(event->globalPos() - m_dragPosition);
        event->accept();
    }
}

void LoginDialog::resizeEvent(QResizeEvent *event)
{
    QDialog::resizeEvent(event);
    applyRoundedMask();
}

void LoginDialog::applyRoundedMask()
{
    QPainterPath path;
    path.addRoundedRect(rect(), 16, 16);
    setMask(QRegion(path.toFillPolygon().toPolygon()));
}

void LoginDialog::on_btnReg_clicked()
{
    done(RegisterRequested);
}

void LoginDialog::on_btnLogin_clicked()
{
    if (m_waitingSalt) {
        return;
    }

    const QString user = ui->editUser->text().trimmed();
    const QString pwd = ui->editPwd->text();
    if (user.isEmpty() || pwd.isEmpty()) {
        QMessageBox::warning(this, "Login", "Username or password cannot be empty.");
        return;
    }

    LoginSaltReq req;
    std::memset(&req, 0, sizeof(LoginSaltReq));
    std::strncpy(req.userName, user.toUtf8().constData(), sizeof(req.userName) - 1);

    m_pendingUserName = user;
    m_pendingPassword = pwd;
    m_pendingCredentialHash.clear();
    m_waitingSalt = true;
    ui->btnLogin->setEnabled(false);

    NetworkManager::instance().sendMsg(MSG_LOGIN_SALT_REQ, QByteArray((char*)&req, sizeof(LoginSaltReq)));
}

void LoginDialog::on_btnClose_clicked()
{
    reject();
}

void LoginDialog::onSigLoginSaltReceived(bool success, const QByteArray &saltBase64)
{
    if (!m_waitingSalt) {
        return;
    }

    if (!success) {
        m_waitingSalt = false;
        ui->btnLogin->setEnabled(true);
        QMessageBox::warning(this, "Login Failed", "Failed to fetch login salt.");
        return;
    }

    const QByteArray salt = QByteArray::fromBase64(saltBase64);
    if (salt.isEmpty()) {
        m_waitingSalt = false;
        ui->btnLogin->setEnabled(true);
        QMessageBox::warning(this, "Login Failed", "Server returned invalid salt.");
        return;
    }

    const QByteArray passwordHash = EncryptionManager::instance().hashPassword(m_pendingPassword, salt);
    if (passwordHash.isEmpty()) {
        m_waitingSalt = false;
        ui->btnLogin->setEnabled(true);
        QMessageBox::warning(this, "Login Failed", "Local password hash failed.");
        return;
    }

    QByteArray hashBase64 = passwordHash.toBase64();
    if (hashBase64.size() >= 64) {
        hashBase64 = hashBase64.left(63);
    }

    LoginReq req;
    std::memset(&req, 0, sizeof(LoginReq));
    std::strncpy(req.userName, m_pendingUserName.toUtf8().constData(), sizeof(req.userName) - 1);
    std::strncpy(req.passwordHash, hashBase64.constData(), sizeof(req.passwordHash) - 1);

    m_pendingCredentialHash = QString::fromLatin1(hashBase64);
    NetworkManager::instance().sendMsg(MSG_LOGIN_REQ, QByteArray((char*)&req, sizeof(LoginReq)));
}

void LoginDialog::onSigLoginResult(bool success, int uid, int errorCode)
{
    m_waitingSalt = false;
    ui->btnLogin->setEnabled(true);

    if (success) {
        m_loginUid = uid;
        m_loginUserName = m_pendingUserName.isEmpty() ? ui->editUser->text().trimmed() : m_pendingUserName;
        m_loginCredentialHash = m_pendingCredentialHash;
        accept();
        return;
    }

    if (errorCode == 2) {
        QMessageBox::warning(this, "Login Failed", "This account is already online on another device.");
        return;
    }
    QMessageBox::warning(this, "Login Failed", "Invalid username or password.");
}

QString LoginDialog::loginCredentialHash() const
{
    return m_loginCredentialHash;
}

int LoginDialog::loginUid() const
{
    return m_loginUid;
}

QString LoginDialog::loginUserName() const
{
    return m_loginUserName;
}
