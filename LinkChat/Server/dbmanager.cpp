#include "dbmanager.h"

#include "tcpserver.h"

#include <QSqlError>
#include <QDebug>
#include <QSqlQuery>

DBManager &DBManager::instance()
{
    static DBManager instance;
    return instance;
}

DBManager::DBManager(QObject *parent)
    : QObject{parent}
{}


void DBManager::connectToDb()
{
    m_db = QSqlDatabase::addDatabase("QMYSQL");
    m_db.setHostName("127.0.0.1");
    m_db.setDatabaseName("linkchat");
    m_db.setUserName("root");
    m_db.setPassword("root");

    if(!m_db.open()){
        qCritical()<<"Database cinnection failed:"<<m_db.lastError().text();
    }else{
        qDebug()<<"Database connected!";
    }
}

bool DBManager::handelRegister(const QString &user, const QString &pwd)
{
    if(user.isEmpty() || pwd.isEmpty()){
        return false;
    }

    QSqlQuery query(m_db);

    // 查重
    query.prepare("select id from t_user where username = ?");
    query.addBindValue(user);
    if(query.exec() && query.next()){
        // 用户已存在
        return false;
    }

    // 插入用户
    query.prepare("insert into t_user (username,password) values (?,?)");
    query.addBindValue(user);
    query.addBindValue(pwd);

    return query.exec();
}

bool DBManager::handleLogin(const QString &user, const QString &pwd, int &outUid)
{
    QSqlQuery query(m_db);
    query.prepare("select id from t_user where username = ? and password = ?");
    query.addBindValue(user);
    query.addBindValue(pwd);

    if(query.exec() && query.next()){
        // 用户存在返回用户在数据库中的id
        outUid = query.value(0).toInt();
        return true;
    }

    return false;
}

QList<FriendInfo> DBManager::getFriendList(int uid)
{
    QList<FriendInfo> list;

    QSqlQuery query(m_db);
    query.prepare("select u.id,u.username from t_user u "
                  "join t_friend f on u.id = f.friend_id "
                  "where f.user_id = ?");
    query.addBindValue(uid);

    if(query.exec()){
        while (query.next()) {
            FriendInfo info;
            info.id = query.value(0).toInt();
            QString name = query.value(1).toString();
            strncpy(info.userName,name.toStdString().c_str(),32);

            // 判断用户是否在线
            bool online = TcpServer::instance().isOnline(info.id);

            info.status = online? 1 : 0;
            list.append(info);
        }
    }
    return list;
}

QList<int> DBManager::getFriendIds(int uid)
{
    QList<int> list;
    QSqlQuery query(m_db);
    // 只需要查 friend_id 即可
    query.prepare("SELECT friend_id FROM t_friend WHERE user_id = ?");
    query.addBindValue(uid);

    if (query.exec()) {
        while (query.next()) {
            list.append(query.value(0).toInt());
        }
    }
    return list;
}

QList<FriendInfo> DBManager::searchUsers(const QString &keyword, int currentId)
{
    QList<FriendInfo> list;
    if(keyword.isEmpty()){
        return list;
    }

    QSqlQuery query(m_db);
    QString sql = "select id,username from t_user where username like ? and id != ?";

    query.prepare(sql);
    query.addBindValue(QString("%%1%").arg(keyword));
    query.addBindValue(currentId);

    if(query.exec()){
        while(query.next()){
            FriendInfo info;
            info.id = query.value(0).toInt();
            QString name = query.value(1).toString();

            memset(info.userName,0,32);
            strncpy(info.userName,name.toStdString().c_str(),32);

            bool online = TcpServer::instance().isOnline(info.id);
            info.status = online? 1 : 0;

            list.append(info);
        }
    }

    return list;
}

bool DBManager::addFriend(int userId, int friendId)
{
    if (userId == friendId) {
        qWarning() << "Cannot add self as friend:" << userId;
        return false;
    }

    if(isFriend(userId,friendId)){
        return false;
    }

    QSqlQuery query(m_db);

    // 开启事务
    if (!m_db.transaction()) {
        qCritical() << "Failed to start transaction";
        return false;
    }

    bool success = true;
    query.prepare("insert ignore into t_friend (user_id,friend_id) values(?,?)");
    query.addBindValue(userId);
    query.addBindValue(friendId);

    if (!query.exec()) {
        success = false;
        qCritical() << "Add friend failed (1):" << query.lastError().text();
    }

    query.clear();
    query.prepare("insert ignore into t_friend (user_id,friend_id) values(?,?)");
    query.addBindValue(friendId);
    query.addBindValue(userId);
    if (!query.exec()) {
        success = false;
        qCritical() << "Add friend failed (2):" << query.lastError().text();
    }

    if(success){
        // 提交事务
        m_db.commit();
        return true;
    }else{
        // 回滚到插入数据之前
        m_db.rollback();
        return false;
    }
}

