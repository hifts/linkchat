#ifndef LOGINDIALOG_H
#define LOGINDIALOG_H

#include <QDialog>
#include <QByteArray>

namespace Ui {
class LoginDialog;
}

class LoginDialog : public QDialog
{
    Q_OBJECT

public:
    enum DialogResult {
        RegisterRequested = QDialog::Accepted + 1
    };

    explicit LoginDialog(QWidget *parent = nullptr);
    ~LoginDialog();

    int loginUid() const;
    QString loginUserName() const;

    QString loginCredentialHash() const;

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private slots:
    void on_btnReg_clicked();

    void on_btnLogin_clicked();

    void on_btnClose_clicked();

    void onSigLoginResult(bool success,int uid,int errorCode);
    void onSigLoginSaltReceived(bool success, const QByteArray &saltBase64);

private:
    Ui::LoginDialog *ui;
    int m_loginUid = 0;
    QString m_loginUserName;
    QString m_loginCredentialHash;
    QString m_pendingUserName;
    QString m_pendingPassword;
    QString m_pendingCredentialHash;
    bool m_waitingSalt = false;
    QPoint m_dragPosition;

    void applyRoundedMask();
};

#endif // LOGINDIALOG_H
