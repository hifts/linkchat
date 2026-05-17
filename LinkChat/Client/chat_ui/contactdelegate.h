#ifndef CONTACTDELEGATE_H
#define CONTACTDELEGATE_H

#include <QStyledItemDelegate>

class ContactDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    enum ContactRoles {
        RoleStatus = Qt::UserRole,
        RoleName = Qt::UserRole + 1,
        RoleUnread = Qt::UserRole + 2,
        RoleIsFriend = Qt::UserRole + 3,
        RoleLastMsgTime = Qt::UserRole + 4,
        RoleShowTime = Qt::UserRole + 5,
        RoleOnlineStatus = Qt::UserRole + 6,
        RoleLastMessagePreview = Qt::UserRole + 7
    };

    explicit ContactDelegate(QObject *parent = nullptr);

public:
    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const;

signals:
};

#endif // CONTACTDELEGATE_H
