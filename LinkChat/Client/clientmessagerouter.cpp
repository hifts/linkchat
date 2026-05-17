#include "clientmessagerouter.h"

#include "networkmanager.h"
#include "logger.h"

#include <QDataStream>
#include <cstring>
#include <exception>

namespace {
inline size_t safe_strnlen(const char* s, size_t maxlen)
{
    if (!s) return 0;
    const char* end = static_cast<const char*>(std::memchr(s, '\0', maxlen));
    return end ? static_cast<size_t>(end - s) : maxlen;
}
}

ClientMessageRouter::ClientMessageRouter(NetworkManager* manager)
    : m_manager(manager)
{
}

void ClientMessageRouter::dispatch(uint32_t msgType, uint32_t srcId, const QByteArray& body)
{
    try {
        switch (msgType) {
        case MSG_HEARTBEAT_RESP:{
            m_manager->m_heartbeatManager->onHeartbeatReceived();
            break;
        }
        case MSG_REGISTER_RESP:{
            if (body.size() < (int)sizeof(LoginResp)) break;
            LoginResp *resp = (LoginResp*)body.data();
            emit m_manager->sigRegisterResult(resp->result == 1);
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
            emit m_manager->sigLoginSaltReceived(ok, saltBase64);
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

            emit m_manager->sigLoginResult(ok,uid,errorCode);
            break;
        }
        case MSG_FRIEND_LIST_RESP:{
            if (body.size() < (int)sizeof(int)) break;
            const char *ptr = body.constData();
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
            emit m_manager->sigFriendListReceived(list);
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

                QByteArray key = EncryptionManager::instance().getCachedChatKey(m_manager->m_currentUserId, srcId);

                if (key.isEmpty()) {
                    LOG_ERROR_FMT("Failed to get chat key for user %1", srcId);
                    QByteArray failedBody;
                    failedBody.append(originalSubType);
                    emit m_manager->sigMsgReceived(srcId, failedBody);
                    break;
                }

                QByteArray decrypted = EncryptionManager::instance().xorEncryptDecrypt(content, key);

                if (decrypted.isEmpty() && !content.isEmpty()) {
                    LOG_ERROR_FMT("Failed to decrypt message from user %1", srcId);
                    QByteArray failedBody;
                    failedBody.append(originalSubType);
                    emit m_manager->sigMsgReceived(srcId, failedBody);
                    break;
                }

                QByteArray decryptedBody;
                decryptedBody.append(originalSubType);
                decryptedBody.append(decrypted);

                emit m_manager->sigMsgReceived(srcId, decryptedBody);
            } else {
                emit m_manager->sigMsgReceived(srcId, body);
            }
            break;
        }
        case MSG_OFFLINE_CHAT_TEXT:{
            if (body.size() < (int)sizeof(quint64) + 1) {
                LOG_WARN("Received invalid offline chat message");
                break;
            }

            quint64 offlineMsgId = 0;
            memcpy(&offlineMsgId, body.constData(), sizeof(offlineMsgId));
            const QByteArray chatBody = body.mid(sizeof(offlineMsgId));

            dispatch(MSG_CHAT_TEXT, srcId, chatBody);

            OfflineMsgAck ack;
            ack.offlineMsgId = offlineMsgId;
            QByteArray packet = makePacket(MSG_OFFLINE_MSG_ACK, QByteArray((char*)&ack, sizeof(ack)));
            m_manager->sendRow(packet);
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

                        QByteArray key = EncryptionManager::instance().getCachedChatKey(m_manager->m_currentUserId, srcId);

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
            emit m_manager->sigChatHistoryReceived(srcId, history);
            break;
        }
        case MSG_FRIEND_STATUS_NOTIFY:{
            if (body.size() < (int)sizeof(FriendStatusChange)) break;
            FriendStatusChange* notify = (FriendStatusChange*)body.data();
            emit m_manager->sigFriendStatusChanged(notify->uid, notify->status);
            break;
        }
        case MSG_SEARCH_USER_RESP:{
            if (body.size() >= (int)sizeof(SearchUserBatchHeader)) {
                SearchUserBatchHeader header;
                memcpy(&header, body.constData(), sizeof(header));
                const qsizetype expectedSize = sizeof(SearchUserBatchHeader) + header.count * sizeof(FriendInfo);
                if (header.count <= 1000 && body.size() >= expectedSize) {
                    QList<FriendInfo> list;
                    const char *ptr = body.constData() + sizeof(SearchUserBatchHeader);
                    for (quint32 i = 0; i < header.count; ++i) {
                        FriendInfo info;
                        memcpy(&info, ptr, sizeof(FriendInfo));
                        list.append(info);
                        ptr += sizeof(FriendInfo);
                    }
                    emit m_manager->sigSearchUserResult(list, header.requestId, header.offset == 0, header.hasMore != 0);
                    break;
                }
            }

            if (body.size() < (int)sizeof(int)) break;
            const char *ptr = body.constData();
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

            emit m_manager->sigSearchUserResult(list, 0, true, false);
            break;
        }
        case MSG_ADD_FRIEND_NOTIFY:{
            if (body.size() < (int)sizeof(AddFriendNotify)) break;
            AddFriendNotify *notify = (AddFriendNotify*)body.data();
            LOG_DEBUG(QString("Received friend request from %1 (%2)")
                          .arg(notify->requesterId)
                          .arg(QString::fromUtf8(notify->requesterName, safe_strnlen(notify->requesterName, 32))));
            emit m_manager->sigFriendRequestReceived(notify->requesterId,QString::fromUtf8(notify->requesterName, safe_strnlen(notify->requesterName, 32)));
            break;
        }
        case MSG_ADD_FRIEND_RESULT:{
            if (body.size() < (int)sizeof(AddFriendResp)) break;
            AddFriendResp *resp = (AddFriendResp*)body.data();
            bool accepted = resp->accepted;

            if(accepted){
                emit m_manager->sigFriendRequestAccepted();
            }else{
                emit m_manager->sigFriendRequestRejected();
            }
            break;
        }
        case MSG_DELETE_FRIEND_RESP:{
            if (body.size() < (int)sizeof(DeleteFriendResp)) break;
            DeleteFriendResp *resp = (DeleteFriendResp*)body.data();
            emit m_manager->sigDeleteFriendResponse(resp->result, resp->targetId);
            break;
        }
        case MSG_FILE_TRANSFER_REQ:{
            if (body.size() < (int)sizeof(FileTransferReq)) break;
            
            FileTransferReq req;
            memcpy(&req, body.data(), sizeof(FileTransferReq));
            
            emit m_manager->sigFileTransferRequest(
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

            emit m_manager->sigFileTransferResponse(fileId,accepted);
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
                emit m_manager->receiveChunk(fileId, chunkIndex, chunkData, srcId);
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
            emit m_manager->sigFileTransferAck(fileId, static_cast<int>(ack.chunkIndex), srcId);
            break;
        }
        case MSG_CREATE_GROUP_RESP: {
            if (body.size() < (int)sizeof(CreateGroupResp)) break;
            CreateGroupResp *resp = (CreateGroupResp*)body.data();
            emit m_manager->sigCreateGroupResult(resp->result == 1, resp->groupId);
            break;
        }
        case MSG_GROUP_LIST_RESP: {
            if (body.size() < (int)sizeof(int)) break;
            const char *ptr = body.constData();
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
            emit m_manager->sigGroupListReceived(list);
            break;
        }
        case MSG_GROUP_MEMBER_LIST_RESP: {
            if (body.size() < (int)sizeof(int) * 2) break;
            const char *ptr = body.constData();
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
            emit m_manager->sigGroupMemberListReceived(groupId, list);
            break;
        }
        case MSG_GROUP_CHAT_TEXT: {
            if (body.size() < (int)sizeof(GroupChatMessage)) break;

            quint64 messageId = 0;
            int headerOffset = 0;
            if (body.size() >= (int)(sizeof(quint64) + sizeof(GroupChatMessage))) {
                GroupChatMessage candidate;
                memcpy(&candidate, body.constData() + sizeof(quint64), sizeof(candidate));
                if (candidate.groupId > 0) {
                    memcpy(&messageId, body.constData(), sizeof(messageId));
                    headerOffset = sizeof(quint64);
                }
            }

            GroupChatMessage *msg = (GroupChatMessage*)(body.data() + headerOffset);
            int groupId = msg->groupId;
            int senderId = msg->senderId;
            QString senderName = QString::fromUtf8(msg->senderName, safe_strnlen(msg->senderName, 32));
            QByteArray content = body.mid(headerOffset + sizeof(GroupChatMessage));
            
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
                    emit m_manager->sigGroupMsgReceived(groupId, senderId, senderName, failedBody, messageId);
                    break;
                }
                
                QByteArray decrypted = EncryptionManager::instance().xorEncryptDecrypt(messageBody, key);
                
                if (decrypted.isEmpty() && !messageBody.isEmpty()) {
                    LOG_ERROR_FMT("Failed to decrypt group message from group %1", groupId);
                    QByteArray failedBody;
                    failedBody.append(originalSubType);
                    emit m_manager->sigGroupMsgReceived(groupId, senderId, senderName, failedBody, messageId);
                    break;
                }
                
                QByteArray decryptedContent;
                decryptedContent.append(originalSubType);
                decryptedContent.append(decrypted);
                
                emit m_manager->sigGroupMsgReceived(groupId, senderId, senderName, decryptedContent, messageId);
            } else {
                emit m_manager->sigGroupMsgReceived(groupId, senderId, senderName, content, messageId);
            }

            if (messageId > 0) {
                GroupMsgCursorAck ack;
                ack.groupId = groupId;
                ack.messageId = messageId;
                QByteArray packet = makePacket(MSG_GROUP_MSG_DELIVERED_ACK, QByteArray((char*)&ack, sizeof(ack)));
                m_manager->sendRow(packet);
            }
            break;
        }
        case MSG_GROUP_OFFLINE_CHAT_TEXT: {
            if (body.size() < (int)(sizeof(quint64) + sizeof(GroupChatMessage))) break;

            quint64 offlineMsgId = 0;
            memcpy(&offlineMsgId, body.constData(), sizeof(offlineMsgId));
            QByteArray groupBody;
            quint64 legacyMessageId = 0;
            groupBody.append(reinterpret_cast<const char*>(&legacyMessageId), sizeof(legacyMessageId));
            groupBody.append(body.mid(sizeof(offlineMsgId)));

            dispatch(MSG_GROUP_CHAT_TEXT, srcId, groupBody);

            OfflineMsgAck ack;
            ack.offlineMsgId = offlineMsgId;
            QByteArray packet = makePacket(MSG_GROUP_OFFLINE_MSG_ACK, QByteArray((char*)&ack, sizeof(ack)));
            m_manager->sendRow(packet);
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
            emit m_manager->sigGroupChatHistoryReceived(groupId, history);
            break;
        }
        case MSG_LEAVE_GROUP_RESP: {
            if (body.size() < (int)sizeof(LeaveGroupResp)) break;
            LeaveGroupResp *resp = (LeaveGroupResp*)body.data();
            emit m_manager->sigLeaveGroupResponse(resp->result, resp->groupId);
            break;
        }
        case MSG_INVITE_TO_GROUP_NOTIFY: {
            if (body.size() < (int)sizeof(InviteToGroupNotify)) break;
            InviteToGroupNotify *notify = (InviteToGroupNotify*)body.data();
            emit m_manager->sigInviteToGroupNotify(
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
            emit m_manager->sigFileResumeReq(fileId, srcId);
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

            emit m_manager->sigFileResumeResp(fileId, canResume, totalChunks, receivedChunks, bitmap);
            break;
        }
        case MSG_FILE_VERIFY_REQ: {
            if(body.size() < (int)sizeof(FileVerifyReq)){
                break;
            }
            LOG_WARN("Received unsupported file verify request");
            break;
        }
        case MSG_FILE_VERIFY_RESP: {
            if(body.size() < (int)sizeof(FileVerifyResp)){
                break;
            }
            FileVerifyResp *resp = (FileVerifyResp*)body.data();
            QString fileId = QString::fromLatin1(resp->fileId, safe_strnlen(resp->fileId, sizeof(resp->fileId)));
            bool verified = (resp->verified == 1);
            emit m_manager->sigFileVerifyResp(fileId, verified);
            break;
        }
        case MSG_FILE_TRANSFER_CANCEL: {
            if(body.size() < (int)sizeof(FileTransferCancel)){
                break;
            }
            FileTransferCancel cancel;
            memcpy(&cancel, body.constData(), sizeof(cancel));
            QString fileId = QString::fromLatin1(cancel.fileId, safe_strnlen(cancel.fileId, sizeof(cancel.fileId)));
            emit m_manager->sigFileTransferCanceled(fileId, srcId, cancel.reason);
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
