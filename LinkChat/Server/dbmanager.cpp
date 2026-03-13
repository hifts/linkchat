#include "dbmanager.h"

#include "tcpserver.h"
#include "encryptionmanager.h"
#include "configmanager.h"
#include "configkeys.h"
#include "logger.h"

#include <QSqlError>
#include <QDebug>
#include <QSqlQuery>
#include <QDateTime>
#include <QDataStream>

DBManager &DBManager::instance()
{
    static DBManager instance;
    return instance;
}

DBManager::DBManager(QObject *parent)
    : QObject{parent}
{}


bool DBManager::connectToDb()
{
    if (QSqlDatabase::contains("linkchat_connection")) {
        m_db = QSqlDatabase::database("linkchat_connection");
    } else {
        m_db = QSqlDatabase::addDatabase("QMYSQL", "linkchat_connection");
    }

    const QString dbHost = ConfigManager::instance().getString(
        ConfigKeys::Server::Database::HOST, "127.0.0.1");
    const int dbPort = ConfigManager::instance().getInt(
        ConfigKeys::Server::Database::PORT, 3306);
    const QString dbName = ConfigManager::instance().getString(
        ConfigKeys::Server::Database::DATABASE, "linkchat");
    const QString dbUser = ConfigManager::instance().getString(
        ConfigKeys::Server::Database::USERNAME, "root");
    const QString dbPassword = ConfigManager::instance().getString(
        ConfigKeys::Server::Database::PASSWORD, "root");

    m_db.setHostName(dbHost);
    m_db.setPort(dbPort);
    m_db.setDatabaseName(dbName);
    m_db.setUserName(dbUser);
    m_db.setPassword(dbPassword);

    LOG_INFO(QString("[DB] Connecting MySQL host=%1 port=%2 database=%3 user=%4 password_empty=%5")
             .arg(dbHost)
             .arg(dbPort)
             .arg(dbName)
             .arg(dbUser)
             .arg(dbPassword.isEmpty() ? "true" : "false"));

    if (!m_db.open()) {
        const QString errorText = m_db.lastError().text();
        qCritical() << "Database connection failed:" << errorText;
        LOG_ERROR(QString("[DB] Connection failed: %1").arg(errorText));
        return false;
    }

    qDebug() << "Database connected!";
    return true;
}

bool DBManager::handelRegister(const QString &user, const QString &pwd)
{
    if(user.isEmpty() || pwd.isEmpty()){
        return false;
    }

    QSqlQuery query(m_db);

    query.prepare("select id from t_user where username = ?");
    query.addBindValue(user);
    if(query.exec() && query.next()){
        return false;
    }

    query.prepare("insert into t_user (username,password) values (?,?)");
    query.addBindValue(user);
    query.addBindValue(pwd);

    return query.exec();
}

bool DBManager::handelRegister(const QString &user, const QString &passwordHash, const QByteArray &salt)
{
    if(user.isEmpty() || passwordHash.isEmpty() || salt.isEmpty()){
        qWarning() << "[DB] Invalid registration data: empty user, passwordHash, or salt";
        return false;
    }

    QSqlQuery query(m_db);

    query.prepare("select id from t_user where username = ?");
    query.addBindValue(user);
    if(query.exec() && query.next()){
        qWarning() << "[DB] Registration failed: user already exists:" << user;
        return false;
    }

    query.prepare("insert into t_user (username, password, salt) values (?, ?, ?)");
    query.addBindValue(user);
    query.addBindValue(passwordHash);
    query.addBindValue(salt.toBase64());

    if(!query.exec()){
        qCritical() << "[DB] Registration failed: database insert error:" << query.lastError().text();
        return false;
    }

    qDebug() << "[DB] User registered successfully:" << user;
    return true;
}

bool DBManager::getUserSalt(const QString &user, QByteArray &outSalt)
{
    outSalt.clear();
    if (user.isEmpty()) {
        return false;
    }

    QSqlQuery query(m_db);
    query.prepare("SELECT salt FROM t_user WHERE username = ?");
    query.addBindValue(user);

    if (!query.exec() || !query.next()) {
        return false;
    }

    const QString saltBase64 = query.value(0).toString();
    const QByteArray salt = QByteArray::fromBase64(saltBase64.toUtf8());
    if (salt.isEmpty()) {
        return false;
    }

    outSalt = salt;
    return true;
}

