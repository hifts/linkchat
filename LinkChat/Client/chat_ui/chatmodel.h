#ifndef CHATMODEL_H
#define CHATMODEL_H

#include "ChatData.h"
#include <QAbstractListModel>


class ChatModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum ChatRoles{
        RoleIsMine = Qt::UserRole + 1,
        RoleContent,
        RoleAvatar,
        RoleType,
        RoleImage,
        RoleSenderName,
        RoleIsGroupChat,
        RoleEncryptionStatus,
        RoleTimestamp
    };

    explicit ChatModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent) const;
    QVariant data(const QModelIndex &index, int role) const;

    void addMessage(const ChatMessage &msg);

    void clearMessages();

    QList<ChatMessage> getMessages() const;

    void setMessages(const QList<ChatMessage> &list);

signals:

private:
    QList<ChatMessage> m_messages;
};

#endif // CHATMODEL_H
