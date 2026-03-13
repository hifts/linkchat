#include "clientsocket.h"
#include "dbmanager.h"
#include "tcpserver.h"
#include "logger.h"

#include <QDataStream>
#include <QTimer>

ClientSocket::ClientSocket(QObject *parent)
    : QTcpSocket{parent}
{
    connect(this,&QTcpSocket::readyRead,this,&ClientSocket::onReadyRead);

    connect(this,&QTcpSocket::disconnected,this,[this](){
        if(this->m_uid != 0){
            TcpServer::instance().userLogout(m_uid);

            notifyFriends(0);
        }
    });
}

void ClientSocket::onReadyRead()
{
    m_buffer.append(this->readAll());

    while (m_buffer.size() >= (int)sizeof(PDUHeader)) {
        while (m_buffer.size() >= (int)sizeof(uint32_t)) {
            uint32_t magic = 0;
            memcpy(&magic, m_buffer.data(), sizeof(uint32_t));
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
                qWarning("[Server] Packet resync: skipping %d bytes to next magic", nextIdx);
                m_buffer.remove(0, nextIdx);
            } else {
                qWarning("[Server] Packet resync: magic not found, dropping buffer except last 3 bytes");
                int keep = qMin(3, m_buffer.size());
                m_buffer.remove(0, m_buffer.size() - keep);
            }
        }

        if (m_buffer.size() < (int)sizeof(PDUHeader)) break;

        PDUHeader header;
        memcpy(&header, m_buffer.data(), sizeof(PDUHeader));
        uint32_t totalLen = header.total_len;

        const uint32_t MAX_PACKET_LEN = 50u * 1024u * 1024u;
        if (totalLen < sizeof(PDUHeader) || totalLen > MAX_PACKET_LEN) {
            qWarning("[Server] Invalid packet length: %u, skipping magic and resyncing", totalLen);
            m_buffer.remove(0, sizeof(uint32_t));
            continue;
        }

        if (m_buffer.size() < (int)totalLen) break;

        QByteArray bodyData = m_buffer.mid(sizeof(PDUHeader), totalLen - sizeof(PDUHeader));
        uint32_t msgType = header.msg_type;
        uint32_t srcId   = header.src_id;
        uint32_t destId  = header.dest_id;

        m_buffer.remove(0, totalLen);

        switch (msgType) {
        case MSG_HEARTBEAT_REQ:{
            this->write(makePacket(MSG_HEARTBEAT_RESP,QByteArray()));
            break;
        }
        case MSG_REGISTER_REQ:{
            if (bodyData.size() < (int)sizeof(RegisterReq)) break;
            RegisterReq *req = (RegisterReq*)bodyData.data();

            QString passwordHashBase64 = QString::fromUtf8(req->passwordHash);
            QString saltBase64 = QString::fromUtf8(req->salt);
            
            qDebug() << "[Server] Registration request for user:" << req->userName;

            QByteArray salt = QByteArray::fromBase64(saltBase64.toUtf8());
            
            bool ok = DBManager::instance().handelRegister(
                req->userName, 
                passwordHashBase64,
                salt
            );

            LoginResp resp;
            resp.result = ok? 1 : 0;
            resp.userId = 0;
            this->write(makePacket(MSG_REGISTER_RESP,QByteArray((char*)&resp,sizeof(LoginResp))));
            break;
        }
        case MSG_LOGIN_SALT_REQ:{
            if (bodyData.size() < (int)sizeof(LoginSaltReq)) break;
            LoginSaltReq *req = (LoginSaltReq*)bodyData.data();

            QByteArray salt;
            const bool ok = DBManager::instance().getUserSalt(req->userName, salt);

            LoginSaltResp resp;
            memset(&resp, 0, sizeof(resp));
            resp.result = ok ? 1 : 0;

            if (ok) {
                const QByteArray saltBase64 = salt.toBase64();
                strncpy(resp.salt, saltBase64.constData(), sizeof(resp.salt) - 1);
            }

            this->write(makePacket(MSG_LOGIN_SALT_RESP, QByteArray((char*)&resp, sizeof(resp))));
            break;
        }
        case MSG_LOGIN_REQ:{
            if (bodyData.size() < (int)sizeof(LoginReq)) break;
            LoginReq *req = (LoginReq*)bodyData.data();

            int uid = -1;
            QString name = req->userName;
            QByteArray salt, passwordHash;

            bool ok = DBManager::instance().handleLogin(req->userName, req->passwordHash, uid, salt, passwordHash);

            LoginResp resp;
            resp.result = ok? 1 : 0;
            resp.userId = uid;

            if(ok){

                if(TcpServer::instance().isOnline(uid)){
                    resp.result = 2;
                    this->write(makePacket(MSG_LOGIN_RESP,QByteArray((char*)&resp,sizeof(LoginResp))));
                    break;
                }

                this->write(makePacket(MSG_LOGIN_RESP,QByteArray((char*)&resp,sizeof(LoginResp))));

                this->m_uid = uid;
                this->m_userName = name;

                TcpServer::instance().userLogin(uid,this);
                notifyFriends(1);

                auto pengingRequests = DBManager::instance().getPendingRequests(uid);
                auto offlineMsgs = DBManager::instance().getAndClearOfflineMessages(uid);
                auto groupOfflineMsgs = DBManager::instance().getAndClearGroupOfflineMessages(uid);

                if(!offlineMsgs.isEmpty() || !pengingRequests.isEmpty() || !groupOfflineMsgs.isEmpty()){
                    QTimer::singleShot(1500, this, [=](){
                        pushFriendRequests(pengingRequests);
                        pushOfflineMsgs(offlineMsgs);
                        pushGroupOfflineMsgs(groupOfflineMsgs);
                    });
                }
            }
            break;
        }
        case MSG_FRIEND_LIST_REQ:{
            int currentUid = this->m_uid;

            QList<FriendInfo> friendList = DBManager::instance().getFriendList(currentUid);

            int count = friendList.size();
            int bodyLen = sizeof(int) + count * sizeof(FriendInfo);

            QByteArray resBody;
            resBody.resize(bodyLen);

            char *ptr = resBody.data();
            memcpy(ptr,&count,sizeof(int));
            ptr += sizeof(int);

            for(const auto &info : friendList){
                memcpy(ptr,&info,sizeof(FriendInfo));
                ptr += sizeof(FriendInfo);
            }

            this->write(makePacket(MSG_FRIEND_LIST_RESP,resBody));
            break;
        }
        case MSG_CHAT_TEXT:{
            int targetId = destId;

            DBManager::instance().saveChatMessage(this->m_uid,targetId,bodyData);

            ClientSocket *targetSocket = TcpServer::instance().getUserSocket(targetId);

            if(targetSocket != nullptr){
                this->writePacket(targetSocket,MSG_CHAT_TEXT,bodyData,this->m_uid,targetId);
            }else{
                DBManager::instance().saveOfflineMessage(this->m_uid,targetId,bodyData);
            }
            break;
        }
        case MSG_CHAT_HISTORY_REQ:{
            if(bodyData.size() < 4){
                break;
            }
            int friendId = *(int*)bodyData.constData();

            auto history = DBManager::instance().getChatHistory(m_uid,friendId,200);
            QByteArray resBytes;
            QDataStream out(&resBytes,QIODevice::WriteOnly);
            out.setByteOrder(QDataStream::LittleEndian);
            out<<quint32(history.size());

            for (const auto &item : history) {
                out << quint32(std::get<0>(item));  // senderId
                out << quint64(std::get<2>(item));  // timestamp
                out << quint32(std::get<1>(item).size());  // content size
                out.writeRawData(std::get<1>(item).constData(), std::get<1>(item).size());  // content
            }
            this->write(makePacket(MSG_CHAT_HISTORY_RESP, resBytes, friendId, m_uid));
            break;
        }
        case MSG_SEARCH_USER_REQ:{
            if (bodyData.size() < (int)sizeof(SearchReq)) break;
            SearchReq *req = (SearchReq*)bodyData.data();
            QString keyword = QString::fromUtf8(req->keyword).trimmed();

            QList<FriendInfo> resultList = DBManager::instance().searchUsers(keyword,m_uid);

            int count = resultList.size();
            int bodyLen = sizeof(int) + sizeof(FriendInfo) * count;

            QByteArray searchRespBody;
            searchRespBody.resize(bodyLen);

            char *ptr = searchRespBody.data();

            memcpy(ptr, &count, sizeof(int));
            ptr += sizeof(int);

            for(const auto &info : resultList){
                memcpy(ptr, &info, sizeof(FriendInfo));
                ptr += sizeof(FriendInfo);
            }

            this->write(makePacket(MSG_SEARCH_USER_RESP,searchRespBody));
            break;
        }
        case MSG_ADD_FRIEND_REQ:{
            if (bodyData.size() < (int)sizeof(AddFriendReq)) break;
            AddFriendReq *req = (AddFriendReq*)bodyData.data();

            int targetId = req->targetId;
            int requesterId = this->m_uid;
            QString name = this->m_userName;

            if (DBManager::instance().isFriend(requesterId, targetId)) {
                break;
            }

            if (DBManager::instance().hasPendingRequest(requesterId, targetId)) {
                break;
            }

            DBManager::instance().saveFriendRequest(requesterId,name,targetId);

            ClientSocket *target = TcpServer::instance().getUserSocket(targetId);
            if(target){
                AddFriendNotify notify;
                notify.requesterId = requesterId;

                strncpy(notify.requesterName,name.toUtf8().constData(),31);
                notify.requesterName[31] = '\0';

                target->write(makePacket(MSG_ADD_FRIEND_NOTIFY,QByteArray((char*)&notify,sizeof(AddFriendNotify)),0,targetId));
            }
            break;
        }
        case MSG_ADD_FRIEND_RESP:{
            if (bodyData.size() < (int)sizeof(AddFriendResp)) break;
            AddFriendResp *resp = (AddFriendResp*)bodyData.data();

            int requesterId = resp->requesterId;
            bool accepted = resp->accepted;
            int responderId = this->m_uid;

            if(accepted){
                DBManager::instance().addFriend(requesterId,responderId);
            }

            DBManager::instance().markRequestProcessed(requesterId, responderId,accepted);

            ClientSocket *requesterSocket = TcpServer::instance().getUserSocket(requesterId);
            if (requesterSocket) {
                requesterSocket->write(makePacket(MSG_ADD_FRIEND_RESULT, bodyData, 0, requesterId));
            }
            break;
        }
        case MSG_DELETE_FRIEND_REQ:{
            if (bodyData.size() < (int)sizeof(DeleteFriendReq)) break;
            DeleteFriendReq *req = (DeleteFriendReq*)bodyData.data();
            
            int targetId = req->targetId;
            int requesterId = this->m_uid;
            
            bool success = DBManager::instance().deleteFriend(requesterId, targetId);
            
            DeleteFriendResp res;
            res.result = success ? 1 : 0;
            res.targetId = targetId;
            
            this->write(makePacket(MSG_DELETE_FRIEND_RESP, QByteArray((char*)&res, sizeof(DeleteFriendResp))));
            break;
        }
        case MSG_FILE_TRANSFER_REQ:{
            int targetId = destId;
            ClientSocket *targetSocket = TcpServer::instance().getUserSocket(targetId);

            if(targetSocket){
                this->writePacket(targetSocket,MSG_FILE_TRANSFER_REQ,bodyData,this->m_uid,targetId);
            }
            break;
        }
        case MSG_FILE_TRANSFER_RESP: {
            int targetId = destId;
            ClientSocket *targetSocket = TcpServer::instance().getUserSocket(targetId);

            if (targetSocket) {
                this->writePacket(targetSocket, MSG_FILE_TRANSFER_RESP,bodyData, this->m_uid, targetId);
            }
            break;
        }
        case MSG_FILE_CHUNK:{
            int targetId = destId;
            ClientSocket *targetSocket = TcpServer::instance().getUserSocket(targetId);

            if(targetSocket){
                this->writePacket(targetSocket,MSG_FILE_CHUNK,bodyData,this->m_uid,targetId);
            }
            break;
        }
        case MSG_FILE_RESUME_REQ: {
            int targetId = destId;
            ClientSocket *targetSocket = TcpServer::instance().getUserSocket(targetId);

            if(targetSocket){
                this->writePacket(targetSocket, MSG_FILE_RESUME_REQ, bodyData, this->m_uid, targetId);
            }else{
                FileResumeResp resp;
                memset(&resp, 0, sizeof(resp));
                if(bodyData.size() >= (int)sizeof(FileResumeReq)){
                    FileResumeReq *req = (FileResumeReq*)bodyData.data();
                    strncpy(resp.fileId, req->fileId, 63);
                }
                resp.canResume = 0;
                this->write(makePacket(MSG_FILE_RESUME_RESP, QByteArray((char*)&resp, sizeof(resp)), targetId, this->m_uid));
            }
            break;
        }
        case MSG_FILE_RESUME_RESP: {
            int targetId = destId;
            ClientSocket *targetSocket = TcpServer::instance().getUserSocket(targetId);

            if(targetSocket){
                this->writePacket(targetSocket, MSG_FILE_RESUME_RESP, bodyData, this->m_uid, targetId);
            }
            break;
        }
        case MSG_FILE_VERIFY_REQ: {
            int targetId = destId;
            ClientSocket *targetSocket = TcpServer::instance().getUserSocket(targetId);

            if(targetSocket){
                this->writePacket(targetSocket, MSG_FILE_VERIFY_REQ, bodyData, this->m_uid, targetId);
            }
            break;
        }
        case MSG_FILE_VERIFY_RESP: {
            int targetId = destId;
            ClientSocket *targetSocket = TcpServer::instance().getUserSocket(targetId);

            if(targetSocket){
                this->writePacket(targetSocket, MSG_FILE_VERIFY_RESP, bodyData, this->m_uid, targetId);
            }
            break;
        }
        case MSG_CREATE_GROUP_REQ: {
            if (bodyData.size() < (int)sizeof(CreateGroupReq)) break;
            CreateGroupReq *req = (CreateGroupReq*)bodyData.data();
            QString groupName = QString::fromUtf8(req->groupName);

            int groupId = DBManager::instance().createGroup(groupName, this->m_uid);

            CreateGroupResp resp;
            resp.result = (groupId > 0) ? 1 : 0;
            resp.groupId = groupId;
            this->write(makePacket(MSG_CREATE_GROUP_RESP, QByteArray((char*)&resp, sizeof(CreateGroupResp))));
            break;
        }
        case MSG_GROUP_LIST_REQ: {
            QList<GroupInfo> groupList = DBManager::instance().getGroupList(this->m_uid);

            int count = groupList.size();
            int bodyLen = sizeof(int) + count * sizeof(GroupInfo);

            QByteArray groupRespBody;
            groupRespBody.resize(bodyLen);
            char *ptr = groupRespBody.data();

            memcpy(ptr, &count, sizeof(int));
            ptr += sizeof(int);

            for (const auto &info : groupList) {
                memcpy(ptr, &info, sizeof(GroupInfo));
                ptr += sizeof(GroupInfo);
            }

            this->write(makePacket(MSG_GROUP_LIST_RESP, groupRespBody));
            break;
        }
        case MSG_GROUP_MEMBER_LIST_REQ: {
            if (bodyData.size() < 4) break;
            int groupId = *(int*)bodyData.constData();

            QList<GroupMemberInfo> memberList = DBManager::instance().getGroupMembers(groupId);

            int count = memberList.size();
            int bodyLen = sizeof(int) + sizeof(int) + count * sizeof(GroupMemberInfo);

            QByteArray resBody;
            resBody.resize(bodyLen);
            char *ptr = resBody.data();

            memcpy(ptr, &groupId, sizeof(int));
            ptr += sizeof(int);
            memcpy(ptr, &count, sizeof(int));
            ptr += sizeof(int);

            for (const auto &info : memberList) {
                memcpy(ptr, &info, sizeof(GroupMemberInfo));
                ptr += sizeof(GroupMemberInfo);
            }

            this->write(makePacket(MSG_GROUP_MEMBER_LIST_RESP, resBody));
            break;
        }
        case MSG_INVITE_TO_GROUP_REQ: {
            if (bodyData.size() < (int)sizeof(InviteToGroupReq)) break;
            InviteToGroupReq *req = (InviteToGroupReq*)bodyData.data();
            int groupId = req->groupId;
            int targetUserId = req->targetUserId;

            bool success = DBManager::instance().addGroupMember(groupId, targetUserId, 0);

            if (success) {
                ClientSocket* targetSocket = TcpServer::instance().getUserSocket(targetUserId);
                if (targetSocket) {
                    InviteToGroupNotify notify;
                    notify.groupId = groupId;
                    notify.inviterId = this->m_uid;

                    QList<GroupInfo> groups = DBManager::instance().getGroupList(targetUserId);
                    for (const auto& g : groups) {
                        if (g.groupId == groupId) {
                            strncpy(notify.groupName, g.groupName, 63);
                            break;
                        }
                    }
                    strncpy(notify.inviterName, this->m_userName.toUtf8().constData(), 31);

                    targetSocket->write(makePacket(MSG_INVITE_TO_GROUP_NOTIFY,
                                                   QByteArray((char*)&notify, sizeof(InviteToGroupNotify)), 0, targetUserId));
                }
            }
            break;
        }
        case MSG_LEAVE_GROUP_REQ: {
            if (bodyData.size() < (int)sizeof(LeaveGroupReq)) break;
            LeaveGroupReq *req = (LeaveGroupReq*)bodyData.data();
            bool success = DBManager::instance().removeGroupMember(req->groupId, this->m_uid);
            
            LeaveGroupResp resp;
            resp.result = success ? 1 : 0;
            resp.groupId = req->groupId;
            
            this->write(makePacket(MSG_LEAVE_GROUP_RESP, QByteArray((char*)&resp, sizeof(LeaveGroupResp))));
            break;
        }
        case MSG_GROUP_CHAT_TEXT: {
            if (bodyData.size() < (int)sizeof(GroupChatMessage)) break;

            GroupChatMessage *msgHeader = (GroupChatMessage*)bodyData.data();
            int groupId = msgHeader->groupId;

            QByteArray msgContent = bodyData.mid(sizeof(GroupChatMessage));

            DBManager::instance().saveGroupMessage(groupId, this->m_uid, msgContent);

            QList<int> memberIds = DBManager::instance().getGroupMemberIds(groupId);

            GroupChatMessage forwardHeader;
            forwardHeader.groupId = groupId;
            forwardHeader.senderId = this->m_uid;
            strncpy(forwardHeader.senderName, this->m_userName.toUtf8().constData(), 31);
            forwardHeader.senderName[31] = '\0';

            QByteArray forwardBody;
            forwardBody.append((char*)&forwardHeader, sizeof(GroupChatMessage));
            forwardBody.append(msgContent);

            for (int memberId : memberIds) {
                if (memberId == this->m_uid) continue;

                ClientSocket* memberSocket = TcpServer::instance().getUserSocket(memberId);
                if (memberSocket) {
                    memberSocket->write(makePacket(MSG_GROUP_CHAT_TEXT, forwardBody, this->m_uid, memberId));
                } else {
                    DBManager::instance().saveGroupOfflineMessage(groupId, this->m_uid, memberId, msgContent);
                }
            }
            break;
        }
        case MSG_GROUP_CHAT_HISTORY_REQ: {
            if (bodyData.size() < 4) break;
            int groupId = *(int*)bodyData.constData();

            auto history = DBManager::instance().getGroupChatHistory(groupId, 200);

            QByteArray res;
            QDataStream out(&res, QIODevice::WriteOnly);
            out.setByteOrder(QDataStream::LittleEndian);

            out << quint32(groupId);
            out << quint32(history.size());

            for (const auto &item : history) {
                int senderId = std::get<0>(item);
                QString senderName = std::get<1>(item);
                QByteArray content = std::get<2>(item);
                quint64 timestamp = std::get<3>(item);

                out << quint32(senderId);
                out << quint64(timestamp);
                QByteArray nameBytes = senderName.toUtf8();
                out << quint32(nameBytes.size());
                out.writeRawData(nameBytes.constData(), nameBytes.size());
                out << quint32(content.size());
                out.writeRawData(content.constData(), content.size());
            }

            this->write(makePacket(MSG_GROUP_CHAT_HISTORY_RESP, res, groupId, this->m_uid));
            break;
        }
        default:
            break;
        }
    }
}

