#include "clientsocket.h"
#include "dbmanager.h"
#include "dbworkerpool.h"
#include "logger.h"
#include "serverstats.h"
#include "tcpserver.h"

#include <QDataStream>
#include <QDateTime>
#include <QMetaObject>
#include <QSharedPointer>
#include <QTimer>
#include <cstring>

Q_DECLARE_METATYPE(QSharedPointer<QList<FriendInfo>>)
Q_DECLARE_METATYPE(QSharedPointer<QList<int>>)
using ChatHistoryList = QList<std::tuple<int, QByteArray, quint64>>;
Q_DECLARE_METATYPE(QSharedPointer<ChatHistoryList>)
using PendingRequestList = QList<QPair<int, QString>>;
using OfflineMessageList = QList<QPair<int, QByteArray>>;
using GroupOfflineMessageList = QList<std::tuple<int, int, QString, QByteArray>>;
using GroupInfoList = QList<GroupInfo>;
using GroupMemberInfoList = QList<GroupMemberInfo>;
using GroupHistoryList = QList<std::tuple<int, QString, QByteArray, quint64>>;
Q_DECLARE_METATYPE(QSharedPointer<PendingRequestList>)
Q_DECLARE_METATYPE(QSharedPointer<OfflineMessageList>)
Q_DECLARE_METATYPE(QSharedPointer<GroupOfflineMessageList>)
Q_DECLARE_METATYPE(QSharedPointer<GroupInfoList>)
Q_DECLARE_METATYPE(QSharedPointer<GroupMemberInfoList>)
Q_DECLARE_METATYPE(QSharedPointer<GroupHistoryList>)

namespace {
constexpr uint32_t MAX_NORMAL_PACKET_LEN = 1u * 1024u * 1024u;
constexpr uint32_t MAX_FILE_PACKET_LEN = 1u * 1024u * 1024u;
constexpr int MAX_SOCKET_BUFFER_LEN = 2 * 1024 * 1024;
constexpr qint64 MAX_PENDING_WRITE_BYTES = 4 * 1024 * 1024;
}

ClientSocket::ClientSocket(QObject *parent)
    : QTcpSocket{parent}
{
    m_lastActiveMs = QDateTime::currentMSecsSinceEpoch();
    connect(this, &QTcpSocket::readyRead, this, &ClientSocket::onReadyRead);

    connect(this, &QTcpSocket::disconnected, this, [this]() {
        const int disconnectedUid = m_uid;
        if (disconnectedUid != 0) {
            notifyFriends(0);
            TcpServer::instance().userLogout(disconnectedUid, this);
        }
        TcpServer::instance().socketDisconnected(this);
    });

    auto* heartbeatTimer = new QTimer(this);
    heartbeatTimer->setInterval(10000);
    connect(heartbeatTimer, &QTimer::timeout, this, &ClientSocket::checkHeartbeatTimeout);
    heartbeatTimer->start();
}

int ClientSocket::uid() const
{
    return m_uid;
}

void ClientSocket::setHeartbeatTimeout(int timeoutMs)
{
    m_heartbeatTimeoutMs = qMax(1000, timeoutMs);
}

void ClientSocket::markLoggedOut()
{
    m_uid = 0;
    m_userName.clear();
}

void ClientSocket::notifyOfflineToFriends()
{
    notifyFriends(0);
}

