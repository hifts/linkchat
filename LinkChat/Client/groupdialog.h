#ifndef GROUPDIALOG_H
#define GROUPDIALOG_H

#include <QDialog>
#include <QListWidgetItem>

namespace Ui {
class GroupDialog;
}

// 定义一个简单的结构体传参用
struct FriendSelectInfo {
    int id;
    QString name;
    // QIcon avatar; // 头像
};

class GroupDialog : public QDialog
{
    Q_OBJECT

public:
    explicit GroupDialog(const QList<FriendSelectInfo>& friends,QWidget *parent = nullptr);
    ~GroupDialog();

    QString getGroupName() const;
    QList<int> getSelctFriendIds() const;
protected:
    // 重写鼠标事件
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;

private slots:
    void updateSelectionCount();
    void onSearchTextChanged(const QString &text);

    void on_btnCancel_clicked();

    void on_btnClose_clicked();

    void on_btnCreate_clicked();

private:
    Ui::GroupDialog *ui;
    QPoint m_dragPosition;      // 记录鼠标按下时的相对位置
    QList<FriendSelectInfo> m_friends;
    QList<int> m_selectFriendIds;

    void initUI();

};

#endif // GROUPDIALOG_H
