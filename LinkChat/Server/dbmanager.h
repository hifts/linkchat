#ifndef DBMANAGER_H
#define DBMANAGER_H

#include <QObject>
#include <QSqlDatabase>
#include <tuple>
#include "packet.h"

struct OfflineMessage {
    quint64 id = 0;
    int senderId = 0;
    QByteArray content;
};

struct GroupOfflineMessage {
    quint64 id = 0;
    int groupId = 0;
    int senderId = 0;
    QString senderName;
    QByteArray content;
};

struct GroupPendingMessage {
    quint64 messageId = 0;
    int groupId = 0;
    int senderId = 0;
    QString senderName;
    QByteArray content;
};

class DBManager : public QObject
{
    Q_OBJECT
public:
    explicit DBManager(QObject *parent = nullptr);

    bool connectToDb(const QString& connectionName);

    bool handleRegister(const QString &user,const QString &pwd);
    bool handleRegister(const QString &user, const QString &passwordHash, const QByteArray &salt);
    bool getUserSalt(const QString &user, QByteArray &outSalt);
    bool handleLogin(const QString &user,const QString &pwd,int & outUid);
    bool handleLogin(const QString &user, const QString &passwordHashBase64, int &outUid,
                    QByteArray &outSalt, QByteArray &outPasswordHash);

    QList<FriendInfo> getFriendList(int uid);

    QList<int> getFriendIds(int uid);

    QList<FriendInfo> searchUsers(const QString &keyword, int currentId);

    bool addFriend(int userId, int friendId);

    bool deleteFriend(int userId, int friendId);

    void saveFriendRequest(int requesterId,const QString &requesterName,int targetId);

    QList<QPair<int,QString>> getPendingRequests(int userId);

    void markRequestProcessed(int requesterId, int targetId,bool accepted);

    bool hasPendingRequest(int requesterId, int targetId);

    bool isFriend(int userId, int friendId);

    void saveChatMessage(int senderId,int receiverId,const QByteArray &content);

    QList<std::tuple<int, QByteArray, quint64>> getChatHistory(int userId, int friendId, int limit = 200);

    void saveOfflineMessage(int senderId, int receiverId, const QByteArray &content);

    QList<OfflineMessage> getPendingOfflineMessages(int receiverId);

    bool markOfflineMessageDelivered(quint64 messageId, int receiverId);

    int createGroup(const QString &groupName, int creatorId);

    QList<GroupInfo> getGroupList(int userId);

    QList<GroupMemberInfo> getGroupMembers(int groupId);

    bool addGroupMember(int groupId, int userId, int role = 0);

    bool removeGroupMember(int groupId, int userId);

    QList<int> getGroupMemberIds(int groupId);

    quint64 saveGroupMessage(int groupId, int senderId, const QByteArray &content);

    QList<std::tuple<int, QString, QByteArray, quint64>> getGroupChatHistory(int groupId, int limit = 200);

    void saveGroupOfflineMessage(int groupId, int senderId, int receiverId, const QByteArray &content);

    QList<GroupOfflineMessage> getPendingGroupOfflineMessages(int receiverId);

    bool markGroupOfflineMessageDelivered(quint64 messageId, int receiverId);

    QList<GroupPendingMessage> getPendingGroupMessagesByCursor(int receiverId);

    bool markGroupMessageDelivered(int groupId, int userId, quint64 messageId);

    bool markGroupMessageRead(int groupId, int userId, quint64 messageId);

    quint64 getGroupMaxMessageId(int groupId);

    QString getUserNameById(int userId);

    bool isGroupMember(int groupId, int userId);

private:
    QSqlDatabase m_db;

signals:
};

#endif // DBMANAGER_H