void DBManager::saveFriendRequest(int requesterId, const QString &requesterName, int targetId)
{
    QSqlQuery query(m_db);
    query.prepare("insert into t_friend_request (requester_id,target_id,requester_name) values (?,?,?)");
    query.addBindValue(requesterId);
    query.addBindValue(targetId);
    query.addBindValue(requesterName);

    query.exec();
}

QList<QPair<int, QString>> DBManager::getPendingRequests(int userId)
{
    QList<QPair<int, QString>> list;
    QSqlQuery query(m_db);

    query.prepare("select requester_id,requester_name from t_friend_request where target_id = ? and status = 0");
    query.addBindValue(userId);
    if(query.exec()){
        while (query.next()) {
            list.append(qMakePair(query.value(0).toInt(),query.value(1).toString()));
        }
    }

    return list;
}

void DBManager::markRequestProcessed(int requesterId, int targetId,bool accepted)
{
    QSqlQuery q(m_db);

    // 1=同意，2=拒绝
    int status = accepted? 1 : 2;

    q.prepare("UPDATE t_friend_request SET status = ? "
              "WHERE requester_id = ? AND target_id = ?");
    q.addBindValue(status);
    q.addBindValue(requesterId);
    q.addBindValue(targetId);
    q.exec();
}

bool DBManager::hasPendingRequest(int requesterId, int targetId)
{
    QSqlQuery q(m_db);
    q.prepare("SELECT 1 FROM t_friend_request WHERE requester_id = ? AND target_id = ? AND status = 0");
    q.addBindValue(requesterId);
    q.addBindValue(targetId);
    return q.exec() && q.next();
}

bool DBManager::isFriend(int userId, int friendId)
{
    QSqlQuery query(m_db);
    query.prepare("SELECT 1 FROM t_friend WHERE user_id = ? AND friend_id = ?");
    query.addBindValue(userId);
    query.addBindValue(friendId);

    return query.exec() && query.next();
}

void DBManager::saveChatMessage(int senderId, int receiverId, const QByteArray &content)
{
    QSqlQuery query(m_db);
    query.prepare("insert into t_chat_history (sender_id, receiver_id, content) values (?,?,?)");
    query.addBindValue(senderId);
    query.addBindValue(receiverId);
    query.addBindValue(content);

    query.exec();
}

QList<QPair<int, QByteArray> > DBManager::getChatHistory(int userId, int friendId, int limit)
{
    QList<QPair<int, QByteArray>> list;

    QSqlQuery query(m_db);
    query.prepare(
        "SELECT sender_id, content FROM t_chat_history "
        "WHERE (sender_id = ? AND receiver_id = ?) OR (sender_id = ? AND receiver_id = ?) "
        "ORDER BY send_time ASC LIMIT ?"
        );
    query.addBindValue(userId);
    query.addBindValue(friendId);
    query.addBindValue(friendId);
    query.addBindValue(userId);
    query.addBindValue(limit);

    if(query.exec()){
        while (query.next()) {
            list.append(qMakePair(query.value(0).toInt(),query.value(1).toByteArray()));
        }
    }

    return list;
}

void DBManager::saveOfflineMessage(int senderId, int receiverId, const QByteArray &content)
{
    QSqlQuery query(m_db);
    query.prepare("insert into t_offline_msg (sender_id,receiver_id,content) values (?,?,?)");
    query.addBindValue(senderId);
    query.addBindValue(receiverId);
    query.addBindValue(content);

    if(query.exec()){
        qDebug() << "Offline msg saved for User" << receiverId << "from User" << senderId;
    }else{
        qCritical() << "Failed to save offline msg:" << query.lastError().text();
    }
}

QList<QPair<int, QByteArray>> DBManager::getAndClearOfflineMessages(int receiverId)
{
    QList<QPair<int,QByteArray>> offlineMsgList;
    QSqlQuery query(m_db);

    // 查询消息
    query.prepare("select sender_id,content from t_offline_msg where receiver_id = ? ORDER BY create_time ASC");
    query.addBindValue(receiverId);

    if(query.exec()){
        while (query.next()) {
            int senderId = query.value(0).toInt();
            QByteArray content = query.value(1).toByteArray();
            offlineMsgList.append(qMakePair(senderId,content));
        }
    }

    // 如果有消息，查询完后删除这些消息
    if(!offlineMsgList.isEmpty()){
        QSqlQuery delQuery(m_db);
        delQuery.prepare("delete from t_offline_msg where receiver_id = ?");
        delQuery.addBindValue(receiverId);
        delQuery.exec();
    }

    return offlineMsgList;
}
