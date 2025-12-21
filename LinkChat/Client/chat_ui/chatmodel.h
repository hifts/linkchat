#ifndef CHATMODEL_H
#define CHATMODEL_H

#include "ChatData.h"
#include <QAbstractListModel>

// 负责存储 QList<ChatMessage>，并告诉 View 有多少行、每行数据显示什么

class ChatModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum ChatRoles{
        RoleIsMine = Qt::UserRole + 1,
        RoleContent,
        RoleAvatar,
        RoleType,           // 消息类型
        RoleImage,          // 图片数据
        RoleSenderName,     // 发送者用户名
        RoleIsGroupChat,    // 是否是群聊
        RoleEncryptionStatus // 加密状态
    };

    explicit ChatModel(QObject *parent = nullptr);

    // 重写函数
    int rowCount(const QModelIndex &parent) const;
    QVariant data(const QModelIndex &index, int role) const;

    void addMessage(const ChatMessage &msg);

    // 清空消息
    void clearMessages();

    // 获取当前聊天窗口的所有信息
    QList<ChatMessage> getMessages() const;

    // 设置新的消息列表（切换聊天好友时）
    void setMessages(const QList<ChatMessage> &list);

signals:

private:
    // 存储消息
    QList<ChatMessage> m_messages;
};

#endif // CHATMODEL_H
