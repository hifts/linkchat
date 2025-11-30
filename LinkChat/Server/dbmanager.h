#ifndef DBMANAGER_H
#define DBMANAGER_H

#include <QObject>
#include <QSqlDatabase>
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
    bool handleLogin(const QString &user,const QString &pwd,int & outUid);

    // 根据用户id查询好友信息
    QList<FriendInfo> getFriendList(int uid);

    // 查询该用户的好友id
    QList<int> getFriendIds(int uid);

    // 根据关键字模糊搜索用户 (排除自己)
    QList<FriendInfo> searchUsers(const QString &keyword,int currentId);

    // 添加好友（双向）
    bool addFriend(int userId, int friendId);

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

    // 获取历史消息
    QList<QPair<int, QByteArray>> getChatHistory(int userId, int friendId, int limit = 200);

    // 保存离线消息
    void saveOfflineMessage(int senderId, int receiverId, const QByteArray &content);

    // 获取并清除离线消息
    QList<QPair<int, QByteArray>> getAndClearOfflineMessages(int receiverId);
private:
    explicit DBManager(QObject *parent = nullptr);
    QSqlDatabase m_db;

signals:
};

#endif // DBMANAGER_H
