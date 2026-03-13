#ifndef GROUPDIALOG_H
#define GROUPDIALOG_H

#include <QDialog>
#include <QListWidgetItem>

namespace Ui {
class GroupDialog;
}

struct FriendSelectInfo {
    int id;
    QString name;
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
    QPoint m_dragPosition;
    QList<FriendSelectInfo> m_friends;
    QList<int> m_selectFriendIds;

    void initUI();

};

#endif // GROUPDIALOG_H
