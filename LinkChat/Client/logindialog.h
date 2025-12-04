#ifndef LOGINDIALOG_H
#define LOGINDIALOG_H

#include <QDialog>

namespace Ui {
class LoginDialog;
}

class LoginDialog : public QDialog
{
    Q_OBJECT

public:
    explicit LoginDialog(QWidget *parent = nullptr);
    ~LoginDialog();

    int loginUid() const;

protected:
    // 重写鼠标事件
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;

private slots:
    void on_btnReg_clicked();

    void on_btnLogin_clicked();

    void on_btnClose_clicked();

    void onSigLoginResult(bool success,int uid,int errorCode);

private:
    Ui::LoginDialog *ui;
    int m_loginUid = 0;         // 登录成功时保存
    QPoint m_dragPosition;      // 记录鼠标按下时的相对位置
};

#endif // LOGINDIALOG_H
