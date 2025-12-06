#ifndef CHATDATA_H
#define CHATDATA_H

#include <QString>
#include <QDateTime>
#include <QPixmap>

//  Model 和 Delegate 之间传递数据的载体

enum MsgType {
    TypeText,
    TypeImage
};

struct ChatMessage{
    MsgType type;           // 消息类型（文本/图片）
    QString content;        // 消息内容
    QByteArray image;       // 图片
    bool isMine;            // true=我发的（右边），false=对方发的（左边）
    QString avatarPath;     // 头像路径
    quint64 timestamp;      // 时间戳
    QString senderName;     // 发送者用户名（群聊用）
    bool isGroupChat;       // 是否是群聊消息

    // 构造文本消息（私聊）
    ChatMessage(const QString &msg, bool mine, const QString &avatar)
        : type(TypeText), content(msg), isMine(mine), avatarPath(avatar), 
          isGroupChat(false) {}

    // 构造图片消息（私聊）
    ChatMessage(const QByteArray &rawImageData, bool mine, const QString &avatar)
        : type(TypeImage), image(rawImageData), isMine(mine), avatarPath(avatar),
          isGroupChat(false) {}

    // 构造群聊文本消息（包含发送者用户名）
    ChatMessage(const QString &msg, bool mine, const QString &avatar, const QString &sender)
        : type(TypeText), content(msg), isMine(mine), avatarPath(avatar), 
          senderName(sender), isGroupChat(true) {}

    // 构造群聊图片消息（包含发送者用户名）
    ChatMessage(const QByteArray &rawImageData, bool mine, const QString &avatar, const QString &sender)
        : type(TypeImage), image(rawImageData), isMine(mine), avatarPath(avatar),
          senderName(sender), isGroupChat(true) {}

    // 空构造（给 QList 用）
    ChatMessage() : type(TypeText), isMine(false), timestamp(0), isGroupChat(false) {}
};

#endif // CHATDATA_H

