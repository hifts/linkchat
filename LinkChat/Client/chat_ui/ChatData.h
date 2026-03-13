#ifndef CHATDATA_H
#define CHATDATA_H

#include <QString>
#include <QDateTime>
#include <QPixmap>


enum MsgType {
    TypeText,
    TypeImage
};

enum EncryptionStatus {
    EncryptionUnknown,
    EncryptionEncrypted,
    EncryptionUnencrypted
};

struct ChatMessage{
    MsgType type;
    QString content;
    QByteArray image;
    bool isMine;
    QString avatarPath;
    quint64 timestamp;
    QString senderName;
    bool isGroupChat;
    EncryptionStatus encryptionStatus;

    ChatMessage(const QString &msg, bool mine, const QString &avatar, 
                EncryptionStatus encStatus = EncryptionUnknown)
        : type(TypeText), content(msg), isMine(mine), avatarPath(avatar), 
          timestamp(QDateTime::currentSecsSinceEpoch()),
          isGroupChat(false), encryptionStatus(encStatus) {}

    ChatMessage(const QString &msg, bool mine, const QString &avatar, 
                quint64 ts, EncryptionStatus encStatus = EncryptionUnknown)
        : type(TypeText), content(msg), isMine(mine), avatarPath(avatar), 
          timestamp(ts),
          isGroupChat(false), encryptionStatus(encStatus) {}

    ChatMessage(const QByteArray &rawImageData, bool mine, const QString &avatar,
                EncryptionStatus encStatus = EncryptionUnknown)
        : type(TypeImage), image(rawImageData), isMine(mine), avatarPath(avatar),
          timestamp(QDateTime::currentSecsSinceEpoch()),
          isGroupChat(false), encryptionStatus(encStatus) {}

    ChatMessage(const QByteArray &rawImageData, bool mine, const QString &avatar,
                quint64 ts, EncryptionStatus encStatus = EncryptionUnknown)
        : type(TypeImage), image(rawImageData), isMine(mine), avatarPath(avatar),
          timestamp(ts),
          isGroupChat(false), encryptionStatus(encStatus) {}

    ChatMessage(const QString &msg, bool mine, const QString &avatar, const QString &sender,
                EncryptionStatus encStatus = EncryptionUnknown)
        : type(TypeText), content(msg), isMine(mine), avatarPath(avatar), 
          timestamp(QDateTime::currentSecsSinceEpoch()),
          senderName(sender), isGroupChat(true), encryptionStatus(encStatus) {}

    ChatMessage(const QString &msg, bool mine, const QString &avatar, const QString &sender,
                quint64 ts, EncryptionStatus encStatus = EncryptionUnknown)
        : type(TypeText), content(msg), isMine(mine), avatarPath(avatar), 
          timestamp(ts),
          senderName(sender), isGroupChat(true), encryptionStatus(encStatus) {}

    ChatMessage(const QByteArray &rawImageData, bool mine, const QString &avatar, const QString &sender,
                EncryptionStatus encStatus = EncryptionUnknown)
        : type(TypeImage), image(rawImageData), isMine(mine), avatarPath(avatar),
          timestamp(QDateTime::currentSecsSinceEpoch()),
          senderName(sender), isGroupChat(true), encryptionStatus(encStatus) {}

    ChatMessage(const QByteArray &rawImageData, bool mine, const QString &avatar, const QString &sender,
                quint64 ts, EncryptionStatus encStatus = EncryptionUnknown)
        : type(TypeImage), image(rawImageData), isMine(mine), avatarPath(avatar),
          timestamp(ts),
          senderName(sender), isGroupChat(true), encryptionStatus(encStatus) {}

    ChatMessage() : type(TypeText), isMine(false), timestamp(0), isGroupChat(false), 
                    encryptionStatus(EncryptionUnknown) {}
};

#endif // CHATDATA_H