void ClientSocket::onReadyRead()
{
    m_lastActiveMs = QDateTime::currentMSecsSinceEpoch();
    m_buffer.append(readAll());
    if (m_buffer.size() > MAX_SOCKET_BUFFER_LEN) {
        closeForProtocolError(QString("receive buffer too large: %1").arg(m_buffer.size()));
        return;
    }

    while (m_buffer.size() >= (int)sizeof(PDUHeader)) {
        while (m_buffer.size() >= (int)sizeof(uint32_t)) {
            uint32_t magic = 0;
            memcpy(&magic, m_buffer.constData(), sizeof(uint32_t));
            if (magic == PDU_MAGIC) break;

            int nextIdx = -1;
            for (int i = 1; i <= m_buffer.size() - (int)sizeof(uint32_t); ++i) {
                uint32_t m = 0;
                memcpy(&m, m_buffer.constData() + i, sizeof(uint32_t));
                if (m == PDU_MAGIC) {
                    nextIdx = i;
                    break;
                }
            }

            if (nextIdx > 0) {
                LOG_WARN(QString("[Server] Packet resync: skipping %1 bytes").arg(nextIdx));
                m_buffer.remove(0, nextIdx);
            } else {
                const int keep = qMin(3, m_buffer.size());
                m_buffer.remove(0, m_buffer.size() - keep);
            }
        }

        if (m_buffer.size() < (int)sizeof(PDUHeader)) break;

        PDUHeader header;
        memcpy(&header, m_buffer.constData(), sizeof(PDUHeader));

        const uint32_t maxLen = isFilePacket(header.msg_type) ? MAX_FILE_PACKET_LEN : MAX_NORMAL_PACKET_LEN;
        if (header.total_len < sizeof(PDUHeader) || header.total_len > maxLen) {
            closeForProtocolError(QString("invalid packet length: %1").arg(header.total_len));
            return;
        }

        if (m_buffer.size() < (int)header.total_len) break;

        QByteArray bodyData = m_buffer.mid(sizeof(PDUHeader), header.total_len - sizeof(PDUHeader));
        m_buffer.remove(0, header.total_len);

        ServerStats::instance().packetReceived();
        ServerStats::instance().messageHandled(header.msg_type);
        handlePacket(header.msg_type, header.src_id, header.dest_id, bodyData);
    }
}

