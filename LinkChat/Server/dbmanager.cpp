#include "dbmanager.h"

#include "tcpserver.h"
#include "encryptionmanager.h"

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
        qCritical()<<"Database connection failed:"<<m_db.lastError().text();
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

bool DBManager::handelRegister(const QString &user, const QString &passwordHash, const QByteArray &salt)
{
    if(user.isEmpty() || passwordHash.isEmpty() || salt.isEmpty()){
        qWarning() << "[DB] Invalid registration data: empty user, passwordHash, or salt";
        return false;
    }

    QSqlQuery query(m_db);

    // 查重
    query.prepare("select id from t_user where username = ?");
    query.addBindValue(user);
    if(query.exec() && query.next()){
        // 用户已存在
        qWarning() << "[DB] Registration failed: user already exists:" << user;
        return false;
    }

    // 插入用户，包含盐值
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

bool DBManager::handleLogin(const QString &user, const QString &pwd, int &outUid,
                           QByteArray &outSalt, QByteArray &outPasswordHash)
{
    if (user.isEmpty() || pwd.isEmpty()) {
        qWarning() << "[DB] Empty username or password for login";
        return false;
    }

    QSqlQuery query(m_db);
    // 查询用户的ID、密码哈希值和盐值
    query.prepare("SELECT id, password, salt FROM t_user WHERE username = ?");
    query.addBindValue(user);

    if (!query.exec()) {
        qCritical() << "[DB] Login query failed:" << query.lastError().text();
        return false;
    }

    if (!query.next()) {
        // 用户不存在
        qWarning() << "[DB] User not found:" << user;
        return false;
    }

    // 读取数据库中的数据
    int userId = query.value(0).toInt();
    QString storedPasswordHash = query.value(1).toString();
    QString saltBase64 = query.value(2).toString();

    // 检查盐值是否存在
    if (saltBase64.isNull() || saltBase64.isEmpty()) {
        qWarning() << "[DB] User has no salt value, login denied";
        return false;
    }

    // 解码盐值
    QByteArray salt = QByteArray::fromBase64(saltBase64.toUtf8());
    if (salt.isEmpty()) {
        qCritical() << "[DB] Failed to decode salt from database";
        return false;
    }

    // 解码存储的哈希值
    QByteArray storedHash = QByteArray::fromBase64(storedPasswordHash.toUtf8());
    if (storedHash.isEmpty()) {
        qCritical() << "[DB] Failed to decode password hash from database";
        return false;
    }

    // 使用EncryptionManager验证密码
    bool verified = EncryptionManager::instance().verifyPassword(pwd, salt, storedHash);
    
    if (verified) {
        outUid = userId;
        outSalt = salt;
        outPasswordHash = storedHash;
        return true;
    } else {
        qWarning() << "[DB] Login failed: incorrect password for user:" << user;
        return false;
    }
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
        // Offline message saved
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

// ============ 群聊相关实现 ============

int DBManager::createGroup(const QString &groupName, int creatorId)
{
    if (groupName.isEmpty()) {
        return -1;
    }

    QSqlQuery query(m_db);

    // 开启事务
    if (!m_db.transaction()) {
        qCritical() << "Failed to start transaction for createGroup";
        return -1;
    }

    // 创建群
    query.prepare("INSERT INTO t_groups (group_name, creator_id) VALUES (?, ?)");
    query.addBindValue(groupName);
    query.addBindValue(creatorId);

    if (!query.exec()) {
        qCritical() << "Create group failed:" << query.lastError().text();
        m_db.rollback();
        return -1;
    }

    int groupId = query.lastInsertId().toInt();

    // 添加创建者为群主
    query.clear();
    query.prepare("INSERT INTO t_group_members (group_id, user_id, role) VALUES (?, ?, 2)");
    query.addBindValue(groupId);
    query.addBindValue(creatorId);

    if (!query.exec()) {
        qCritical() << "Add creator to group failed:" << query.lastError().text();
        m_db.rollback();
        return -1;
    }

    // 创建群成功提交事务
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
        "(SELECT username FROM t_user WHERE id = g.creator_id) as creator_name "
        "FROM t_groups g "
        "JOIN t_group_members m ON g.id = m.group_id "
        "WHERE m.user_id = ?"
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
    // 存储加密的群消息内容（服务器不解密，直接存储密文）
    QSqlQuery query(m_db);
    query.prepare("INSERT INTO t_group_messages (group_id, sender_id, content) VALUES (?, ?, ?)");
    query.addBindValue(groupId);
    query.addBindValue(senderId);
    query.addBindValue(content);  // content 是加密的密文

    if (!query.exec()) {
        qCritical() << "Save group message failed:" << query.lastError().text();
    }
}

QList<std::tuple<int, QString, QByteArray>> DBManager::getGroupChatHistory(int groupId, int limit)
{
    // 返回群聊历史消息（content 是加密的密文，客户端负责解密）
    QList<std::tuple<int, QString, QByteArray>> list;
    QSqlQuery query(m_db);

    query.prepare(
        "SELECT m.sender_id, u.username, m.content "
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
            QByteArray content = query.value(2).toByteArray();  // 加密的密文
            list.append(std::make_tuple(senderId, senderName, content));
        }
    }

    return list;
}

void DBManager::saveGroupOfflineMessage(int groupId, int senderId, int receiverId, const QByteArray &content)
{
    // 存储加密的群离线消息（服务器不解密，直接存储密文）
    QSqlQuery query(m_db);
    query.prepare("INSERT INTO t_group_offline_msg (group_id, sender_id, receiver_id, content) VALUES (?, ?, ?, ?)");
    query.addBindValue(groupId);
    query.addBindValue(senderId);
    query.addBindValue(receiverId);
    query.addBindValue(content);  // content 是加密的密文

    if (!query.exec()) {
        qCritical() << "Save group offline message failed:" << query.lastError().text();
    }
}

QList<std::tuple<int, int, QString, QByteArray>> DBManager::getAndClearGroupOfflineMessages(int receiverId)
{
    // 返回群离线消息（content 是加密的密文，客户端负责解密）
    QList<std::tuple<int, int, QString, QByteArray>> list;
    QSqlQuery query(m_db);

    // 查询离线消息并关联发送者用户名
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
            QByteArray content = query.value(3).toByteArray();  // 加密的密文
            list.append(std::make_tuple(groupId, senderId, senderName, content));
        }
    }

    // 删除已查询的离线消息
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