bool DBManager::handleLogin(const QString &user, const QString &pwd, int &outUid)
{
    QSqlQuery query(m_db);
    query.prepare("select id from t_user where username = ? and password = ?");
    query.addBindValue(user);
    query.addBindValue(pwd);

    if(query.exec() && query.next()){
        outUid = query.value(0).toInt();
        return true;
    }

    return false;
}

bool DBManager::handleLogin(const QString &user, const QString &passwordHashBase64, int &outUid,
                           QByteArray &outSalt, QByteArray &outPasswordHash)
{
    if (user.isEmpty() || passwordHashBase64.isEmpty()) {
        qWarning() << "[DB] Empty username or passwordHash for login";
        return false;
    }

    QSqlQuery query(m_db);
    query.prepare("SELECT id, password, salt FROM t_user WHERE username = ?");
    query.addBindValue(user);

    if (!query.exec()) {
        qCritical() << "[DB] Login query failed:" << query.lastError().text();
        return false;
    }

    if (!query.next()) {
        qWarning() << "[DB] User not found:" << user;
        return false;
    }

    int userId = query.value(0).toInt();
    QString storedPasswordHash = query.value(1).toString();
    QString saltBase64 = query.value(2).toString();

    if (saltBase64.isNull() || saltBase64.isEmpty()) {
        qWarning() << "[DB] User has no salt value, login denied";
        return false;
    }

    QByteArray salt = QByteArray::fromBase64(saltBase64.toUtf8());
    if (salt.isEmpty()) {
        qCritical() << "[DB] Failed to decode salt from database";
        return false;
    }

    QByteArray storedHash = QByteArray::fromBase64(storedPasswordHash.toUtf8());
    if (storedHash.isEmpty()) {
        qCritical() << "[DB] Failed to decode password hash from database";
        return false;
    }

    const QByteArray clientHash = QByteArray::fromBase64(passwordHashBase64.toUtf8());
    if (clientHash.isEmpty()) {
        qWarning() << "[DB] Failed to decode client password hash";
        return false;
    }

    if (clientHash == storedHash) {
        outUid = userId;
        outSalt = salt;
        outPasswordHash = storedHash;
        return true;
    } else {
        qWarning() << "[DB] Login failed: incorrect password hash for user:" << user;
        return false;
    }
}

