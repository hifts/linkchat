#ifndef CHATDATA_H
#define CHATDATA_H

#include <QString>
#include <QDateTime>
#include <QPixmap>


enum MsgType {
    TypeText,
    TypeImage,
    TypeFile
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
    QString fileId;
    QString fileName;
    QString fileDetail;
    QString filePath;
    QString fileSavePath;
    int filePeerId;
    int fileProgress;
    qint64 fileTransferredBytes;
    qint64 fileTotalBytes;
    bool isGroupChat;
    EncryptionStatus encryptionStatus;

    ChatMessage(const QString &msg, bool mine, const QString &avatar, 
                EncryptionStatus encStatus = EncryptionUnknown)
        : type(TypeText), content(msg), isMine(mine), avatarPath(avatar), 
          timestamp(QDateTime::currentSecsSinceEpoch()), filePeerId(0), fileProgress(-1),
          fileTransferredBytes(0), fileTotalBytes(0),
          isGroupChat(false), encryptionStatus(encStatus) {}

    ChatMessage(const QString &msg, bool mine, const QString &avatar, 
                quint64 ts, EncryptionStatus encStatus = EncryptionUnknown)
        : type(TypeText), content(msg), isMine(mine), avatarPath(avatar), 
          timestamp(ts), filePeerId(0), fileProgress(-1),
          fileTransferredBytes(0), fileTotalBytes(0),
          isGroupChat(false), encryptionStatus(encStatus) {}

    ChatMessage(const QByteArray &rawImageData, bool mine, const QString &avatar,
                EncryptionStatus encStatus = EncryptionUnknown)
        : type(TypeImage), image(rawImageData), isMine(mine), avatarPath(avatar),
          timestamp(QDateTime::currentSecsSinceEpoch()), filePeerId(0), fileProgress(-1),
          fileTransferredBytes(0), fileTotalBytes(0),
          isGroupChat(false), encryptionStatus(encStatus) {}

    ChatMessage(const QByteArray &rawImageData, bool mine, const QString &avatar,
                quint64 ts, EncryptionStatus encStatus = EncryptionUnknown)
        : type(TypeImage), image(rawImageData), isMine(mine), avatarPath(avatar),
          timestamp(ts), filePeerId(0), fileProgress(-1),
          fileTransferredBytes(0), fileTotalBytes(0),
          isGroupChat(false), encryptionStatus(encStatus) {}

    ChatMessage(const QString &msg, bool mine, const QString &avatar, const QString &sender,
                EncryptionStatus encStatus = EncryptionUnknown)
        : type(TypeText), content(msg), isMine(mine), avatarPath(avatar), 
          timestamp(QDateTime::currentSecsSinceEpoch()), filePeerId(0), fileProgress(-1),
          fileTransferredBytes(0), fileTotalBytes(0),
          senderName(sender), isGroupChat(true), encryptionStatus(encStatus) {}

    ChatMessage(const QString &msg, bool mine, const QString &avatar, const QString &sender,
                quint64 ts, EncryptionStatus encStatus = EncryptionUnknown)
        : type(TypeText), content(msg), isMine(mine), avatarPath(avatar), 
          timestamp(ts), filePeerId(0), fileProgress(-1),
          fileTransferredBytes(0), fileTotalBytes(0),
          senderName(sender), isGroupChat(true), encryptionStatus(encStatus) {}

    ChatMessage(const QByteArray &rawImageData, bool mine, const QString &avatar, const QString &sender,
                EncryptionStatus encStatus = EncryptionUnknown)
        : type(TypeImage), image(rawImageData), isMine(mine), avatarPath(avatar),
          timestamp(QDateTime::currentSecsSinceEpoch()), filePeerId(0), fileProgress(-1),
          fileTransferredBytes(0), fileTotalBytes(0),
          senderName(sender), isGroupChat(true), encryptionStatus(encStatus) {}

    ChatMessage(const QByteArray &rawImageData, bool mine, const QString &avatar, const QString &sender,
                quint64 ts, EncryptionStatus encStatus = EncryptionUnknown)
        : type(TypeImage), image(rawImageData), isMine(mine), avatarPath(avatar),
          timestamp(ts), filePeerId(0), fileProgress(-1),
          fileTransferredBytes(0), fileTotalBytes(0),
          senderName(sender), isGroupChat(true), encryptionStatus(encStatus) {}

    ChatMessage() : type(TypeText), isMine(false), timestamp(0),
                    filePeerId(0), fileProgress(-1), fileTransferredBytes(0), fileTotalBytes(0),
                    isGroupChat(false), encryptionStatus(EncryptionUnknown) {}
};

#endif // CHATDATA_H