void ClientSocket::handlePacket(uint32_t msgType, uint32_t srcId, uint32_t destId, const QByteArray& bodyData)
{
    Q_UNUSED(srcId);

    switch (msgType) {
    case MSG_HEARTBEAT_REQ: {
        ServerStats::instance().heartbeatReceived();
        sendPacket(MSG_HEARTBEAT_RESP, QByteArray());
        break;
    }
    case MSG_REGISTER_REQ: {
        if (bodyData.size() < (int)sizeof(RegisterReq)) break;
        RegisterReq req;
        memcpy(&req, bodyData.constData(), sizeof(req));
        const QString userName = QString::fromUtf8(req.userName);
        const QString passwordHashBase64 = QString::fromUtf8(req.passwordHash);
        const QByteArray salt = QByteArray::fromBase64(QByteArray(req.salt));

        DbWorkerPool::instance().enqueue(
            [userName, passwordHashBase64, salt](DBManager& db) {
                return QVariant(db.handelRegister(userName, passwordHashBase64, salt));
            },
            this,
            [this](const QVariant& result) {
                LoginResp resp;
                resp.result = result.toBool() ? 1 : 0;
                resp.userId = 0;
                sendPacket(MSG_REGISTER_RESP, QByteArray((char*)&resp, sizeof(resp)));
            });
        break;
    }
    case MSG_LOGIN_SALT_REQ: {
        if (bodyData.size() < (int)sizeof(LoginSaltReq)) break;
        LoginSaltReq req;
        memcpy(&req, bodyData.constData(), sizeof(req));
        const QString userName = QString::fromUtf8(req.userName);

        DbWorkerPool::instance().enqueue(
            [userName](DBManager& db) {
                QByteArray salt;
                QVariantMap out;
                out["ok"] = db.getUserSalt(userName, salt);
                out["salt"] = salt;
                return out;
            },
            this,
            [this](const QVariant& result) {
                const QVariantMap out = result.toMap();
                LoginSaltResp resp;
                memset(&resp, 0, sizeof(resp));
                resp.result = out.value("ok").toBool() ? 1 : 0;
                if (resp.result == 1) {
                    const QByteArray saltBase64 = out.value("salt").toByteArray().toBase64();
                    strncpy(resp.salt, saltBase64.constData(), sizeof(resp.salt) - 1);
                }
                sendPacket(MSG_LOGIN_SALT_RESP, QByteArray((char*)&resp, sizeof(resp)));
            });
        break;
    }
    case MSG_LOGIN_REQ: {
        if (bodyData.size() < (int)sizeof(LoginReq) || m_loginPending) break;
        m_loginPending = true;

        LoginReq req;
        memcpy(&req, bodyData.constData(), sizeof(req));
        const QString userName = QString::fromUtf8(req.userName);
        const QString passwordHash = QString::fromUtf8(req.passwordHash);

        DbWorkerPool::instance().enqueue(
            [userName, passwordHash](DBManager& db) {
                int uid = -1;
                QByteArray salt, storedHash;
                QVariantMap out;
                out["ok"] = db.handleLogin(userName, passwordHash, uid, salt, storedHash);
                out["uid"] = uid;
                return out;
            },
            this,
            [this, userName](const QVariant& result) {
                m_loginPending = false;
                const QVariantMap out = result.toMap();
                LoginResp resp;
                resp.result = out.value("ok").toBool() ? 1 : 0;
                resp.userId = out.value("uid").toInt();

                if (resp.result == 1) {
                    if (TcpServer::instance().tryUserLogin(resp.userId, this)) {
                        m_uid = resp.userId;
                        m_userName = userName;
                        ServerStats::instance().loginSucceeded();
                        notifyFriends(1);
                        const int loginUid = resp.userId;
                        DbWorkerPool::instance().enqueue(
                            [loginUid](DBManager& db) {
                                QVariantMap data;
                                data["pending"] = QVariant::fromValue(QSharedPointer<PendingRequestList>::create(db.getPendingRequests(loginUid)));
                                data["offline"] = QVariant::fromValue(QSharedPointer<OfflineMessageList>::create(db.getAndClearOfflineMessages(loginUid)));
                                data["groupOffline"] = QVariant::fromValue(QSharedPointer<GroupOfflineMessageList>::create(db.getAndClearGroupOfflineMessages(loginUid)));
                                return data;
                            },
                            this,
                            [this, loginUid](const QVariant& result) {
                                if (m_uid != loginUid || state() != QAbstractSocket::ConnectedState) {
                                    return;
                                }
                                const QVariantMap data = result.toMap();
                                const auto pending = data.value("pending").value<QSharedPointer<PendingRequestList>>();
                                const auto offline = data.value("offline").value<QSharedPointer<OfflineMessageList>>();
                                const auto groupOffline = data.value("groupOffline").value<QSharedPointer<GroupOfflineMessageList>>();
                                QTimer::singleShot(200, this, [this, loginUid, pending, offline, groupOffline]() {
                                    if (m_uid != loginUid || state() != QAbstractSocket::ConnectedState) {
                                        return;
                                    }
                                    if (pending) pushFriendRequests(*pending);
                                    if (offline) pushOfflineMsgs(*offline);
                                    if (groupOffline) pushGroupOfflineMsgs(*groupOffline);
                                });
                            });
                    } else {
                        resp.result = 2;
                        ServerStats::instance().loginFailed();
                    }
                } else {
                    ServerStats::instance().loginFailed();
                }
                sendPacket(MSG_LOGIN_RESP, QByteArray((char*)&resp, sizeof(resp)));
            });
        break;
    }
    case MSG_FRIEND_LIST_REQ: {
        const int currentUid = m_uid;
        DbWorkerPool::instance().enqueue(
            [currentUid](DBManager& db) {
                return QVariant::fromValue(QSharedPointer<QList<FriendInfo>>::create(db.getFriendList(currentUid)));
            },
            this,
            [this](const QVariant& result) {
                const auto friendListPtr = result.value<QSharedPointer<QList<FriendInfo>>>();
                QList<FriendInfo> friendList = friendListPtr ? *friendListPtr : QList<FriendInfo>();
                for (auto& info : friendList) {
                    info.status = TcpServer::instance().isOnline(info.id) ? 1 : 0;
                }
                const int count = friendList.size();
                QByteArray resBody;
                resBody.resize(sizeof(int) + count * sizeof(FriendInfo));
                char* ptr = resBody.data();
                memcpy(ptr, &count, sizeof(int));
                ptr += sizeof(int);
                for (const auto& info : friendList) {
                    memcpy(ptr, &info, sizeof(FriendInfo));
                    ptr += sizeof(FriendInfo);
                }
                sendPacket(MSG_FRIEND_LIST_RESP, resBody);
            });
        break;
    }
    case MSG_CHAT_TEXT: {
        const int senderId = m_uid;
        const int targetId = destId;
        DbWorkerPool::instance().enqueue(
            [senderId, targetId, bodyData](DBManager& db) {
                db.saveChatMessage(senderId, targetId, bodyData);
                return QVariant();
            },
            this);

        if (!TcpServer::instance().sendToUser(targetId, MSG_CHAT_TEXT, bodyData, senderId, targetId)) {
            DbWorkerPool::instance().enqueue(
                [senderId, targetId, bodyData](DBManager& db) {
                    db.saveOfflineMessage(senderId, targetId, bodyData);
                    return QVariant();
                },
                this);
        }
        break;
    }
    case MSG_CHAT_HISTORY_REQ: {
        if (bodyData.size() < 4) break;
        const int friendId = *(int*)bodyData.constData();
        const int currentUid = m_uid;
        DbWorkerPool::instance().enqueue(
            [currentUid, friendId](DBManager& db) {
                using HistoryList = QList<std::tuple<int, QByteArray, quint64>>;
                return QVariant::fromValue(QSharedPointer<HistoryList>::create(db.getChatHistory(currentUid, friendId, 200)));
            },
            this,
            [this, friendId](const QVariant& result) {
                using HistoryList = QList<std::tuple<int, QByteArray, quint64>>;
                const auto historyPtr = result.value<QSharedPointer<HistoryList>>();
                const HistoryList history = historyPtr ? *historyPtr : HistoryList();
                QByteArray resBytes;
                QDataStream out(&resBytes, QIODevice::WriteOnly);
                out.setByteOrder(QDataStream::LittleEndian);
                out << quint32(history.size());
                for (const auto& item : history) {
                    out << quint32(std::get<0>(item));
                    out << quint64(std::get<2>(item));
                    out << quint32(std::get<1>(item).size());
                    out.writeRawData(std::get<1>(item).constData(), std::get<1>(item).size());
                }
                sendPacket(MSG_CHAT_HISTORY_RESP, resBytes, friendId, m_uid);
            });
        break;
    }
    case MSG_SEARCH_USER_REQ: {
        if (bodyData.size() < (int)sizeof(SearchReq)) break;
        SearchReq req;
        memcpy(&req, bodyData.constData(), sizeof(req));
        const QString keyword = QString::fromUtf8(req.keyword).trimmed();
        const int currentUid = m_uid;
        DbWorkerPool::instance().enqueue(
            [keyword, currentUid](DBManager& db) {
                return QVariant::fromValue(QSharedPointer<QList<FriendInfo>>::create(db.searchUsers(keyword, currentUid)));
            },
            this,
            [this](const QVariant& result) {
                const auto listPtr = result.value<QSharedPointer<QList<FriendInfo>>>();
                QList<FriendInfo> users = listPtr ? *listPtr : QList<FriendInfo>();
                for (auto& info : users) {
                    info.status = TcpServer::instance().isOnline(info.id) ? 1 : 0;
                }
                const int count = users.size();
                QByteArray body;
                body.resize(sizeof(int) + sizeof(FriendInfo) * count);
                char* ptr = body.data();
                memcpy(ptr, &count, sizeof(int));
                ptr += sizeof(int);
                for (const auto& info : users) {
                    memcpy(ptr, &info, sizeof(FriendInfo));
                    ptr += sizeof(FriendInfo);
                }
                sendPacket(MSG_SEARCH_USER_RESP, body);
            });
        break;
    }
    case MSG_ADD_FRIEND_REQ: {
        if (bodyData.size() < (int)sizeof(AddFriendReq)) break;
        AddFriendReq req;
        memcpy(&req, bodyData.constData(), sizeof(req));
        const int requesterId = m_uid;
        const int targetId = req.targetId;
        const QString requesterName = m_userName;
        DbWorkerPool::instance().enqueue(
            [requesterId, targetId, requesterName](DBManager& db) {
                const bool canSave = !db.isFriend(requesterId, targetId)
                                     && !db.hasPendingRequest(requesterId, targetId);
                if (canSave) {
                    db.saveFriendRequest(requesterId, requesterName, targetId);
                }
                return QVariant(canSave);
            },
            this,
            [this, requesterId, targetId, requesterName](const QVariant& result) {
                if (!result.toBool()) {
                    return;
                }
                AddFriendNotify notify;
                notify.requesterId = requesterId;
                memset(notify.requesterName, 0, sizeof(notify.requesterName));
                strncpy(notify.requesterName, requesterName.toUtf8().constData(), 31);
                TcpServer::instance().sendToUser(
                    targetId,
                    MSG_ADD_FRIEND_NOTIFY,
                    QByteArray((char*)&notify, sizeof(notify)),
                    0,
                    targetId);
            });
        break;
    }
    case MSG_ADD_FRIEND_RESP: {
        if (bodyData.size() < (int)sizeof(AddFriendResp)) break;
        AddFriendResp resp;
        memcpy(&resp, bodyData.constData(), sizeof(resp));
        const int requesterId = resp.requesterId;
        const int responderId = m_uid;
        const bool accepted = resp.accepted;
        DbWorkerPool::instance().enqueue(
            [requesterId, responderId, accepted](DBManager& db) {
                if (accepted) {
                    db.addFriend(requesterId, responderId);
                }
                db.markRequestProcessed(requesterId, responderId, accepted);
                return QVariant(true);
            },
            this,
            [requesterId, bodyData](const QVariant&) {
                TcpServer::instance().sendToUser(requesterId, MSG_ADD_FRIEND_RESULT, bodyData, 0, requesterId);
            });
        break;
    }
    case MSG_DELETE_FRIEND_REQ: {
        if (bodyData.size() < (int)sizeof(DeleteFriendReq)) break;
        DeleteFriendReq req;
        memcpy(&req, bodyData.constData(), sizeof(req));
        const int requesterId = m_uid;
        const int targetId = req.targetId;
        DbWorkerPool::instance().enqueue(
            [requesterId, targetId](DBManager& db) {
                return QVariant(db.deleteFriend(requesterId, targetId));
            },
            this,
            [this, targetId](const QVariant& result) {
                DeleteFriendResp resp;
                resp.result = result.toBool() ? 1 : 0;
                resp.targetId = targetId;
                sendPacket(MSG_DELETE_FRIEND_RESP, QByteArray((char*)&resp, sizeof(resp)));
            });
        break;
    }
    case MSG_CREATE_GROUP_REQ: {
        if (bodyData.size() < (int)sizeof(CreateGroupReq)) break;
        CreateGroupReq req;
        memcpy(&req, bodyData.constData(), sizeof(req));
        const QString groupName = QString::fromUtf8(req.groupName);
        const int creatorId = m_uid;
        DbWorkerPool::instance().enqueue(
            [groupName, creatorId](DBManager& db) {
                return QVariant(db.createGroup(groupName, creatorId));
            },
            this,
            [this](const QVariant& result) {
                CreateGroupResp resp;
                resp.groupId = result.toInt();
                resp.result = resp.groupId > 0 ? 1 : 0;
                sendPacket(MSG_CREATE_GROUP_RESP, QByteArray((char*)&resp, sizeof(resp)));
            });
        break;
    }
    case MSG_GROUP_LIST_REQ: {
        const int currentUid = m_uid;
        DbWorkerPool::instance().enqueue(
            [currentUid](DBManager& db) {
                return QVariant::fromValue(QSharedPointer<GroupInfoList>::create(db.getGroupList(currentUid)));
            },
            this,
            [this](const QVariant& result) {
                const auto groupsPtr = result.value<QSharedPointer<GroupInfoList>>();
                const GroupInfoList groups = groupsPtr ? *groupsPtr : GroupInfoList();
                const int count = groups.size();
                QByteArray body;
                body.resize(sizeof(int) + count * sizeof(GroupInfo));
                char* ptr = body.data();
                memcpy(ptr, &count, sizeof(int));
                ptr += sizeof(int);
                for (const auto& info : groups) {
                    memcpy(ptr, &info, sizeof(GroupInfo));
                    ptr += sizeof(GroupInfo);
                }
                sendPacket(MSG_GROUP_LIST_RESP, body);
            });
        break;
    }
    case MSG_GROUP_MEMBER_LIST_REQ: {
        if (bodyData.size() < (int)sizeof(int)) break;
        const int groupId = *(int*)bodyData.constData();
        DbWorkerPool::instance().enqueue(
            [groupId](DBManager& db) {
                return QVariant::fromValue(QSharedPointer<GroupMemberInfoList>::create(db.getGroupMembers(groupId)));
            },
            this,
            [this, groupId](const QVariant& result) {
                const auto membersPtr = result.value<QSharedPointer<GroupMemberInfoList>>();
                GroupMemberInfoList members = membersPtr ? *membersPtr : GroupMemberInfoList();
                for (auto& info : members) {
                    info.status = TcpServer::instance().isOnline(info.userId) ? 1 : 0;
                }
                const int count = members.size();
                QByteArray body;
                body.resize(sizeof(int) + sizeof(int) + count * sizeof(GroupMemberInfo));
                char* ptr = body.data();
                memcpy(ptr, &groupId, sizeof(int));
                ptr += sizeof(int);
                memcpy(ptr, &count, sizeof(int));
                ptr += sizeof(int);
                for (const auto& info : members) {
                    memcpy(ptr, &info, sizeof(GroupMemberInfo));
                    ptr += sizeof(GroupMemberInfo);
                }
                sendPacket(MSG_GROUP_MEMBER_LIST_RESP, body);
            });
        break;
    }
    case MSG_INVITE_TO_GROUP_REQ: {
        if (bodyData.size() < (int)sizeof(InviteToGroupReq)) break;
        InviteToGroupReq req;
        memcpy(&req, bodyData.constData(), sizeof(req));
        const int groupId = req.groupId;
        const int targetUserId = req.targetUserId;
        const int inviterId = m_uid;
        const QString inviterName = m_userName;
        DbWorkerPool::instance().enqueue(
            [groupId, targetUserId, inviterId, inviterName](DBManager& db) {
                QVariantMap out;
                const bool ok = db.addGroupMember(groupId, targetUserId, 0);
                out["ok"] = ok;
                out["groupName"] = QString();
                if (ok) {
                    const auto groups = db.getGroupList(targetUserId);
                    for (const auto& group : groups) {
                        if (group.groupId == groupId) {
                            out["groupName"] = QString::fromUtf8(group.groupName);
                            break;
                        }
                    }
                }
                out["inviterId"] = inviterId;
                out["inviterName"] = inviterName;
                return out;
            },
            this,
            [groupId, targetUserId](const QVariant& result) {
                const QVariantMap out = result.toMap();
                if (!out.value("ok").toBool()) {
                    return;
                }
                InviteToGroupNotify notify;
                memset(&notify, 0, sizeof(notify));
                notify.groupId = groupId;
                notify.inviterId = out.value("inviterId").toInt();
                strncpy(notify.groupName, out.value("groupName").toString().toUtf8().constData(), 63);
                strncpy(notify.inviterName, out.value("inviterName").toString().toUtf8().constData(), 31);
                TcpServer::instance().sendToUser(
                    targetUserId,
                    MSG_INVITE_TO_GROUP_NOTIFY,
                    QByteArray((char*)&notify, sizeof(notify)),
                    0,
                    targetUserId);
            });
        break;
    }
    case MSG_LEAVE_GROUP_REQ: {
        if (bodyData.size() < (int)sizeof(LeaveGroupReq)) break;
        LeaveGroupReq req;
        memcpy(&req, bodyData.constData(), sizeof(req));
        const int groupId = req.groupId;
        const int currentUid = m_uid;
        DbWorkerPool::instance().enqueue(
            [groupId, currentUid](DBManager& db) {
                return QVariant(db.removeGroupMember(groupId, currentUid));
            },
            this,
            [this, groupId](const QVariant& result) {
                LeaveGroupResp resp;
                resp.result = result.toBool() ? 1 : 0;
                resp.groupId = groupId;
                sendPacket(MSG_LEAVE_GROUP_RESP, QByteArray((char*)&resp, sizeof(resp)));
            });
        break;
    }
    case MSG_GROUP_CHAT_TEXT: {
        if (bodyData.size() < (int)sizeof(GroupChatMessage)) break;
        GroupChatMessage req;
        memcpy(&req, bodyData.constData(), sizeof(req));
        const int groupId = req.groupId;
        const int senderId = m_uid;
        const QString senderName = m_userName;
        const QByteArray content = bodyData.mid(sizeof(GroupChatMessage));
        DbWorkerPool::instance().enqueue(
            [groupId, senderId, content](DBManager& db) {
                db.saveGroupMessage(groupId, senderId, content);
                return QVariant::fromValue(QSharedPointer<QList<int>>::create(db.getGroupMemberIds(groupId)));
            },
            this,
            [this, groupId, senderId, senderName, content](const QVariant& result) {
                const auto memberIdsPtr = result.value<QSharedPointer<QList<int>>>();
                const QList<int> memberIds = memberIdsPtr ? *memberIdsPtr : QList<int>();
                GroupChatMessage header;
                memset(&header, 0, sizeof(header));
                header.groupId = groupId;
                header.senderId = senderId;
                strncpy(header.senderName, senderName.toUtf8().constData(), 31);
                QByteArray forwardBody;
                forwardBody.append((char*)&header, sizeof(header));
                forwardBody.append(content);
                for (int memberId : memberIds) {
                    if (memberId == senderId) continue;
                    if (!TcpServer::instance().sendToUser(memberId, MSG_GROUP_CHAT_TEXT, forwardBody, senderId, memberId)) {
                        DbWorkerPool::instance().enqueue(
                            [groupId, senderId, memberId, content](DBManager& db) {
                                db.saveGroupOfflineMessage(groupId, senderId, memberId, content);
                                return QVariant();
                            },
                            this);
                    }
                }
            });
        break;
    }
    case MSG_GROUP_CHAT_HISTORY_REQ: {
        if (bodyData.size() < (int)sizeof(int)) break;
        const int groupId = *(int*)bodyData.constData();
        DbWorkerPool::instance().enqueue(
            [groupId](DBManager& db) {
                return QVariant::fromValue(QSharedPointer<GroupHistoryList>::create(db.getGroupChatHistory(groupId, 200)));
            },
            this,
            [this, groupId](const QVariant& result) {
                const auto historyPtr = result.value<QSharedPointer<GroupHistoryList>>();
                const GroupHistoryList history = historyPtr ? *historyPtr : GroupHistoryList();
                QByteArray body;
                QDataStream out(&body, QIODevice::WriteOnly);
                out.setByteOrder(QDataStream::LittleEndian);
                out << quint32(groupId);
                out << quint32(history.size());
                for (const auto& item : history) {
                    const QByteArray nameBytes = std::get<1>(item).toUtf8();
                    const QByteArray content = std::get<2>(item);
                    out << quint32(std::get<0>(item));
                    out << quint64(std::get<3>(item));
                    out << quint32(nameBytes.size());
                    out.writeRawData(nameBytes.constData(), nameBytes.size());
                    out << quint32(content.size());
                    out.writeRawData(content.constData(), content.size());
                }
                sendPacket(MSG_GROUP_CHAT_HISTORY_RESP, body, groupId, m_uid);
            });
        break;
    }
    case MSG_FILE_TRANSFER_REQ:
    case MSG_FILE_TRANSFER_RESP:
    case MSG_FILE_CHUNK:
    case MSG_FILE_TRANSFER_ACK:
    case MSG_FILE_RESUME_REQ:
    case MSG_FILE_RESUME_RESP:
    case MSG_FILE_VERIFY_REQ:
    case MSG_FILE_VERIFY_RESP: {
        const int targetId = destId;
        if (!TcpServer::instance().sendToUser(targetId, msgType, bodyData, m_uid, targetId)
            && msgType == MSG_FILE_RESUME_REQ) {
            FileResumeResp resp;
            memset(&resp, 0, sizeof(resp));
            if (bodyData.size() >= (int)sizeof(FileResumeReq)) {
                FileResumeReq req;
                memcpy(&req, bodyData.constData(), sizeof(req));
                strncpy(resp.fileId, req.fileId, 63);
            }
            resp.canResume = 0;
            sendPacket(MSG_FILE_RESUME_RESP, QByteArray((char*)&resp, sizeof(resp)), targetId, m_uid);
        }
        break;
    }
    default:
        break;
    }
}