QList<FriendInfo> DBManager::getFriendList(int uid)
{
    QList<FriendInfo> list;

    QSqlQuery query(m_db);
    query.prepare(
        "SELECT u.id, u.username, "
        "COALESCE(MAX(h.send_time), 0) as last_msg_time "
        "FROM t_user u "
        "JOIN t_friend f ON u.id = f.friend_id "
        "LEFT JOIN t_chat_history h ON "
        "  ((h.sender_id = ? AND h.receiver_id = u.id) OR "
        "   (h.sender_id = u.id AND h.receiver_id = ?)) "
        "WHERE f.user_id = ? "
        "GROUP BY u.id, u.username"
    );
    query.addBindValue(uid);
    query.addBindValue(uid);
    query.addBindValue(uid);

    if(query.exec()){
        while (query.next()) {
            FriendInfo info;
            info.id = query.value(0).toInt();
            QString name = query.value(1).toString();
            strncpy(info.userName,name.toStdString().c_str(),32);
            
            QDateTime lastMsgTime = query.value(2).toDateTime();
            info.lastMsgTime = lastMsgTime.isValid() ? lastMsgTime.toSecsSinceEpoch() : 0;

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
    query.addBindValue("%" + keyword + "%");
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
        m_db.commit();
        return true;
    }else{
        m_db.rollback();
        return false;
    }
}

bool DBManager::deleteFriend(int userId, int friendId)
{
    if (userId == friendId) {
        qWarning() << "Cannot delete self as friend:" << userId;
        return false;
    }

    if (!isFriend(userId, friendId)) {
        qWarning() << "Users are not friends:" << userId << "and" << friendId;
        return false;
    }

    QSqlQuery query(m_db);

    if (!m_db.transaction()) {
        qCritical() << "Failed to start transaction";
        return false;
    }

    bool success = true;
    
    query.prepare("DELETE FROM t_friend WHERE (user_id = ? AND friend_id = ?) OR (user_id = ? AND friend_id = ?)");
    query.addBindValue(userId);
    query.addBindValue(friendId);
    query.addBindValue(friendId);
    query.addBindValue(userId);

    if (!query.exec()) {
        success = false;
        qCritical() << "Delete friend failed:" << query.lastError().text();
    }

    if (success) {
        query.clear();
        query.prepare("DELETE FROM t_friend_request WHERE "
                     "(requester_id = ? AND target_id = ?) OR "
                     "(requester_id = ? AND target_id = ?)");
        query.addBindValue(userId);
        query.addBindValue(friendId);
        query.addBindValue(friendId);
        query.addBindValue(userId);
        
        if (!query.exec()) {
            qWarning() << "Failed to delete friend request records:" << query.lastError().text();
        } else {
            qDebug() << "Deleted friend request records between" << userId << "and" << friendId;
        }
    }

    if (success) {
        m_db.commit();
        qDebug() << "Successfully deleted friendship between" << userId << "and" << friendId;
        return true;
    } else {
        m_db.rollback();
        return false;
    }
}

void DBManager::saveFriendRequest(int requesterId, const QString &requesterName, int targetId)
{
    QSqlQuery query(m_db);
    
    query.prepare("DELETE FROM t_friend_request WHERE requester_id = ? AND target_id = ?");
    query.addBindValue(requesterId);
    query.addBindValue(targetId);
    query.exec();
    
    query.clear();
    query.prepare("insert into t_friend_request (requester_id,target_id,requester_name,request_time) values (?,?,?,NOW())");
    query.addBindValue(requesterId);
    query.addBindValue(targetId);
    query.addBindValue(requesterName);
    
    if (!query.exec()) {
        qCritical() << "[DBManager] Failed to save friend request:" << query.lastError().text();
    }
}

QList<QPair<int, QString>> DBManager::getPendingRequests(int userId)
{
    QList<QPair<int, QString>> list;
    QSqlQuery query(m_db);

    query.prepare("select requester_id,requester_name from t_friend_request where target_id = ? and status = 0");
    query.addBindValue(userId);
    
    if(query.exec()){
        while (query.next()) {
            int requesterId = query.value(0).toInt();
            QString requesterName = query.value(1).toString();
            list.append(qMakePair(requesterId, requesterName));
        }
    } else {
        qCritical() << "[DBManager] Failed to get pending requests:" << query.lastError().text();
    }

    return list;
}

void DBManager::markRequestProcessed(int requesterId, int targetId,bool accepted)
{
    QSqlQuery q(m_db);

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

QList<std::tuple<int, QByteArray, quint64>> DBManager::getChatHistory(int userId, int friendId, int limit)
{
    QList<std::tuple<int, QByteArray, quint64>> list;

    QSqlQuery query(m_db);
    query.prepare(
        "SELECT sender_id, content, UNIX_TIMESTAMP(send_time) FROM t_chat_history "
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
            int senderId = query.value(0).toInt();
            QByteArray content = query.value(1).toByteArray();
            quint64 timestamp = query.value(2).toULongLong();
            list.append(std::make_tuple(senderId, content, timestamp));
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
        // Offline message saved
    }else{
        qCritical() << "Failed to save offline msg:" << query.lastError().text();
    }
}

QList<QPair<int, QByteArray>> DBManager::getAndClearOfflineMessages(int receiverId)
{
    QList<QPair<int,QByteArray>> offlineMsgList;
    QSqlQuery query(m_db);

    query.prepare("select sender_id,content from t_offline_msg where receiver_id = ? ORDER BY create_time ASC");
    query.addBindValue(receiverId);

    if(query.exec()){
        while (query.next()) {
            int senderId = query.value(0).toInt();
            QByteArray content = query.value(1).toByteArray();
            offlineMsgList.append(qMakePair(senderId,content));
        }
    }

    if(!offlineMsgList.isEmpty()){
        QSqlQuery delQuery(m_db);
        delQuery.prepare("delete from t_offline_msg where receiver_id = ?");
        delQuery.addBindValue(receiverId);
        delQuery.exec();
    }

    return offlineMsgList;
}


int DBManager::createGroup(const QString &groupName, int creatorId)
{
    if (groupName.isEmpty()) {
        return -1;
    }

    QSqlQuery query(m_db);

    if (!m_db.transaction()) {
        qCritical() << "Failed to start transaction for createGroup";
        return -1;
    }

    query.prepare("INSERT INTO t_groups (group_name, creator_id) VALUES (?, ?)");
    query.addBindValue(groupName);
    query.addBindValue(creatorId);

    if (!query.exec()) {
        qCritical() << "Create group failed:" << query.lastError().text();
        m_db.rollback();
        return -1;
    }

    int groupId = query.lastInsertId().toInt();

    query.clear();
    query.prepare("INSERT INTO t_group_members (group_id, user_id, role) VALUES (?, ?, 2)");
    query.addBindValue(groupId);
    query.addBindValue(creatorId);

    if (!query.exec()) {
        qCritical() << "Add creator to group failed:" << query.lastError().text();
        m_db.rollback();
        return -1;
    }

    m_db.commit();
    
    return groupId;
}

QList<GroupInfo> DBManager::getGroupList(int userId)
{
    QList<GroupInfo> list;
    QSqlQuery query(m_db);

    query.prepare(
        "SELECT g.id, g.group_name, "
        "(SELECT COUNT(*) FROM t_group_members WHERE group_id = g.id) as member_count, "
        "(SELECT username FROM t_user WHERE id = g.creator_id) as creator_name, "
        "COALESCE(MAX(gm.send_time), 0) as last_msg_time "
        "FROM t_groups g "
        "JOIN t_group_members m ON g.id = m.group_id "
        "LEFT JOIN t_group_messages gm ON g.id = gm.group_id "
        "WHERE m.user_id = ? "
        "GROUP BY g.id, g.group_name, g.creator_id"
    );
    query.addBindValue(userId);

    if (query.exec()) {
        while (query.next()) {
            GroupInfo info;
            info.groupId = query.value(0).toInt();
            QString name = query.value(1).toString();
            strncpy(info.groupName, name.toUtf8().constData(), 63);
            info.groupName[63] = '\0';
            info.memberCount = query.value(2).toInt();
            QString creatorName = query.value(3).toString();
            strncpy(info.creatorName, creatorName.toUtf8().constData(), 31);
            info.creatorName[31] = '\0';
            
            QDateTime lastMsgTime = query.value(4).toDateTime();
            info.lastMsgTime = lastMsgTime.isValid() ? lastMsgTime.toSecsSinceEpoch() : 0;
            
            list.append(info);
        }
    }

    return list;
}

QList<GroupMemberInfo> DBManager::getGroupMembers(int groupId)
{
    QList<GroupMemberInfo> list;
    QSqlQuery query(m_db);

    query.prepare(
        "SELECT u.id, u.username, m.role "
        "FROM t_group_members m "
        "JOIN t_user u ON m.user_id = u.id "
        "WHERE m.group_id = ? "
        "ORDER BY m.role DESC, m.join_time ASC"
    );
    query.addBindValue(groupId);

    if (query.exec()) {
        while (query.next()) {
            GroupMemberInfo info;
            info.userId = query.value(0).toInt();
            QString name = query.value(1).toString();
            strncpy(info.userName, name.toUtf8().constData(), 31);
            info.userName[31] = '\0';
            info.role = query.value(2).toInt();
            info.status = TcpServer::instance().isOnline(info.userId) ? 1 : 0;
            list.append(info);
        }
    }

    return list;
}

bool DBManager::addGroupMember(int groupId, int userId, int role)
{
    QSqlQuery query(m_db);
    query.prepare("INSERT IGNORE INTO t_group_members (group_id, user_id, role) VALUES (?, ?, ?)");
    query.addBindValue(groupId);
    query.addBindValue(userId);
    query.addBindValue(role);

    if (!query.exec()) {
        qCritical() << "Add group member failed:" << query.lastError().text();
        return false;
    }

    return query.numRowsAffected() > 0;
}

bool DBManager::removeGroupMember(int groupId, int userId)
{
    QSqlQuery query(m_db);
    query.prepare("DELETE FROM t_group_members WHERE group_id = ? AND user_id = ?");
    query.addBindValue(groupId);
    query.addBindValue(userId);

    if (!query.exec()) {
        qCritical() << "Remove group member failed:" << query.lastError().text();
        return false;
    }

    return query.numRowsAffected() > 0;
}

QList<int> DBManager::getGroupMemberIds(int groupId)
{
    QList<int> list;
    QSqlQuery query(m_db);
    query.prepare("SELECT user_id FROM t_group_members WHERE group_id = ?");
    query.addBindValue(groupId);

    if (query.exec()) {
        while (query.next()) {
            list.append(query.value(0).toInt());
        }
    }

    return list;
}

void DBManager::saveGroupMessage(int groupId, int senderId, const QByteArray &content)
{
    QSqlQuery query(m_db);
    query.prepare("INSERT INTO t_group_messages (group_id, sender_id, content) VALUES (?, ?, ?)");
    query.addBindValue(groupId);
    query.addBindValue(senderId);
    query.addBindValue(content);

    if (!query.exec()) {
        qCritical() << "Save group message failed:" << query.lastError().text();
    }
}

QList<std::tuple<int, QString, QByteArray, quint64>> DBManager::getGroupChatHistory(int groupId, int limit)
{
    QList<std::tuple<int, QString, QByteArray, quint64>> list;
    QSqlQuery query(m_db);

    query.prepare(
        "SELECT m.sender_id, u.username, m.content, UNIX_TIMESTAMP(m.send_time) "
        "FROM t_group_messages m "
        "JOIN t_user u ON m.sender_id = u.id "
        "WHERE m.group_id = ? "
        "ORDER BY m.send_time ASC "
        "LIMIT ?"
    );
    query.addBindValue(groupId);
    query.addBindValue(limit);

    if (query.exec()) {
        while (query.next()) {
            int senderId = query.value(0).toInt();
            QString senderName = query.value(1).toString();
            QByteArray content = query.value(2).toByteArray();
            quint64 timestamp = query.value(3).toULongLong();
            list.append(std::make_tuple(senderId, senderName, content, timestamp));
        }
    }

    return list;
}

void DBManager::saveGroupOfflineMessage(int groupId, int senderId, int receiverId, const QByteArray &content)
{
    QSqlQuery query(m_db);
    query.prepare("INSERT INTO t_group_offline_msg (group_id, sender_id, receiver_id, content) VALUES (?, ?, ?, ?)");
    query.addBindValue(groupId);
    query.addBindValue(senderId);
    query.addBindValue(receiverId);
    query.addBindValue(content);

    if (!query.exec()) {
        qCritical() << "Save group offline message failed:" << query.lastError().text();
    }
}

QList<std::tuple<int, int, QString, QByteArray>> DBManager::getAndClearGroupOfflineMessages(int receiverId)
{
    QList<std::tuple<int, int, QString, QByteArray>> list;
    QSqlQuery query(m_db);

    query.prepare(
        "SELECT o.group_id, o.sender_id, u.username, o.content "
        "FROM t_group_offline_msg o "
        "JOIN t_user u ON o.sender_id = u.id "
        "WHERE o.receiver_id = ? "
        "ORDER BY o.create_time ASC"
    );
    query.addBindValue(receiverId);

    if (query.exec()) {
        while (query.next()) {
            int groupId = query.value(0).toInt();
            int senderId = query.value(1).toInt();
            QString senderName = query.value(2).toString();
            QByteArray content = query.value(3).toByteArray();
            list.append(std::make_tuple(groupId, senderId, senderName, content));
        }
    }

    if (!list.isEmpty()) {
        QSqlQuery delQuery(m_db);
        delQuery.prepare("DELETE FROM t_group_offline_msg WHERE receiver_id = ?");
        delQuery.addBindValue(receiverId);
        delQuery.exec();
    }

    return list;
}

QString DBManager::getUserNameById(int userId)
{
    QSqlQuery query(m_db);
    query.prepare("SELECT username FROM t_user WHERE id = ?");
    query.addBindValue(userId);

    if (query.exec() && query.next()) {
        return query.value(0).toString();
    }

    return QString();
}

bool DBManager::isGroupMember(int groupId, int userId)
{
    QSqlQuery query(m_db);
    query.prepare("SELECT 1 FROM t_group_members WHERE group_id = ? AND user_id = ?");
    query.addBindValue(groupId);
    query.addBindValue(userId);

    return query.exec() && query.next();
}