void ClientSocket::writePacket(ClientSocket *target, int type, const QByteArray &body, int src, int dest)
{
    if(target && target->state() == QAbstractSocket::ConnectedState){

        QByteArray data = makePacket(type,body,src,dest);

        target->write(data);

    }else{
        LOG_ERROR("Target socket is invalid or disconnected. Cannot forward message.");
    }
}

void ClientSocket::notifyFriends(int status)
{
    QList<int> friendIds = DBManager::instance().getFriendIds(this->m_uid);

    for (int friendId : friendIds) {
        ClientSocket* friendSocket = TcpServer::instance().getUserSocket(friendId);

        if (friendSocket) {
            FriendStatusChange notify;
            notify.uid = this->m_uid;
            notify.status = status;

            QByteArray data((char*)&notify, sizeof(FriendStatusChange));
            writePacket(friendSocket, MSG_FRIEND_STATUS_NOTIFY, data, 0, friendId);
        }
    }
}

void ClientSocket::pushOfflineMsgs(const QList<QPair<int, QByteArray> > &offlineMsgs)
{
    for(const auto &msgPair : offlineMsgs){
        int senderId = msgPair.first;
        QByteArray content = msgPair.second;

        QByteArray packet = makePacket(MSG_CHAT_TEXT, content, senderId, this->m_uid);
        this->write(packet);
    }
}

