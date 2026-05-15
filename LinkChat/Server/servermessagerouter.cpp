#include "servermessagerouter.h"

#include "clientsocket.h"
#include "dbmanager.h"
#include "dbworkerpool.h"
#include "logger.h"
#include "serverstats.h"
#include "tcpserver.h"

#include <QAbstractSocket>
#include <QDataStream>
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

ServerMessageRouter::ServerMessageRouter(ClientSocket* socket)
    : m_socket(socket)
{
}

void ServerMessageRouter::dispatch(uint32_t msgType, uint32_t srcId, uint32_t destId, const QByteArray& bodyData)
{
    Q_UNUSED(srcId);
    ClientSocket* socket = m_socket;

    switch (msgType) {
    case MSG_HEARTBEAT_REQ: {
        ServerStats::instance().heartbeatReceived();
        socket->sendPacket(MSG_HEARTBEAT_RESP, QByteArray());
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
            socket,
            [socket](const QVariant& result) {
                LoginResp resp;
                resp.result = result.toBool() ? 1 : 0;
                resp.userId = 0;
                socket->sendPacket(MSG_REGISTER_RESP, QByteArray((char*)&resp, sizeof(resp)));
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
            socket,
            [socket](const QVariant& result) {
                const QVariantMap out = result.toMap();
                LoginSaltResp resp;
                memset(&resp, 0, sizeof(resp));
                resp.result = out.value("ok").toBool() ? 1 : 0;
                if (resp.result == 1) {
                    const QByteArray saltBase64 = out.value("salt").toByteArray().toBase64();
                    strncpy(resp.salt, saltBase64.constData(), sizeof(resp.salt) - 1);
                }
                socket->sendPacket(MSG_LOGIN_SALT_RESP, QByteArray((char*)&resp, sizeof(resp)));
            });
        break;
    }
    case MSG_LOGIN_REQ: {
        if (bodyData.size() < (int)sizeof(LoginReq) || socket->m_loginPending) break;
        socket->m_loginPending = true;

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
            socket,
            [socket, userName](const QVariant& result) {
                socket->m_loginPending = false;
                const QVariantMap out = result.toMap();
                LoginResp resp;
                resp.result = out.value("ok").toBool() ? 1 : 0;
                resp.userId = out.value("uid").toInt();

                if (resp.result == 1) {
                    if (TcpServer::instance().tryUserLogin(resp.userId, socket)) {
                        socket->m_uid = resp.userId;
                        socket->m_userName = userName;
                        ServerStats::instance().loginSucceeded();
                        socket->notifyFriends(1);
                        const int loginUid = resp.userId;
                        DbWorkerPool::instance().enqueue(
                            [loginUid](DBManager& db) {
                                QVariantMap data;
                                data["pending"] = QVariant::fromValue(QSharedPointer<PendingRequestList>::create(db.getPendingRequests(loginUid)));
                                data["offline"] = QVariant::fromValue(QSharedPointer<OfflineMessageList>::create(db.getAndClearOfflineMessages(loginUid)));
                                data["groupOffline"] = QVariant::fromValue(QSharedPointer<GroupOfflineMessageList>::create(db.getAndClearGroupOfflineMessages(loginUid)));
                                return data;
                            },
                            socket,
                            [socket, loginUid](const QVariant& result) {
                                if (socket->m_uid != loginUid || socket->state() != QAbstractSocket::ConnectedState) {
                                    return;
                                }
                                const QVariantMap data = result.toMap();
                                const auto pending = data.value("pending").value<QSharedPointer<PendingRequestList>>();
                                const auto offline = data.value("offline").value<QSharedPointer<OfflineMessageList>>();
                                const auto groupOffline = data.value("groupOffline").value<QSharedPointer<GroupOfflineMessageList>>();
                                QTimer::singleShot(200, socket, [socket, loginUid, pending, offline, groupOffline]() {
                                    if (socket->m_uid != loginUid || socket->state() != QAbstractSocket::ConnectedState) {
                                        return;
                                    }
                                    if (pending) socket->pushFriendRequests(*pending);
                                    if (offline) socket->pushOfflineMsgs(*offline);
                                    if (groupOffline) socket->pushGroupOfflineMsgs(*groupOffline);
                                });
                            });
                    } else {
                        resp.result = 2;
                        ServerStats::instance().loginFailed();
                    }
                } else {
                    ServerStats::instance().loginFailed();
                }
                socket->sendPacket(MSG_LOGIN_RESP, QByteArray((char*)&resp, sizeof(resp)));
            });
        break;
    }
    case MSG_FRIEND_LIST_REQ: {
        const int currentUid = socket->m_uid;
        DbWorkerPool::instance().enqueue(
            [currentUid](DBManager& db) {
                return QVariant::fromValue(QSharedPointer<QList<FriendInfo>>::create(db.getFriendList(currentUid)));
            },
            socket,
            [socket](const QVariant& result) {
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
                socket->sendPacket(MSG_FRIEND_LIST_RESP, resBody);
            });
        break;
    }
    case MSG_CHAT_TEXT: {
        const int senderId = socket->m_uid;
        const int targetId = destId;
        DbWorkerPool::instance().enqueue(
            [senderId, targetId, bodyData](DBManager& db) {
                db.saveChatMessage(senderId, targetId, bodyData);
                return QVariant();
            },
            socket);

        if (!TcpServer::instance().sendToUser(targetId, MSG_CHAT_TEXT, bodyData, senderId, targetId)) {
            DbWorkerPool::instance().enqueue(
                [senderId, targetId, bodyData](DBManager& db) {
                    db.saveOfflineMessage(senderId, targetId, bodyData);
                    return QVariant();
                },
                socket);
        }
        break;
    }
    case MSG_CHAT_HISTORY_REQ: {
        if (bodyData.size() < 4) break;
        const int friendId = *(int*)bodyData.constData();
        const int currentUid = socket->m_uid;
        DbWorkerPool::instance().enqueue(
            [currentUid, friendId](DBManager& db) {
                using HistoryList = QList<std::tuple<int, QByteArray, quint64>>;
                return QVariant::fromValue(QSharedPointer<HistoryList>::create(db.getChatHistory(currentUid, friendId, 200)));
            },
            socket,
            [socket, friendId](const QVariant& result) {
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
                socket->sendPacket(MSG_CHAT_HISTORY_RESP, resBytes, friendId, socket->m_uid);
            });
        break;
    }
    case MSG_SEARCH_USER_REQ: {
        if (bodyData.size() < (int)sizeof(SearchReq)) break;
        SearchReq req;
        memcpy(&req, bodyData.constData(), sizeof(req));
        const QString keyword = QString::fromUtf8(req.keyword).trimmed();
        const int currentUid = socket->m_uid;
        DbWorkerPool::instance().enqueue(
            [keyword, currentUid](DBManager& db) {
                return QVariant::fromValue(QSharedPointer<QList<FriendInfo>>::create(db.searchUsers(keyword, currentUid)));
            },
            socket,
            [socket](const QVariant& result) {
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
                socket->sendPacket(MSG_SEARCH_USER_RESP, body);
            });
        break;
    }
    case MSG_ADD_FRIEND_REQ: {
        if (bodyData.size() < (int)sizeof(AddFriendReq)) break;
        AddFriendReq req;
        memcpy(&req, bodyData.constData(), sizeof(req));
        const int requesterId = socket->m_uid;
        const int targetId = req.targetId;
        const QString requesterName = socket->m_userName;
        DbWorkerPool::instance().enqueue(
            [requesterId, targetId, requesterName](DBManager& db) {
                const bool canSave = !db.isFriend(requesterId, targetId)
                                     && !db.hasPendingRequest(requesterId, targetId);
                if (canSave) {
                    db.saveFriendRequest(requesterId, requesterName, targetId);
                }
                return QVariant(canSave);
            },
            socket,
            [socket, requesterId, targetId, requesterName](const QVariant& result) {
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
        const int responderId = socket->m_uid;
        const bool accepted = resp.accepted;
        DbWorkerPool::instance().enqueue(
            [requesterId, responderId, accepted](DBManager& db) {
                if (accepted) {
                    db.addFriend(requesterId, responderId);
                }
                db.markRequestProcessed(requesterId, responderId, accepted);
                return QVariant(true);
            },
            socket,
            [requesterId, bodyData](const QVariant&) {
                TcpServer::instance().sendToUser(requesterId, MSG_ADD_FRIEND_RESULT, bodyData, 0, requesterId);
            });
        break;
    }
    case MSG_DELETE_FRIEND_REQ: {
        if (bodyData.size() < (int)sizeof(DeleteFriendReq)) break;
        DeleteFriendReq req;
        memcpy(&req, bodyData.constData(), sizeof(req));
        const int requesterId = socket->m_uid;
        const int targetId = req.targetId;
        DbWorkerPool::instance().enqueue(
            [requesterId, targetId](DBManager& db) {
                return QVariant(db.deleteFriend(requesterId, targetId));
            },
            socket,
            [socket, targetId](const QVariant& result) {
                DeleteFriendResp resp;
                resp.result = result.toBool() ? 1 : 0;
                resp.targetId = targetId;
                socket->sendPacket(MSG_DELETE_FRIEND_RESP, QByteArray((char*)&resp, sizeof(resp)));
            });
        break;
    }
    case MSG_CREATE_GROUP_REQ: {
        if (bodyData.size() < (int)sizeof(CreateGroupReq)) break;
        CreateGroupReq req;
        memcpy(&req, bodyData.constData(), sizeof(req));
        const QString groupName = QString::fromUtf8(req.groupName);
        const int creatorId = socket->m_uid;
        DbWorkerPool::instance().enqueue(
            [groupName, creatorId](DBManager& db) {
                return QVariant(db.createGroup(groupName, creatorId));
            },
            socket,
            [socket](const QVariant& result) {
                CreateGroupResp resp;
                resp.groupId = result.toInt();
                resp.result = resp.groupId > 0 ? 1 : 0;
                socket->sendPacket(MSG_CREATE_GROUP_RESP, QByteArray((char*)&resp, sizeof(resp)));
            });
        break;
    }
    case MSG_GROUP_LIST_REQ: {
        const int currentUid = socket->m_uid;
        DbWorkerPool::instance().enqueue(
            [currentUid](DBManager& db) {
                return QVariant::fromValue(QSharedPointer<GroupInfoList>::create(db.getGroupList(currentUid)));
            },
            socket,
            [socket](const QVariant& result) {
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
                socket->sendPacket(MSG_GROUP_LIST_RESP, body);
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
            socket,
            [socket, groupId](const QVariant& result) {
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
                socket->sendPacket(MSG_GROUP_MEMBER_LIST_RESP, body);
            });
        break;
    }
    case MSG_INVITE_TO_GROUP_REQ: {
        if (bodyData.size() < (int)sizeof(InviteToGroupReq)) break;
        InviteToGroupReq req;
        memcpy(&req, bodyData.constData(), sizeof(req));
        const int groupId = req.groupId;
        const int targetUserId = req.targetUserId;
        const int inviterId = socket->m_uid;
        const QString inviterName = socket->m_userName;
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
            socket,
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
        const int currentUid = socket->m_uid;
        DbWorkerPool::instance().enqueue(
            [groupId, currentUid](DBManager& db) {
                return QVariant(db.removeGroupMember(groupId, currentUid));
            },
            socket,
            [socket, groupId](const QVariant& result) {
                LeaveGroupResp resp;
                resp.result = result.toBool() ? 1 : 0;
                resp.groupId = groupId;
                socket->sendPacket(MSG_LEAVE_GROUP_RESP, QByteArray((char*)&resp, sizeof(resp)));
            });
        break;
    }
    case MSG_GROUP_CHAT_TEXT: {
        if (bodyData.size() < (int)sizeof(GroupChatMessage)) break;
        GroupChatMessage req;
        memcpy(&req, bodyData.constData(), sizeof(req));
        const int groupId = req.groupId;
        const int senderId = socket->m_uid;
        const QString senderName = socket->m_userName;
        const QByteArray content = bodyData.mid(sizeof(GroupChatMessage));
        DbWorkerPool::instance().enqueue(
            [groupId, senderId, content](DBManager& db) {
                db.saveGroupMessage(groupId, senderId, content);
                return QVariant::fromValue(QSharedPointer<QList<int>>::create(db.getGroupMemberIds(groupId)));
            },
            socket,
            [socket, groupId, senderId, senderName, content](const QVariant& result) {
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
                            socket);
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
            socket,
            [socket, groupId](const QVariant& result) {
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
                socket->sendPacket(MSG_GROUP_CHAT_HISTORY_RESP, body, groupId, socket->m_uid);
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
        if (!TcpServer::instance().sendToUser(targetId, msgType, bodyData, socket->m_uid, targetId)
            && msgType == MSG_FILE_RESUME_REQ) {
            FileResumeResp resp;
            memset(&resp, 0, sizeof(resp));
            if (bodyData.size() >= (int)sizeof(FileResumeReq)) {
                FileResumeReq req;
                memcpy(&req, bodyData.constData(), sizeof(req));
                strncpy(resp.fileId, req.fileId, 63);
            }
            resp.canResume = 0;
            socket->sendPacket(MSG_FILE_RESUME_RESP, QByteArray((char*)&resp, sizeof(resp)), targetId, socket->m_uid);
        }
        break;
    }
    default:
        break;
    }

}