void ClientSocket::sendPacket(uint32_t type, const QByteArray& body, uint32_t src, uint32_t dest)
{
    if (state() != QAbstractSocket::ConnectedState) {
        return;
    }
    if (bytesToWrite() > MAX_PENDING_WRITE_BYTES) {
        LOG_WARN(QString("Closing slow client uid=%1 pending_write=%2").arg(m_uid).arg(bytesToWrite()));
        abort();
        return;
    }
    write(makePacket(type, body, src, dest));
    ServerStats::instance().packetSent();
}

void ClientSocket::checkHeartbeatTimeout()
{
    if (state() != QAbstractSocket::ConnectedState) {
        return;
    }

    const qint64 idleMs = QDateTime::currentMSecsSinceEpoch() - m_lastActiveMs;
    if (idleMs > m_heartbeatTimeoutMs) {
        LOG_WARN(QString("Closing idle client uid=%1 idle_ms=%2 timeout_ms=%3")
                     .arg(m_uid)
                     .arg(idleMs)
                     .arg(m_heartbeatTimeoutMs));
        abort();
    }
}

bool ClientSocket::isFilePacket(uint32_t msgType) const
{
    return msgType == MSG_FILE_CHUNK;
}

void ClientSocket::closeForProtocolError(const QString& reason)
{
    LOG_WARN(QString("Closing client for protocol error: %1").arg(reason));
    abort();
}

