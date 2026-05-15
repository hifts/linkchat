#include "networkmanager.h"
#include "logger.h"

#include <QDataStream>
#include <cstring>
#include <exception>

namespace {
    inline size_t safe_strnlen(const char* s, size_t maxlen) {
        if (!s) return 0;
        const char* end = (const char*)std::memchr(s, '\0', maxlen);
        return end ? (size_t)(end - s) : maxlen;
    }
}

NetworkManager &NetworkManager::instance()
{
    static NetworkManager instance;
    return instance;
}

NetworkManager::NetworkManager(QObject *parent)
    : QObject{parent}
{
    m_socket = new QTcpSocket(this);

    m_heartbeatManager = new HeartbeatManager(this);
    m_reconnectManager = new ReconnectManager(this);

    connect(m_socket,&QTcpSocket::connected,this,&NetworkManager::onConnected);
    connect(m_socket,&QTcpSocket::disconnected,this,&NetworkManager::onDisconnected);
    connect(m_socket, &QTcpSocket::errorOccurred, this,&NetworkManager::onError);

    connect(m_socket,&QTcpSocket::readyRead,this,&NetworkManager::onReadyRead);

    connect(m_heartbeatManager,&HeartbeatManager::needSendHeartbeat,this,&NetworkManager::sendHeartbeat);
    connect(m_heartbeatManager,&HeartbeatManager::heartbeatTimeout,
            this,[this](int missedCount){
                Q_UNUSED(missedCount);
                m_socket->abort();
            });

    connect(m_reconnectManager,&ReconnectManager::needReconnect,
            this,[this](const QString &ip,uint16_t port){
                m_socket->abort();
                m_socket->connectToHost(ip,port);
            });

    connect(m_reconnectManager,&ReconnectManager::needAutoLogin,this,&NetworkManager::handleAutoLogin);
}

void NetworkManager::connectToServer(const QString &ip, uint16_t port)
{
    m_reconnectManager->setServerInfo(ip,port);

    m_reconnectManager->setConnectionState(ReconnectManager::Connecting);

    m_socket->abort();
    m_socket->connectToHost(ip,port);
}

void NetworkManager::disconnectFromServer()
{
    m_heartbeatManager->stop();

    m_reconnectManager->setAutoConnect(false);
    m_reconnectManager->stopReconnect();

    m_reconnectManager->clearLoginInfo();

    EncryptionManager::instance().clearKeyCache();

    m_socket->disconnectFromHost();
}

bool NetworkManager::isConnected() const
{
    return m_socket->state() == QAbstractSocket::ConnectedState;
}

void NetworkManager::sendMsg(uint32_t type, const QByteArray &body)
{
    QByteArray data = makePacket(type,body);
    if(m_socket->state() == QAbstractSocket::ConnectedState){
        m_socket->write(data);
    }else{
        LOG_WARN("Socket is not connected, cannot send message");
    }
}

void NetworkManager::sendRow(const QByteArray &packet)
{
    if(packet.isEmpty()){
        return;
    }

    if(m_socket->state() == QAbstractSocket::ConnectedState){
        m_socket->write(packet);
    }else{
        LOG_WARN("Socket is not connected, cannot send packet");
    }
}

void NetworkManager::requestResumeTransfer(const QString &fileId, int friendId)
{
    FileResumeReq req;
    memset(&req, 0, sizeof(req));
    strncpy(req.fileId, fileId.toLatin1().constData(), 63);

    QByteArray body((char*)&req, sizeof(req));
    QByteArray packet = makePacket(MSG_FILE_RESUME_REQ, body, 0, friendId);
    sendRow(packet);
}

void NetworkManager::requestFileVerify(const QString &fileId, const QString &fileMD5, int friendId)
{
    FileVerifyReq req;
    memset(&req, 0, sizeof(req));
    strncpy(req.fileId, fileId.toLatin1().constData(), 63);
    strncpy(req.fileMD5, fileMD5.toUtf8().constData(), 32);

    QByteArray body((char*)&req, sizeof(req));
    QByteArray packet = makePacket(MSG_FILE_VERIFY_REQ, body, 0, friendId);
    sendRow(packet);
}