void ClientSocket::pushFriendRequests(const QList<QPair<int, QString> > &pendingReqs)
{
    for (const auto &req : pendingReqs) {
        int requesterId = req.first;
        QString requesterName = req.second;

        AddFriendNotify notify;
        notify.requesterId = requesterId;
        strncpy(notify.requesterName, requesterName.toUtf8().constData(), 32);

        this->write(makePacket(MSG_ADD_FRIEND_NOTIFY,QByteArray((char*)&notify, sizeof(AddFriendNotify)), 0, this->m_uid));
    }
}

void ClientSocket::pushGroupOfflineMsgs(const QList<std::tuple<int, int, QString, QByteArray>>& offlineMsgs)
{
    for (const auto &msg : offlineMsgs) {
        int groupId = std::get<0>(msg);
        int senderId = std::get<1>(msg);
        QString senderName = std::get<2>(msg);
        QByteArray content = std::get<3>(msg);

        GroupChatMessage header;
        header.groupId = groupId;
        header.senderId = senderId;
        strncpy(header.senderName, senderName.toUtf8().constData(), 31);
        header.senderName[31] = '\0';

        QByteArray body;
        body.append((char*)&header, sizeof(GroupChatMessage));
        body.append(content);

        this->write(makePacket(MSG_GROUP_CHAT_TEXT, body, senderId, this->m_uid));
    }
}

