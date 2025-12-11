#include "clientsocket.h"
#include "dbmanager.h"
#include "tcpserver.h"
#include "logger.h"

#include <QDataStream>
#include <QTimer>

ClientSocket::ClientSocket(QObject *parent)
    : QTcpSocket{parent}
{
    // 有数据过来就处理
    connect(this,&QTcpSocket::readyRead,this,&ClientSocket::onReadyRead);

    connect(this,&QTcpSocket::disconnected,this,[this](){
        if(this->m_uid != 0){
            // 处理用户下线
            TcpServer::instance().userLogout(m_uid);

            notifyFriends(0);
        }
    });

}

void ClientSocket::onReadyRead()
{
    // 把收到的数据追加到缓冲区
    m_buffer.append(this->readAll());

    while(m_buffer.size() >= (int)sizeof(PDUHeader)){

        // 先获取数据包的长度(总包长)
        PDUHeader *header = (PDUHeader*)m_buffer.data();
        uint32_t totalLen = header->total_len;

        // 如果缓冲区的数据不够一个包长，说明包没接收完，退出循环等待下一次数据
        if(m_buffer.size() < (int)totalLen){
            break;
        }

        // 获取body部分（总长 - 头部大小）
        QByteArray bodyData = m_buffer.mid(sizeof(PDUHeader),totalLen - sizeof(PDUHeader));
        uint32_t msgType = header->msg_type;

        switch (msgType) {
        case MSG_HEARTBEAT_REQ:{
            // 心跳包
            this->write(makePacket(MSG_HEARTBEAT_RESP,QByteArray()));
            break;
        }
        case MSG_REGISTER_REQ:{
            // 解包（登录/注册包）
            LoginReq *req = (LoginReq*)bodyData.data();

            // 查库
            bool ok = DBManager::instance().handelRegister(req->userName,req->password);

            // 回包
            LoginResp resp;
            resp.result = ok? 1 : 0;    // 1=成功，0=失败
            resp.userId = 0;
            this->write(makePacket(MSG_REGISTER_RESP,QByteArray((char*)&resp,sizeof(LoginResp))));
            break;
        }
        case MSG_LOGIN_REQ:{
            LoginReq *req = (LoginReq*)bodyData.data();

            // 记录用户在数据库中的id和用户名
            int uid = -1;
            QString name = req->userName;

            bool ok = DBManager::instance().handleLogin(req->userName,req->password,uid);

            // 回包
            LoginResp resp;
            resp.result = ok? 1 : 0;
            resp.userId = uid;

            // 登录成功就记录下当前socket属于哪个用户的以及用户名
            if(ok){

                if(TcpServer::instance().isOnline(uid)){
                    resp.result = 2;
                    this->write(makePacket(MSG_LOGIN_RESP,QByteArray((char*)&resp,sizeof(LoginResp))));
                    break;
                }

                this->write(makePacket(MSG_LOGIN_RESP,QByteArray((char*)&resp,sizeof(LoginResp))));

                this->m_uid = uid;
                this->m_userName = name;

                // 处理用户上线
                TcpServer::instance().userLogin(uid,this);
                notifyFriends(1);

                // 登录成功后，推送离线好友请求和离线消息
                auto pengingRequests = DBManager::instance().getPendingRequests(uid);
                auto offlineMsgs = DBManager::instance().getAndClearOfflineMessages(uid);
                auto groupOfflineMsgs = DBManager::instance().getAndClearGroupOfflineMessages(uid);

                // 逐条消息推送(延迟推送，避免客户端没初始化就推送导致消息缺失)
                if(!offlineMsgs.isEmpty() || !pengingRequests.isEmpty() || !groupOfflineMsgs.isEmpty()){
                    QTimer::singleShot(500, this, [=](){
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

            // 查当前用户的好友列表
            QList<FriendInfo> friendList = DBManager::instance().getFriendList(currentUid);

            // 回包
            int count = friendList.size();
            // 包主体的大小（好友列表信息）
            int bodyLen = sizeof(int) + count * sizeof(FriendInfo);

            QByteArray bodyData;
            bodyData.resize(bodyLen);

            char *ptr = bodyData.data();
            memcpy(ptr,&count,sizeof(int));     // 先拷贝好友数量
            ptr += sizeof(int);                 // 偏移存放好友数量的空间后再继续存放好友信息

            // 拷贝每一位好友信息
            for(const auto &info : friendList){
                memcpy(ptr,&info,sizeof(FriendInfo));
                ptr += sizeof(FriendInfo);
            }

            this->write(makePacket(MSG_FRIEND_LIST_RESP,bodyData));
            break;
        }
        case MSG_CHAT_TEXT:{
            // 获取要发送目的的用户id
            int targetId = header->dest_id;

            // 消息存入数据库
            DBManager::instance().saveChatMessage(this->m_uid,targetId,bodyData);

            // 查找用户是否在线
            ClientSocket *targetSocket = TcpServer::instance().getUserSocket(targetId);

            // 转发给目标用户
            if(targetSocket != nullptr){
                this->writePacket(targetSocket,MSG_CHAT_TEXT,bodyData,this->m_uid,targetId);
            }else{
                // 离线时存储消息到数据库
                DBManager::instance().saveOfflineMessage(this->m_uid,targetId,bodyData);
            }
            break;
        }
        case MSG_CHAT_HISTORY_REQ:{
            if(bodyData.size() < 4){
                return;
            }
            int friendId = *(int*)bodyData.constData();

            auto history = DBManager::instance().getChatHistory(m_uid,friendId,200);
            QByteArray resp;
            QDataStream out(&resp,QIODevice::WriteOnly);
            out.setByteOrder(QDataStream::LittleEndian);
            out<<quint32(history.size());

            for (const auto &p : history) {
                out << quint32(p.first);
                out << quint32(p.second.size());
                out.writeRawData(p.second.constData(), p.second.size());
            }
            this->write(makePacket(MSG_CHAT_HISTORY_RESP, resp, friendId, m_uid));
            break;
        }
        case MSG_SEARCH_USER_REQ:{
            SearchReq *req = (SearchReq*)bodyData.data();
            QString keyword = QString::fromUtf8(req->keyword).trimmed();

            QString msg = QString("User %1 searching for: %2").arg(m_uid).arg(keyword);
            LOG_INFO(msg);

            // 查库
            QList<FriendInfo> resultList = DBManager::instance().searchUsers(keyword,m_uid);

            // 组装回包
            int count = resultList.size();
            int bodyLen = sizeof(int) + sizeof(FriendInfo) * count;

            QByteArray respBody;
            respBody.resize(bodyLen);

            char *ptr = respBody.data();

            // 写入数量
            memcpy(ptr, &count, sizeof(int));
            ptr += sizeof(int);

            // 写入每一个用户信息
            for(const auto &info : resultList){
                memcpy(ptr, &info, sizeof(FriendInfo));
                ptr += sizeof(FriendInfo);
            }

            this->write(makePacket(MSG_SEARCH_USER_RESP,respBody));
            break;
        }
        case MSG_ADD_FRIEND_REQ:{
            AddFriendReq *req = (AddFriendReq*)bodyData.data();

            // 对方id和请求者的id
            int targetId = req->targetId;
            int requesterId = this->m_uid;
            QString name = this->m_userName;

            // 请求存入数据库
            DBManager::instance().saveFriendRequest(requesterId,name,targetId);

            // 直接转发给目标用户（如果在线）
            ClientSocket *target = TcpServer::instance().getUserSocket(targetId);
            if(target){
                AddFriendNotify notify;
                notify.requesterId = requesterId;

                strncpy(notify.requesterName,name.toStdString().c_str(),32);

                target->write(makePacket(MSG_ADD_FRIEND_NOTIFY,QByteArray((char*)&notify,sizeof(AddFriendNotify)),0,targetId));
            }
            break;
        }
        case MSG_ADD_FRIEND_RESP:{
            AddFriendResp *resp = (AddFriendResp*)bodyData.data();

            // 发起请求的人，是否同意，当前用户即点了同意/拒绝的人
            int requesterId = resp->requesterId;
            bool accepted = resp->accepted;
            int responderId = this->m_uid;

            // 同意，写入数据库
            if(accepted){
                DBManager::instance().addFriend(requesterId,responderId);
            }

            // 更新数据库请求状态
            DBManager::instance().markRequestProcessed(requesterId, responderId,accepted);

            // 转发结果给请求者
            ClientSocket *requesterSocket = TcpServer::instance().getUserSocket(requesterId);
            if (requesterSocket) {
                requesterSocket->write(makePacket(MSG_ADD_FRIEND_RESULT, bodyData, 0, requesterId));
            }
            break;
        }
        case MSG_FILE_TRANSFER_REQ:{
            // 文件传输请求 - 直接转发给目标用户
            int targetId = header->dest_id;
            ClientSocket *targetSocket = TcpServer::instance().getUserSocket(targetId);

            if(targetSocket){
                this->writePacket(targetSocket,MSG_FILE_TRANSFER_REQ,bodyData,this->m_uid,targetId);
            }else{
                // 不在线
                LOG_INFO_FMT("Send file failed,target user %1 is offline",targetId);
            }
            break;
        }
        case MSG_FILE_TRANSFER_RESP: {
            // 文件传输响应 - 转发给发送者
            int targetId = header->dest_id;
            ClientSocket *targetSocket = TcpServer::instance().getUserSocket(targetId);

            if (targetSocket) {
                this->writePacket(targetSocket, MSG_FILE_TRANSFER_RESP,bodyData, this->m_uid, targetId);

                FileTransferResp *resp = (FileTransferResp*)bodyData.data();
                qDebug() << "[Server] Forwarded file transfer response, accepted:"
                         << (resp->accepted == 1);
            }
            break;
        }
        case MSG_FILE_CHUNK:{
            // 文件传输分片 - 转发给发送者
            int targetId = header->dest_id;
            ClientSocket *targetSocket = TcpServer::instance().getUserSocket(targetId);

            if(targetSocket){
                this->writePacket(targetSocket,MSG_FILE_CHUNK,bodyData,this->m_uid,targetId);

                // 每100个分片打印一次日志，避免日志刷屏
                FileChunk *chunk = (FileChunk*)bodyData.data();
                if (chunk->chunkIndex % 100 == 0) {
                    QString msg = QString("Forwarding file chunk %1 from user %2").arg(chunk->chunkIndex,targetId);
                    LOG_INFO(msg);
                }
            }else{
                // 目标用户不在线，记录日志
                LOG_INFO_FMT("Target user %1 is offline.Cannot forward file chunk.",targetId);
            }
            break;
        }
        case MSG_CREATE_GROUP_REQ: {
            // 创建群聊请求
            CreateGroupReq *req = (CreateGroupReq*)bodyData.data();
            QString groupName = QString::fromUtf8(req->groupName);

            int groupId = DBManager::instance().createGroup(groupName, this->m_uid);

            // 回包
            CreateGroupResp resp;
            resp.result = (groupId > 0) ? 1 : 0;
            resp.groupId = groupId;
            this->write(makePacket(MSG_CREATE_GROUP_RESP, QByteArray((char*)&resp, sizeof(CreateGroupResp))));
            break;
        }
        case MSG_GROUP_LIST_REQ: {
            // 获取用户所在群列表
            QList<GroupInfo> groupList = DBManager::instance().getGroupList(this->m_uid);

            int count = groupList.size();
            int bodyLen = sizeof(int) + count * sizeof(GroupInfo);

            QByteArray respBody;
            respBody.resize(bodyLen);
            char *ptr = respBody.data();

            memcpy(ptr, &count, sizeof(int));
            ptr += sizeof(int);

            for (const auto &info : groupList) {
                memcpy(ptr, &info, sizeof(GroupInfo));
                ptr += sizeof(GroupInfo);
            }

            this->write(makePacket(MSG_GROUP_LIST_RESP, respBody));
            break;
        }
        case MSG_GROUP_MEMBER_LIST_REQ: {
            // 获取群成员列表
            if (bodyData.size() < 4) break;
            int groupId = *(int*)bodyData.constData();

            QList<GroupMemberInfo> memberList = DBManager::instance().getGroupMembers(groupId);

            int count = memberList.size();
            int bodyLen = sizeof(int) + sizeof(int) + count * sizeof(GroupMemberInfo);

            QByteArray respBody;
            respBody.resize(bodyLen);
            char *ptr = respBody.data();

            memcpy(ptr, &groupId, sizeof(int));
            ptr += sizeof(int);
            memcpy(ptr, &count, sizeof(int));
            ptr += sizeof(int);

            for (const auto &info : memberList) {
                memcpy(ptr, &info, sizeof(GroupMemberInfo));
                ptr += sizeof(GroupMemberInfo);
            }

            this->write(makePacket(MSG_GROUP_MEMBER_LIST_RESP, respBody));
            break;
        }
        case MSG_INVITE_TO_GROUP_REQ: {
            // 邀请用户加入群(不用通过好友同意直接在群)
            InviteToGroupReq *req = (InviteToGroupReq*)bodyData.data();
            int groupId = req->groupId;
            int targetUserId = req->targetUserId;

            bool success = DBManager::instance().addGroupMember(groupId, targetUserId, 0);

            if (success) {
                // 通知被邀请的用户
                ClientSocket* targetSocket = TcpServer::instance().getUserSocket(targetUserId);
                if (targetSocket) {
                    InviteToGroupNotify notify;
                    notify.groupId = groupId;
                    notify.inviterId = this->m_uid;

                    // 获取群名
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
            }else{
                // TODO 离线群邀请通知
            }
            break;
        }
        case MSG_LEAVE_GROUP_REQ: {
            // 退出群聊
            LeaveGroupReq *req = (LeaveGroupReq*)bodyData.data();
            DBManager::instance().removeGroupMember(req->groupId, this->m_uid);
            break;
        }
        case MSG_GROUP_CHAT_TEXT: {
            // 群聊消息
            if (bodyData.size() < (int)sizeof(GroupChatMessage)) break;

            GroupChatMessage *msgHeader = (GroupChatMessage*)bodyData.data();
            int groupId = msgHeader->groupId;

            // 获取消息内容（去掉GroupChatMessage头部）
            QByteArray msgContent = bodyData.mid(sizeof(GroupChatMessage));

            // 保存到群聊消息表
            DBManager::instance().saveGroupMessage(groupId, this->m_uid, msgContent);

            // 获取群成员列表
            QList<int> memberIds = DBManager::instance().getGroupMemberIds(groupId);

            // 构建转发消息体（包含发送者信息）
            GroupChatMessage forwardHeader;
            forwardHeader.groupId = groupId;
            forwardHeader.senderId = this->m_uid;
            strncpy(forwardHeader.senderName, this->m_userName.toUtf8().constData(), 31);
            forwardHeader.senderName[31] = '\0';

            QByteArray forwardBody;
            forwardBody.append((char*)&forwardHeader, sizeof(GroupChatMessage));
            forwardBody.append(msgContent);

            // 遍历群成员转发消息
            for (int memberId : memberIds) {
                if (memberId == this->m_uid) continue; // 不发给自己

                ClientSocket* memberSocket = TcpServer::instance().getUserSocket(memberId);
                if (memberSocket) {
                    // 在线：直接转发
                    memberSocket->write(makePacket(MSG_GROUP_CHAT_TEXT, forwardBody, this->m_uid, memberId));
                } else {
                    // 离线：保存离线消息
                    DBManager::instance().saveGroupOfflineMessage(groupId, this->m_uid, memberId, msgContent);
                }
            }
            break;
        }
        case MSG_GROUP_CHAT_HISTORY_REQ: {
            // 群聊历史消息请求
            if (bodyData.size() < 4) break;
            int groupId = *(int*)bodyData.constData();

            auto history = DBManager::instance().getGroupChatHistory(groupId, 200);

            QByteArray resp;
            QDataStream out(&resp, QIODevice::WriteOnly);
            out.setByteOrder(QDataStream::LittleEndian);

            out << quint32(groupId);
            out << quint32(history.size());

            for (const auto &item : history) {
                int senderId = std::get<0>(item);
                QString senderName = std::get<1>(item);
                QByteArray content = std::get<2>(item);

                out << quint32(senderId);
                QByteArray nameBytes = senderName.toUtf8();
                out << quint32(nameBytes.size());
                out.writeRawData(nameBytes.constData(), nameBytes.size());
                out << quint32(content.size());
                out.writeRawData(content.constData(), content.size());
            }

            this->write(makePacket(MSG_GROUP_CHAT_HISTORY_RESP, resp, groupId, this->m_uid));
            break;
        }
        default:
            break;
        }

        // 从缓冲区移除已处理的这个包
        m_buffer = m_buffer.right(m_buffer.size() - totalLen);
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
    // 1. 查库：我有那些好友？
    QList<int> friendIds = DBManager::instance().getFriendIds(this->m_uid);

    // 2. 遍历：看这些好友谁在线
    for (int friendId : friendIds) {
        ClientSocket* friendSocket = TcpServer::instance().getUserSocket(friendId);

        // 3. 如果好友在线，就发包通知他
        if (friendSocket) {
            FriendStatusChange notify;
            notify.uid = this->m_uid; // 我变了
            notify.status = status;   // 变成啥了

            // 注意：这里 src_id 填 0 或 server_id 都可以，因为关键数据在 struct 里
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

        // 构建群聊消息体
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