void NetworkManager::sendHeartbeat()
{
    if (!isConnected()) {
        return;
    }

    HeartbeatPacket hb;
    hb.timestamp = QDateTime::currentMSecsSinceEpoch();

    sendMsg(MSG_HEARTBEAT_REQ,QByteArray((char*)&hb,sizeof(HeartbeatPacket)));
}

void NetworkManager::handleAutoLogin(const QString &userName, const QString &passwordHashBase64)
{
    LoginReq req;
    memset(&req,0,sizeof(LoginReq));
    strncpy(req.userName,userName.toUtf8().constData(),31);
    strncpy(req.passwordHash,passwordHashBase64.toUtf8().constData(),63);
    sendMsg(MSG_LOGIN_REQ,QByteArray((char*)&req,sizeof(LoginReq)));

}

void NetworkManager::onConnected()
{
    m_buffer.clear();

    m_reconnectManager->setConnectionState(ReconnectManager::Connected);

    m_heartbeatManager->start();

    emit sigConnectionStateChanged(true);
}

void NetworkManager::onDisconnected()
{
    m_heartbeatManager->stop();

    m_reconnectManager->setConnectionState(ReconnectManager::Disconnected);

    emit sigConnectionStateChanged(false);
}

void NetworkManager::onError(QAbstractSocket::SocketError error)
{
    Q_UNUSED(error);
    LOG_ERROR(QString("NetworkManager socket error: %1").arg(m_socket->errorString()));
    m_reconnectManager->setConnectionState(ReconnectManager::Disconnected);
    
}

