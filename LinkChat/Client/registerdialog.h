#ifndef REGISTERDIALOG_H
#define REGISTERDIALOG_H

#include <QDialog>

namespace Ui {
class RegisterDialog;
}

class RegisterDialog : public QDialog
{
    Q_OBJECT

public:
    explicit RegisterDialog(QWidget *parent = nullptr);
    ~RegisterDialog();

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;


private slots:

    void on_btnCancel_clicked();

    void on_btnOk_clicked();

    void on_btnClose_clicked();

    void onSigRegisterResult(bool success);
private:
    Ui::RegisterDialog *ui;
    QPoint m_dragPosition;
};

#endif // REGISTERDIALOG_H
