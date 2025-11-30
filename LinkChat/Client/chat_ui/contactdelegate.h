#ifndef CONTACTDELEGATE_H
#define CONTACTDELEGATE_H

#include <QStyledItemDelegate>

class ContactDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    // 定义自定义数据的 Role
    enum ContactRoles {
        RoleStatus = Qt::UserRole,          // 存ID
        RoleName = Qt::UserRole + 1,        // 存名字
        RoleUnread = Qt::UserRole + 2,      // 存未读消息数
        RoleIsFriend = Qt::UserRole + 3     // 是否是好友
    };

    explicit ContactDelegate(QObject *parent = nullptr);

public:
    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const;

signals:
};

#endif // CONTACTDELEGATE_H
