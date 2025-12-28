#ifndef DBMANAGER_H
#define DBMANAGER_H

#include <QObject>
#include <QSqlDatabase>
#include <tuple>
#include "packet.h"

class DBManager : public QObject
{
    Q_OBJECT
public:
    static DBManager &instance();

    // 连接数据库
    void connectToDb();

    // 业务接口
    bool handelRegister(const QString &user,const QString &pwd);
    bool handelRegister(const QString &user, const QString &passwordHash, const QByteArray &salt);
    bool handleLogin(const QString &user,const QString &pwd,int & outUid);
    bool handleLogin(const QString &user, const QString &pwd, int &outUid, 
                    QByteArray &outSalt, QByteArray &outPasswordHash);

    // 根据用户id查询好友信息
    QList<FriendInfo> getFriendList(int uid);

    // 查询该用户的好友id
    QList<int> getFriendIds(int uid);

    // 根据关键字模糊搜索用户 (排除自己)
    QList<FriendInfo> searchUsers(const QString &keyword,int currentId);

    // 添加好友（双向）
    bool addFriend(int userId, int friendId);

    // 删除好友（双向）
    bool deleteFriend(int userId, int friendId);

    // 保存好友请求
    void saveFriendRequest(int requesterId,const QString &requesterName,int targetId);

    // 获取某个用户的待处理好友请求
    QList<QPair<int,QString>> getPendingRequests(int userId);

    // 标记请求已处理（同意/拒绝后调用）
    void markRequestProcessed(int requesterId, int targetId,bool accepted);

    // 检查是否已有请求（防重复加）
    bool hasPendingRequest(int requesterId, int targetId);

    // 检查是否是好友
    bool isFriend(int userId, int friendId);

    // 保存历史消息
    void saveChatMessage(int senderId,int receiverId,const QByteArray &content);

    // 获取历史消息（返回：senderId, content, timestamp）
    QList<std::tuple<int, QByteArray, quint64>> getChatHistory(int userId, int friendId, int limit = 200);

    // 保存离线消息
    void saveOfflineMessage(int senderId, int receiverId, const QByteArray &content);

    // 获取并清除离线消息
    QList<QPair<int, QByteArray>> getAndClearOfflineMessages(int receiverId);

    // 创建群聊，返回群ID（失败返回-1）
    int createGroup(const QString &groupName, int creatorId);

    // 获取用户加入的群列表
    QList<GroupInfo> getGroupList(int userId);

    // 获取群成员列表
    QList<GroupMemberInfo> getGroupMembers(int groupId);

    // 添加群成员
    bool addGroupMember(int groupId, int userId, int role = 0);

    // 移除群成员
    bool removeGroupMember(int groupId, int userId);

    // 获取群内所有成员ID
    QList<int> getGroupMemberIds(int groupId);

    // 保存群聊消息
    void saveGroupMessage(int groupId, int senderId, const QByteArray &content);

    // 获取群聊历史消息（返回：senderId, senderName, content, timestamp）
    QList<std::tuple<int, QString, QByteArray, quint64>> getGroupChatHistory(int groupId, int limit = 200);

    // 保存群聊离线消息
    void saveGroupOfflineMessage(int groupId, int senderId, int receiverId, const QByteArray &content);

    // 获取并清除群聊离线消息（返回：groupId, senderId, senderName, content）
    QList<std::tuple<int, int, QString, QByteArray>> getAndClearGroupOfflineMessages(int receiverId);

    // 根据用户ID查询用户名
    QString getUserNameById(int userId);

    // 检查用户是否是群成员
    bool isGroupMember(int groupId, int userId);

private:
    explicit DBManager(QObject *parent = nullptr);
    QSqlDatabase m_db;

signals:
};

#endif // DBMANAGER_H
