#include "logindialog.h"
#include "networkmanager.h"
#include "packet.h"
#include "ui_logindialog.h"
#include "registerdialog.h"

#include <QMessageBox>
#include <QMouseEvent>

LoginDialog::LoginDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::LoginDialog)
{
    ui->setupUi(this);

    connect(&NetworkManager::instance(),&NetworkManager::sigLoginResult,this,&LoginDialog::onSigLoginResult);

    // 给控件起名（为了防止样式冲突，确保你在 Designer 里也是这些名字）
    ui->lblTitle->setObjectName("lblTitle");
    ui->btnClose->setObjectName("btnClose");
    ui->btnLogin->setObjectName("btnLogin");
    ui->btnReg->setObjectName("btnReg");
    ui->LoginFrame->setObjectName("LoginFrame");
    // 去掉窗口边框，看起来更现代
    this->setWindowFlags(Qt::FramelessWindowHint);
    this->setAttribute(Qt::WA_TranslucentBackground);


    QString style = R"(
    /* 1. 整体窗口背景：搞一个高级的蓝紫渐变色，不用找图了 */
    QDialog {
        background-color: qlineargradient(spread:pad, x1:0, y1:0, x2:1, y2:1, stop:0 #2c3e50, stop:1 #4ca1af);
    }

    /* 2. 中间卡片：磨砂玻璃质感 */
    QFrame#LoginFrame {
        background-color: rgba(0, 0, 0, 0.6); /* 纯黑背景，60%透明度 */
        border-radius: 15px;
        border: 1px solid rgba(255, 255, 255, 0.1); /* 微微的白边，增加立体感 */
    }

    /* 3. 标题 LinkChat：核心整容 */
    QLabel#lblTitle {
        font-family: "Microsoft YaHei", "Segoe UI", sans-serif; /* 强制使用无衬线现代字体 */
        font-size: 28px;       /* 字号适中 */
        font-weight: bold;     /* 加粗 */
        color: #ffffff;        /* 纯白文字 */
        background-color: transparent;
        margin-bottom: 10px;   /* 下方留点空隙 */
        letter-spacing: 2px;   /* 字间距拉开一点点，显得高级 */
    }

    /* 4. 输入框：极简风格 */
    QLineEdit {
        background-color: rgba(255, 255, 255, 0.1); /* 微微发白的背景 */
        border: none;
        border-radius: 5px;    /* 小圆角 */
        color: #ffffff;        /* 文字白色 */
        padding: 8px 10px;     /* 内部留白，不要让文字顶着框 */
        font-family: "Microsoft YaHei";
        font-size: 14px;
        selection-background-color: #3498db;
    }
    QLineEdit:focus {
        background-color: rgba(255, 255, 255, 0.2); /* 选中时稍微亮一点 */
        border: 1px solid #3498db; /* 出现蓝色细边框 */
    }
    /* 输入框里的提示文字颜色 (Placeholder) */
    QLineEdit::placeholder {
        color: #aaaaaa;
    }

    /* 5. 登录按钮：去掉了丑陋的虚线框 */
    QPushButton#btnLogin {
        background-color: #3498db;
        color: white;
        border-radius: 5px;
        font-size: 16px;
        font-family: "Microsoft YaHei";
        font-weight: bold;
        padding: 8px;
        border: none;
        outline: none; /* 【关键】去掉点击时的虚线框 */
    }
    QPushButton#btnLogin:hover {
        background-color: #5dade2;
    }
    QPushButton#btnLogin:pressed {
        background-color: #2980b9;
        padding-top: 10px; /* 简单的按下微动效 */
        padding-bottom: 6px;
    }

    /* 6. 注册按钮：文字链接风 */
    QPushButton#btnReg {
        background: transparent;
        color: #dddddd;
        border: none;
        font-size: 12px;
        font-family: "Microsoft YaHei";
    }
    QPushButton#btnReg:hover {
        color: #ffffff;
        text-decoration: underline;
    }

    /* 7. 关闭按钮：放在右上角的 X */
    QPushButton#btnClose {
        color: rgba(255, 255, 255, 0.7);
        background: transparent;
        font-weight: 900; /* 特别粗 */
        font-family: "Arial";
        font-size: 16px;
        border: none;
    }
    QPushButton#btnClose:hover {
        color: #e74c3c; /* 悬停变红 */
    }
)";

    this->setStyleSheet(style);

}

LoginDialog::~LoginDialog()
{
    delete ui;
}

void LoginDialog::mousePressEvent(QMouseEvent *event)
{
    // 只有左键点击才能拖动
    if (event->button() == Qt::LeftButton) {
        // 计算鼠标相对于窗口左上角的偏移量
        m_dragPosition = event->globalPos() - frameGeometry().topLeft();
        event->accept();
    }
}

void LoginDialog::mouseMoveEvent(QMouseEvent *event)
{
    // 只有按住左键移动才处理
    if (event->buttons() & Qt::LeftButton) {
        // 移动窗口到鼠标当前位置减去偏移量
        move(event->globalPos() - m_dragPosition);
        event->accept();
    }
}

void LoginDialog::on_btnReg_clicked()
{
    RegisterDialog regDlg(this);

    regDlg.exec();
}


void LoginDialog::on_btnLogin_clicked()
{
    QString user = ui->editUser->text().trimmed();
    QString pwd = ui->editPwd->text();

    if(user.isEmpty() || pwd.isEmpty()){
        QMessageBox::warning(this,"提示","账号或密码不能为空");
        return;
    }

    // 打包（登陆包）
    LoginReq req;
    memset(&req,0,sizeof(LoginReq));
    strncpy(req.userName,user.toStdString().c_str(),32);
    strncpy(req.password,pwd.toStdString().c_str(),32);

    // 请求服务端登录
    NetworkManager::instance().sendMsg(MSG_LOGIN_REQ,QByteArray((char*)&req,sizeof(LoginReq)));
}


void LoginDialog::on_btnClose_clicked()
{
    this->reject();
}

void LoginDialog::onSigLoginResult(bool success, int uid,int errorCode)
{
    if(success){
        // 保存用户id,用户名,密码
        m_loginUid = uid;
        m_loginUserName = ui->editUser->text().trimmed();
        m_loginPassword = ui->editPwd->text().trimmed();
        qDebug()<<"登陆成功，uid:"<<uid;
        // QMessageBox::information(this,"提示","登录成功");
        this->accept();
    }else{
        QString msg;
        if(errorCode == 2){
            msg = "该账号已在其他设备登录，禁止多端登录！";
        }else{
            msg = "用户名或密码错误";
        }
        QMessageBox::warning(this,"登录失败",msg);
    }
}

QString LoginDialog::loginPassword() const
{
    return m_loginPassword;
}

int LoginDialog::loginUid() const
{
    return m_loginUid;
}

QString LoginDialog::loginUserName() const
{
    return m_loginUserName;
}