void ClientSocket::writePacket(ClientSocket *target, uint32_t type, const QByteArray &body, uint32_t src, uint32_t dest)
{
    if (target && target->state() == QAbstractSocket::ConnectedState) {
        QMetaObject::invokeMethod(target, "sendPacket",
                                  Qt::QueuedConnection,
                                  Q_ARG(uint32_t, type),
                                  Q_ARG(QByteArray, body),
                                  Q_ARG(uint32_t, src),
                                  Q_ARG(uint32_t, dest));
    } else {
        LOG_ERROR("Target socket is invalid or disconnected. Cannot forward message.");
    }
}

void ClientSocket::notifyFriends(int status)
{
    const int currentUid = m_uid;
    if (currentUid == 0) {
        return;
    }
    DbWorkerPool::instance().enqueue(
        [currentUid](DBManager& db) {
            return QVariant::fromValue(QSharedPointer<QList<int>>::create(db.getFriendIds(currentUid)));
        },
        this,
        [this, currentUid, status](const QVariant& result) {
            const auto friendIdsPtr = result.value<QSharedPointer<QList<int>>>();
            const QList<int> friendIds = friendIdsPtr ? *friendIdsPtr : QList<int>();
            for (int friendId : friendIds) {
                ClientSocket* friendSocket = TcpServer::instance().getUserSocket(friendId);
                if (friendSocket) {
                    FriendStatusChange notify;
                    notify.uid = currentUid;
                    notify.status = status;
                    QByteArray data((char*)&notify, sizeof(FriendStatusChange));
                    TcpServer::instance().sendToUser(friendId, MSG_FRIEND_STATUS_NOTIFY, data, 0, friendId);
                }
            }
        });
}

