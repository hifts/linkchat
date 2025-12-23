#include "chatmodel.h"

ChatModel::ChatModel(QObject *parent)
    : QAbstractListModel{parent}
{}

int ChatModel::rowCount(const QModelIndex &parent) const
{
    // 告诉模型需要显示的行数(有多少条信息需要显示)
    if(parent.isValid()){
        return 0;
    }
    return m_messages.size();
}

QVariant ChatModel::data(const QModelIndex &index, int role) const
{
    if(!index.isValid() || index.row() >= m_messages.size()){
        return QVariant();
    }

    const ChatMessage &msg = m_messages.at(index.row());

    switch (role) {
    case RoleIsMine:
        return msg.isMine;
    case RoleContent:
        return msg.content;
    case RoleAvatar:
        return msg.avatarPath;
    case RoleType:
        return msg.type;
    case RoleImage:
        return msg.image;
    case RoleSenderName:
        return msg.senderName;
    case RoleIsGroupChat:
        return msg.isGroupChat;
    case RoleEncryptionStatus:
        return msg.encryptionStatus;
    case RoleTimestamp:
        return msg.timestamp;
    default:
        return QVariant();
    }
}

void ChatModel::addMessage(const ChatMessage &msg)
{
    // 告诉 View 我们要加数据了，View 会自动刷新
    beginInsertRows(QModelIndex(),m_messages.size(),m_messages.size());

    m_messages.append(msg);

    endInsertRows();

}

void ChatModel::clearMessages()
{
    beginResetModel();
    m_messages.clear();
    endResetModel();
}

QList<ChatMessage> ChatModel::getMessages() const
{
    return m_messages;
}

void ChatModel::setMessages(const QList<ChatMessage> &list)
{
    beginResetModel();
    m_messages = list;
    endResetModel();
}