void NetworkManager::onReadyRead()
{
    m_buffer.append(m_socket->readAll());

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
                LOG_WARN(QString("Packet resync: skipping %1 bytes to next magic").arg(nextIdx));
                m_buffer.remove(0, nextIdx);
            } else {
                LOG_WARN("Packet resync: magic not found, dropping buffer except last 3 bytes");
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
            LOG_ERROR_FMT("Invalid packet length: %1, skipping magic and resyncing", totalLen);
            m_buffer.remove(0, sizeof(uint32_t));
            continue;
        }

        if (m_buffer.size() < (int)totalLen) break;

        QByteArray body    = m_buffer.mid(sizeof(PDUHeader), totalLen - sizeof(PDUHeader));
        uint32_t msgType   = header.msg_type;
        uint32_t srcId     = header.src_id;

        if (msgType != MSG_FILE_CHUNK) {
            LOG_DEBUG(QString("Received packet: type=%1, len=%2").arg(msgType).arg(totalLen));
        }

        m_buffer.remove(0, totalLen);

        try {
            switch (msgType) {
            case MSG_HEARTBEAT_RESP:{
                m_heartbeatManager->onHeartbeatReceived();
                break;
            }
            case MSG_REGISTER_RESP:{
                if (body.size() < (int)sizeof(LoginResp)) break;
                LoginResp *resp = (LoginResp*)body.data();
                emit sigRegisterResult(resp->result == 1);
                break;
            }
            case MSG_LOGIN_SALT_RESP:{
                if (body.size() < (int)sizeof(LoginSaltResp)) break;
                LoginSaltResp *resp = (LoginSaltResp*)body.data();
                const bool ok = (resp->result == 1);
                QByteArray saltBase64;
                if (ok) {
                    saltBase64 = QByteArray(resp->salt, (int)safe_strnlen(resp->salt, sizeof(resp->salt)));
                }
                emit sigLoginSaltReceived(ok, saltBase64);
                break;
            }
            case MSG_LOGIN_RESP:{
                if (body.size() < (int)sizeof(LoginResp)) break;
                LoginResp *resp = (LoginResp*)body.data();
                int result = resp->result;
                bool ok = (result == 1);
                int uid = resp->userId;

                int errorCode = 0;
                if(result == 2){
                    errorCode = 2;
                }else if(result == 0){
                    errorCode = 1;
                }

                emit sigLoginResult(ok,uid,errorCode);
                break;
            }
            case MSG_FRIEND_LIST_RESP:{
                if (body.size() < (int)sizeof(int)) break;
                char *ptr = body.data();
                int count = 0;
                memcpy(&count,ptr,sizeof(int));
                ptr += sizeof(int);

                if (body.size() < (int)(sizeof(int) + count * sizeof(FriendInfo))) break;

                QList<FriendInfo> list;
                for (int i = 0; i < count; ++i) {
                    FriendInfo info;
                    memcpy(&info,ptr,sizeof(FriendInfo));
                    list.append(info);
                    ptr += sizeof(FriendInfo);
                }
                emit sigFriendListReceived(list);
                break;
            }
            case MSG_CHAT_TEXT:{
                if (body.isEmpty() || body.size() < 1) {
                    LOG_WARN("Received empty chat message");
                    break;
                }
                
                char subType = body[0];
                QByteArray content = body.mid(1);
                
                if (isSubTypeEncrypted(subType)) {
                    char originalSubType = getOriginalSubType(subType);
                    
                    QByteArray key = EncryptionManager::instance().getCachedChatKey(m_currentUserId, srcId);
                    
                    if (key.isEmpty()) {
                        LOG_ERROR_FMT("Failed to get chat key for user %1", srcId);
                        QByteArray failedBody;
                        failedBody.append(originalSubType);
                        emit sigMsgReceived(srcId, failedBody);
                        break;
                    }
                    
                    QByteArray decrypted = EncryptionManager::instance().xorEncryptDecrypt(content, key);
                    
                    if (decrypted.isEmpty() && !content.isEmpty()) {
                        LOG_ERROR_FMT("Failed to decrypt message from user %1", srcId);
                        QByteArray failedBody;
                        failedBody.append(originalSubType);
                        emit sigMsgReceived(srcId, failedBody);
                        break;
                    }
                    
                    QByteArray decryptedBody;
                    decryptedBody.append(originalSubType);
                    decryptedBody.append(decrypted);
                    
                    emit sigMsgReceived(srcId, decryptedBody);
                } else {
                    emit sigMsgReceived(srcId, body);
                }
                break;
            }
            case MSG_CHAT_HISTORY_RESP:{
                QDataStream in(body);
                in.setByteOrder(QDataStream::LittleEndian);

                quint32 count;
                in >> count;

                QList<std::tuple<int, QByteArray, quint64>> history;
                for (quint32 i = 0; i < count; ++i) {
                    quint32 senderId;
                    quint64 timestamp;
                    quint32 len;
                    if (in.atEnd()) break;
                    in >> senderId >> timestamp >> len;
                    if (body.size() < (int)(in.device()->pos() + len)) break;
                    QByteArray content = body.mid(in.device()->pos(), len);
                    in.device()->seek(in.device()->pos() + len);
                    
                    QByteArray decryptedContent;
                    if (!content.isEmpty() && content.size() >= 1) {
                        char subType = content[0];
                        QByteArray messageBody = content.mid(1);
                        
                        if (isSubTypeEncrypted(subType)) {
                            char originalSubType = getOriginalSubType(subType);
                            
                            QByteArray key = EncryptionManager::instance().getCachedChatKey(m_currentUserId, srcId);
                            
                            if (key.isEmpty()) {
                                LOG_ERROR_FMT("Failed to get chat key for history with user %1", srcId);
                                decryptedContent.append(originalSubType);
                            } else {
                                QByteArray decrypted = EncryptionManager::instance().xorEncryptDecrypt(messageBody, key);
                                
                                if (decrypted.isEmpty() && !messageBody.isEmpty()) {
                                    LOG_ERROR_FMT("Failed to decrypt chat history message from user %1", senderId);
                                    decryptedContent.append(originalSubType);
                                } else {
                                    decryptedContent.append(originalSubType);
                                    decryptedContent.append(decrypted);
                                }
                            }
                        } else {
                            decryptedContent = content;
                        }
                    } else {
                        decryptedContent = content;
                    }
                
                history.append(std::make_tuple((int)senderId, decryptedContent, timestamp));
            }
            emit sigChatHistoryReceived(srcId, history);
            break;
        }
        case MSG_FRIEND_STATUS_NOTIFY:{
            if (body.size() < (int)sizeof(FriendStatusChange)) break;
            FriendStatusChange* notify = (FriendStatusChange*)body.data();
            emit sigFriendStatusChanged(notify->uid, notify->status);
            break;
        }
        case MSG_SEARCH_USER_RESP:{
            if (body.size() < (int)sizeof(int)) break;
            char *ptr = body.data();
            int count = 0;

            memcpy(&count,ptr,sizeof(int));
            ptr += sizeof(int);

            if (body.size() < (int)(sizeof(int) + count * sizeof(FriendInfo))) break;

            QList<FriendInfo> list;
            for (int i = 0; i < count; ++i) {
                FriendInfo info;
                memcpy(&info,ptr,sizeof(FriendInfo));
                list.append(info);
                ptr += sizeof(FriendInfo);
            }

            emit sigSearchUserResult(list);
            break;
        }
        case MSG_ADD_FRIEND_NOTIFY:{
            if (body.size() < (int)sizeof(AddFriendNotify)) break;
            AddFriendNotify *notify = (AddFriendNotify*)body.data();
            LOG_DEBUG(QString("Received friend request from %1 (%2)")
                     .arg(notify->requesterId)
                     .arg(QString::fromUtf8(notify->requesterName, safe_strnlen(notify->requesterName, 32))));
            emit sigFriendRequestReceived(notify->requesterId,QString::fromUtf8(notify->requesterName, safe_strnlen(notify->requesterName, 32)));
            break;
        }
        case MSG_ADD_FRIEND_RESULT:{
            if (body.size() < (int)sizeof(AddFriendResp)) break;
            AddFriendResp *resp = (AddFriendResp*)body.data();
            bool accepted = resp->accepted;

            if(accepted){
                emit sigFriendRequestAccepted();
            }else{
                emit sigFriendRequestRejected();
            }
            break;
        }
        case MSG_DELETE_FRIEND_RESP:{
            if (body.size() < (int)sizeof(DeleteFriendResp)) break;
            DeleteFriendResp *resp = (DeleteFriendResp*)body.data();
            emit sigDeleteFriendResponse(resp->result, resp->targetId);
            break;
        }
        case MSG_FILE_TRANSFER_REQ:{
            if (body.size() < (int)sizeof(FileTransferReq)) break;
            
            FileTransferReq req;
            memcpy(&req, body.data(), sizeof(FileTransferReq));
            
            emit sigFileTransferRequest(
                QString::fromLatin1(req.fileId, safe_strnlen(req.fileId, sizeof(req.fileId))),
                QString::fromUtf8(req.fileName, safe_strnlen(req.fileName, sizeof(req.fileName))),
                req.fileSize,
                srcId
                );
            break;
        }
        case MSG_FILE_TRANSFER_RESP:{
            if(body.size() < (int)sizeof(FileTransferResp)){
                break;
            }

            FileTransferResp resp;
            memcpy(&resp, body.data(), sizeof(FileTransferResp));
            
            QString fileId = QString::fromLatin1(resp.fileId, safe_strnlen(resp.fileId, sizeof(resp.fileId)));
            bool accepted = (resp.accepted == 1);

            emit sigFileTransferResponse(fileId,accepted);
            break;
        }
        case MSG_FILE_CHUNK:{
            if (body.size() < (int)sizeof(FileChunk)) {
                LOG_WARN(QString("Received file chunk with insufficient size: body=%1, expected=%2")
                            .arg(body.size()).arg((int)sizeof(FileChunk)));
                break;
            }
            
            FileChunk chunk;
            memset(&chunk, 0, sizeof(FileChunk));
            memcpy(&chunk, body.data(), sizeof(FileChunk));
            
            QString fileId = QString::fromLatin1(chunk.fileId, safe_strnlen(chunk.fileId, sizeof(chunk.fileId)));
            int chunkIndex = static_cast<int>(chunk.chunkIndex);
            int chunkSize = static_cast<int>(chunk.chunkSize);
            
            int declaredChunkSize = (int)chunk.chunkSize;
            int actualDataSize = body.size() - (int)sizeof(FileChunk);

            if (chunkIndex % 50 == 0) {
                LOG_DEBUG(QString("Processing chunk for file %1, index %2, declared size %3, actual data %4")
                         .arg(fileId).arg(chunkIndex).arg(declaredChunkSize).arg(actualDataSize));
            }

            if (actualDataSize < 0) {
                LOG_ERROR(QString("Body size %1 smaller than FileChunk header %2")
                         .arg(body.size()).arg((int)sizeof(FileChunk)));
                break;
            }

            if (declaredChunkSize < 0 || declaredChunkSize > 1024 * 1024) {
                LOG_ERROR(QString("Invalid declared chunk size: %1").arg(declaredChunkSize));
                break;
            }

            if (chunkIndex < 0 || chunkIndex > 1000000) {
                LOG_ERROR(QString("Invalid chunk index: %1").arg(chunkIndex));
                break;
            }

            int readSize = qMin(declaredChunkSize, actualDataSize);
            if (readSize <= 0 && declaredChunkSize > 0) {
                LOG_ERROR(QString("No data available for chunk %1").arg(chunkIndex));
                break;
            }

            QByteArray chunkData;
            try {
                chunkData = body.mid(sizeof(FileChunk), readSize);
            } catch (const std::exception& e) {
                LOG_ERROR(QString("Exception extracting chunk data: %1").arg(e.what()));
                break;
            } catch (...) {
                LOG_ERROR("Unknown exception extracting chunk data");
                break;
            }

            if (chunkData.isEmpty() && readSize > 0) {
                LOG_ERROR(QString("Failed to extract chunk data for chunk %1").arg(chunkIndex));
                break;
            }

            if (chunkIndex % 50 == 0) {
                LOG_DEBUG(QString("Chunk %1 extracted: %2 bytes")
                         .arg(chunkIndex).arg(chunkData.size()));
            }

            try {
                emit receiveChunk(fileId, chunkIndex, chunkData, srcId);
            } catch (const std::exception& e) {
                LOG_ERROR(QString("Exception emitting receiveChunk: %1").arg(e.what()));
            } catch (...) {
                LOG_ERROR("Unknown exception emitting receiveChunk");
            }
            break;
        }
        case MSG_FILE_TRANSFER_ACK:{
            if (body.size() < (int)sizeof(FileTransferAck)) {
                break;
            }

            FileTransferAck ack;
            memset(&ack, 0, sizeof(FileTransferAck));
            memcpy(&ack, body.data(), sizeof(FileTransferAck));

            QString fileId = QString::fromLatin1(ack.fileId, safe_strnlen(ack.fileId, sizeof(ack.fileId)));
            emit sigFileTransferAck(fileId, static_cast<int>(ack.chunkIndex), srcId);
            break;
        }
        case MSG_CREATE_GROUP_RESP: {
            if (body.size() < (int)sizeof(CreateGroupResp)) break;
            CreateGroupResp *resp = (CreateGroupResp*)body.data();
            emit sigCreateGroupResult(resp->result == 1, resp->groupId);
            break;
        }
        case MSG_GROUP_LIST_RESP: {
            if (body.size() < (int)sizeof(int)) break;
            char *ptr = body.data();
            int count = 0;
            memcpy(&count, ptr, sizeof(int));
            ptr += sizeof(int);

            if (body.size() < (int)(sizeof(int) + count * sizeof(GroupInfo))) break;

            QList<GroupInfo> list;
            for (int i = 0; i < count; ++i) {
                GroupInfo info;
                memcpy(&info, ptr, sizeof(GroupInfo));
                list.append(info);
                ptr += sizeof(GroupInfo);
            }
            emit sigGroupListReceived(list);
            break;
        }
        case MSG_GROUP_MEMBER_LIST_RESP: {
            if (body.size() < (int)sizeof(int) * 2) break;
            char *ptr = body.data();
            int groupId = 0;
            int count = 0;
            memcpy(&groupId, ptr, sizeof(int));
            ptr += sizeof(int);
            memcpy(&count, ptr, sizeof(int));
            ptr += sizeof(int);

            if (body.size() < (int)(sizeof(int) * 2 + count * sizeof(GroupMemberInfo))) break;

            QList<GroupMemberInfo> list;
            for (int i = 0; i < count; ++i) {
                GroupMemberInfo info;
                memcpy(&info, ptr, sizeof(GroupMemberInfo));
                list.append(info);
                ptr += sizeof(GroupMemberInfo);
            }
            emit sigGroupMemberListReceived(groupId, list);
            break;
        }
        case MSG_GROUP_CHAT_TEXT: {
            if (body.size() < (int)sizeof(GroupChatMessage)) break;

            GroupChatMessage *msg = (GroupChatMessage*)body.data();
            int groupId = msg->groupId;
            int senderId = msg->senderId;
            QString senderName = QString::fromUtf8(msg->senderName, safe_strnlen(msg->senderName, 32));
            QByteArray content = body.mid(sizeof(GroupChatMessage));
            
            if (content.isEmpty() || content.size() < 1) {
                LOG_WARN("Received empty group chat message");
                break;
            }
            
            char subType = content[0];
            QByteArray messageBody = content.mid(1);
            
            if (isSubTypeEncrypted(subType)) {
                char originalSubType = getOriginalSubType(subType);
                
                QByteArray key = EncryptionManager::instance().getCachedGroupKey(groupId);
                
                if (key.isEmpty()) {
                    LOG_ERROR_FMT("Failed to get group key for group %1", groupId);
                    QByteArray failedBody;
                    failedBody.append(originalSubType);
                    emit sigGroupMsgReceived(groupId, senderId, senderName, failedBody);
                    break;
                }
                
                QByteArray decrypted = EncryptionManager::instance().xorEncryptDecrypt(messageBody, key);
                
                if (decrypted.isEmpty() && !messageBody.isEmpty()) {
                    LOG_ERROR_FMT("Failed to decrypt group message from group %1", groupId);
                    QByteArray failedBody;
                    failedBody.append(originalSubType);
                    emit sigGroupMsgReceived(groupId, senderId, senderName, failedBody);
                    break;
                }
                
                QByteArray decryptedContent;
                decryptedContent.append(originalSubType);
                decryptedContent.append(decrypted);
                
                emit sigGroupMsgReceived(groupId, senderId, senderName, decryptedContent);
            } else {
                emit sigGroupMsgReceived(groupId, senderId, senderName, content);
            }
            break;
        }
        case MSG_GROUP_CHAT_HISTORY_RESP: {
            if (body.size() < (int)sizeof(quint32) * 2) break;
            QDataStream in(body);
            in.setByteOrder(QDataStream::LittleEndian);

            quint32 groupId, count;
            in >> groupId >> count;

            QList<std::tuple<int, QString, QByteArray, quint64>> history;
            for (quint32 i = 0; i < count; ++i) {
                if (in.atEnd()) break;
                quint32 senderId, nameLen, contentLen;
                quint64 timestamp;
                in >> senderId >> timestamp >> nameLen;

                if (body.size() < (int)(in.device()->pos() + nameLen)) break;
                QByteArray nameBytes(nameLen, '\0');
                in.readRawData(nameBytes.data(), nameLen);
                QString senderName = QString::fromUtf8(nameBytes);

                if (in.atEnd()) break;
                in >> contentLen;
                if (body.size() < (int)(in.device()->pos() + contentLen)) break;
                QByteArray content(contentLen, '\0');
                in.readRawData(content.data(), contentLen);
                
                QByteArray decryptedContent;
                if (!content.isEmpty() && content.size() >= 1) {
                    char subType = content[0];
                    QByteArray messageBody = content.mid(1);
                    
                    if (isSubTypeEncrypted(subType)) {
                        char originalSubType = getOriginalSubType(subType);
                        
                        QByteArray key = EncryptionManager::instance().getCachedGroupKey(groupId);
                        
                        if (key.isEmpty()) {
                            LOG_ERROR_FMT("Failed to get group key for history from group %1", groupId);
                            decryptedContent.append(originalSubType);
                        } else {
                            QByteArray decrypted = EncryptionManager::instance().xorEncryptDecrypt(messageBody, key);
                            
                            if (decrypted.isEmpty() && !messageBody.isEmpty()) {
                                LOG_ERROR_FMT("Failed to decrypt group history message from group %1", groupId);
                                decryptedContent.append(originalSubType);
                            } else {
                                decryptedContent.append(originalSubType);
                                decryptedContent.append(decrypted);
                            }
                        }
                    } else {
                        decryptedContent = content;
                    }
                } else {
                    decryptedContent = content;
                }

                history.append(std::make_tuple((int)senderId, senderName, decryptedContent, timestamp));
            }
            emit sigGroupChatHistoryReceived(groupId, history);
            break;
        }
        case MSG_LEAVE_GROUP_RESP: {
            if (body.size() < (int)sizeof(LeaveGroupResp)) break;
            LeaveGroupResp *resp = (LeaveGroupResp*)body.data();
            emit sigLeaveGroupResponse(resp->result, resp->groupId);
            break;
        }
        case MSG_INVITE_TO_GROUP_NOTIFY: {
            if (body.size() < (int)sizeof(InviteToGroupNotify)) break;
            InviteToGroupNotify *notify = (InviteToGroupNotify*)body.data();
            emit sigInviteToGroupNotify(
                notify->groupId,
                QString::fromUtf8(notify->groupName, safe_strnlen(notify->groupName, 64)),
                notify->inviterId,
                QString::fromUtf8(notify->inviterName, safe_strnlen(notify->inviterName, 32))
                );
            break;
        }
        case MSG_FILE_RESUME_REQ: {
            if(body.size() < (int)sizeof(FileResumeReq)){
                break;
            }
            FileResumeReq *req = (FileResumeReq*)body.data();
            QString fileId = QString::fromLatin1(req->fileId, safe_strnlen(req->fileId, sizeof(req->fileId)));
            emit sigFileResumeReq(fileId, srcId);
            break;
        }
        case MSG_FILE_RESUME_RESP: {
            if(body.size() < (int)sizeof(FileResumeResp)){
                break;
            }
            FileResumeResp *resp = (FileResumeResp*)body.data();
            QString fileId = QString::fromLatin1(resp->fileId, safe_strnlen(resp->fileId, sizeof(resp->fileId)));
            bool canResume = (resp->canResume == 1);
            int totalChunks = resp->totalChunks;
            int receivedChunks = resp->receivedChunks;

            QByteArray bitmap;
            if(canResume && totalChunks > 0){
                int bitmapSize = (totalChunks + 7) / 8;
                if (body.size() >= (int)(sizeof(FileResumeResp) + bitmapSize)) {
                    bitmap = body.mid(sizeof(FileResumeResp), bitmapSize);
                }
            }

            emit sigFileResumeResp(fileId, canResume, totalChunks, receivedChunks, bitmap);
            break;
        }
        case MSG_FILE_VERIFY_REQ: {
            if(body.size() < (int)sizeof(FileVerifyReq)){
                break;
            }
            FileVerifyReq *req = (FileVerifyReq*)body.data();
            QString fileId = QString::fromLatin1(req->fileId, safe_strnlen(req->fileId, sizeof(req->fileId)));
            QString fileMD5 = QString::fromLatin1(req->fileMD5, safe_strnlen(req->fileMD5, sizeof(req->fileMD5)));
            break;
        }
        case MSG_FILE_VERIFY_RESP: {
            if(body.size() < (int)sizeof(FileVerifyResp)){
                break;
            }
            FileVerifyResp *resp = (FileVerifyResp*)body.data();
            QString fileId = QString::fromLatin1(resp->fileId, safe_strnlen(resp->fileId, sizeof(resp->fileId)));
            bool verified = (resp->verified == 1);
            emit sigFileVerifyResp(fileId, verified);
            break;
        }
        default:
            break;
        }
        } catch (const std::exception& e) {
            LOG_ERROR_FMT("Exception in packet processing loop: %1", e.what());
        } catch (...) {
            LOG_ERROR("Unknown exception in packet processing loop");
        }
    }
}