void ClientSocket::pushOfflineMsgs(const QList<QPair<int, QByteArray> > &offlineMsgs)
{
    for (const auto &msgPair : offlineMsgs) {
        sendPacket(MSG_CHAT_TEXT, msgPair.second, msgPair.first, m_uid);
    }
}

void ClientSocket::pushFriendRequests(const QList<QPair<int, QString> > &pendingReqs)
{
    for (const auto &req : pendingReqs) {
        AddFriendNotify notify;
        notify.requesterId = req.first;
        strncpy(notify.requesterName, req.second.toUtf8().constData(), 31);
        notify.requesterName[31] = '\0';
        sendPacket(MSG_ADD_FRIEND_NOTIFY, QByteArray((char*)&notify, sizeof(AddFriendNotify)), 0, m_uid);
    }
}

void ClientSocket::pushGroupOfflineMsgs(const QList<std::tuple<int, int, QString, QByteArray>>& offlineMsgs)
{
    for (const auto &msg : offlineMsgs) {
        GroupChatMessage header;
        header.groupId = std::get<0>(msg);
        header.senderId = std::get<1>(msg);
        strncpy(header.senderName, std::get<2>(msg).toUtf8().constData(), 31);
        header.senderName[31] = '\0';

        QByteArray body;
        body.append((char*)&header, sizeof(GroupChatMessage));
        body.append(std::get<3>(msg));
        sendPacket(MSG_GROUP_CHAT_TEXT, body, header.senderId, m_uid);
    }
}
