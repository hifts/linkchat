#include "networkmanager.h"
#include "logger.h"

#include <QDataStream>

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

    // 连接socket信号
    connect(m_socket,&QTcpSocket::connected,this,&NetworkManager::onConnected);
    connect(m_socket,&QTcpSocket::disconnected,this,&NetworkManager::onDisconnected);
    connect(m_socket, &QTcpSocket::errorOccurred, this,&NetworkManager::onError);

    // 关联接收数据信号
    connect(m_socket,&QTcpSocket::readyRead,this,&NetworkManager::onReadyRead);

    // 连接心跳管理器信号
    connect(m_heartbeatManager,&HeartbeatManager::needSendHeartbeat,this,&NetworkManager::sendHeartbeat);
    connect(m_heartbeatManager,&HeartbeatManager::heartbeatTimeout,
            this,[this](int missedCount){
                LOG_INFO_FMT("心跳超时,错过 %1 次, 主动断开连接",missedCount);
                m_socket->abort();
            });

    // 连接重连管理器信号
    connect(m_reconnectManager,&ReconnectManager::needReconnect,
            this,[this](const QString &ip,uint16_t port){
                LOG_INFO_FMT("执行重连 %1:%2",ip,port);
                m_socket->abort();
                m_socket->connectToHost(ip,port);
            });

    connect(m_reconnectManager,&ReconnectManager::needAutoLogin,this,&NetworkManager::handleAutoLogin);
}

void NetworkManager::connectToServer(const QString &ip, uint16_t port)
{

    LOG_INFO(QString("连接服务器 %1:%2").arg(ip).arg(port));

    // 保存服务器信息到重连管理器中
    m_reconnectManager->setServerInfo(ip,port);

    // 设置状态为连接中
    m_reconnectManager->setConnectionState(ReconnectManager::Connecting);

    m_socket->abort();
    m_socket->connectToHost(ip,port);
}

void NetworkManager::disconnectFromServer()
{
    LOG_INFO("主动断开连接");

    // 停止心跳
    m_heartbeatManager->stop();

    // 禁用自动重连
    m_reconnectManager->setAutoConnect(false);
    m_reconnectManager->stopReconnect();

    // 清除登录信息
    m_reconnectManager->clearLoginInfo();

    m_socket->disconnectFromHost();
}

bool NetworkManager::isConnected() const
{
    return m_socket->state() == QAbstractSocket::ConnectedState;
}

void NetworkManager::sendMsg(uint32_t type, const QByteArray &body)
{
    QByteArray data = makePacket(type,body);
    m_socket->write(data);
}

void NetworkManager::sendRow(const QByteArray &packet)
{
    if(packet.isEmpty()){
        return;
    }

    // 处于连接状态才发送
    if(m_socket->state() == QAbstractSocket::ConnectedState){
        m_socket->write(packet);
    }else{
        LOG_WARN("Socket 未连接，无法发送数据");
    }
}

void NetworkManager::requestResumeTransfer(const QString &fileId, int friendId)
{
    FileResumeReq req;
    memset(&req, 0, sizeof(req));
    strncpy(req.fileId, fileId.toUtf8().constData(), 63);

    QByteArray body((char*)&req, sizeof(req));
    QByteArray packet = makePacket(MSG_FILE_RESUME_REQ, body, 0, friendId);
    sendRow(packet);

    LOG_INFO_FMT("Sent file resume request: %1 to %2", fileId, friendId);
}

void NetworkManager::requestFileVerify(const QString &fileId, const QString &fileMD5, int friendId)
{
    FileVerifyReq req;
    memset(&req, 0, sizeof(req));
    strncpy(req.fileId, fileId.toUtf8().constData(), 63);
    strncpy(req.fileMD5, fileMD5.toUtf8().constData(), 32);

    QByteArray body((char*)&req, sizeof(req));
    QByteArray packet = makePacket(MSG_FILE_VERIFY_REQ, body, 0, friendId);
    sendRow(packet);

    LOG_INFO_FMT("Sent file verify request: %1", fileId);
}

void NetworkManager::sendHeartbeat()
{
    if (!isConnected()) {
        LOG_WARN("与服务器连接已断开,无法发送心跳");
        return;
    }

    HeartbeatPacket hb;
    hb.timestamp = QDateTime::currentMSecsSinceEpoch();

    // 发送心跳包
    sendMsg(MSG_HEARTBEAT_REQ,QByteArray((char*)&hb,sizeof(HeartbeatPacket)));
    LOG_INFO("发送心跳包");
}

void NetworkManager::handleAutoLogin(const QString &userName, const QString &password)
{
    LOG_INFO_FMT("执行自动登录(用户名:%1)",userName);

    LoginReq req;
    memset(&req,0,sizeof(LoginReq));
    strncpy(req.userName,userName.toUtf8().constData(),31);
    strncpy(req.password,password.toUtf8().constData(),31);

    // 发送登录请求
    sendMsg(MSG_LOGIN_REQ,QByteArray((char*)&req,sizeof(LoginReq)));
}

void NetworkManager::onConnected()
{
    LOG_INFO("已连接到服务器");

    // 清空缓冲区
    m_buffer.clear();

    // 跟新重连管理器连接状态(已连接)
    m_reconnectManager->setConnectionState(ReconnectManager::Connected);

    // 启动心跳
    m_heartbeatManager->start();

    emit sigConnectionStateChanged(true);
}

void NetworkManager::onDisconnected()
{
    LOG_WARN("与服务器断开连接");

    // 停止心跳
    m_heartbeatManager->stop();

    m_reconnectManager->setConnectionState(ReconnectManager::Disconnected);

    // 发出连接状态改变信号
    emit sigConnectionStateChanged(false);
}

void NetworkManager::onError(QAbstractSocket::SocketError error)
{
    Q_UNUSED(error);
    LOG_ERROR(QString("NetworkManager: 网络错误: %1").arg(m_socket->errorString()));
    m_reconnectManager->setConnectionState(ReconnectManager::Disconnected);
    
}

void NetworkManager::onReadyRead()
{
    m_buffer.append(m_socket->readAll());

    while(m_buffer.size() >= (int)sizeof(PDUHeader)){
        PDUHeader *header = (PDUHeader*)m_buffer.data();
        uint32_t totalLen = header->total_len;

        if(m_buffer.size() < (int)totalLen){
            break;
        }

        // 读取信息主体部分
        QByteArray body = m_buffer.mid(sizeof(PDUHeader),totalLen - sizeof(PDUHeader));
        uint32_t type = header->msg_type;

        switch (type) {
        case MSG_HEARTBEAT_RESP:{
            // 收到心跳响应
            m_heartbeatManager->onHeartbeatReceived();
            LOG_DEBUG("收到心跳响应");
            break;
        }
        case MSG_REGISTER_RESP:{
            LoginResp *resp = (LoginResp*)body.data();
            emit sigRegisterResult(resp->result == 1);
            break;
        }
        case MSG_LOGIN_RESP:{
            LoginResp *resp = (LoginResp*)body.data();
            int result = resp->result;
            bool ok = (result == 1);
            int uid = resp->userId;

            int errorCode = 0;
            if(result == 2){
                errorCode = 2;      // 已在其他设备登录
            }else if(result == 0){
                errorCode = 1;      // 用户名或密码错误
            }

            emit sigLoginResult(ok,uid,errorCode);
            break;
        }
        case MSG_FRIEND_LIST_RESP:{
            // 解析好友列表包
            char *ptr = body.data();
            int count = 0;
            // 获取好友数量
            memcpy(&count,ptr,sizeof(int));
            ptr += sizeof(int);

            // 获取每一个好友包
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
            emit sigMsgReceived(header->src_id,body);
            break;
        }
        case MSG_CHAT_HISTORY_RESP:{
            QDataStream in(body);
            in.setByteOrder(QDataStream::LittleEndian);

            quint32 count;
            in >> count;

            QList<QPair<int,QByteArray>> history;
            for (quint32 i = 0; i < count; ++i) {
                quint32 senderId, len;
                in >> senderId >> len;
                QByteArray content = body.mid(in.device()->pos(), len);
                in.device()->seek(in.device()->pos() + len);
                history.append(qMakePair((int)senderId, content));
            }
            emit sigChatHistoryReceived(header->src_id, history);
            break;
        }
        case MSG_FRIEND_STATUS_NOTIFY:{
            FriendStatusChange* notify = (FriendStatusChange*)body.data();
            emit sigFriendStatusChanged(notify->uid, notify->status);
            break;
        }
        case MSG_SEARCH_USER_RESP:{
            char *ptr = body.data();
            int count = 0;

            memcpy(&count,ptr,sizeof(int));
            ptr += sizeof(int);

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
            AddFriendNotify *notify = (AddFriendNotify*)body.data();
            emit sigFriendRequestReceived(notify->requesterId,QString::fromUtf8(notify->requesterName));
            break;
        }
        case MSG_ADD_FRIEND_RESULT:{
            AddFriendResp *resp = (AddFriendResp*)body.data();
            // int friendId = resp->requesterId;
            bool accepted = resp->accepted;

            if(accepted){
                emit sigFriendRequestAccepted();
            }else{
                emit sigFriendRequestRejected();
            }
            break;
        }
        case MSG_FILE_TRANSFER_REQ:{
            FileTransferReq *req = (FileTransferReq*)body.data();
            QString msg = QString("Received file transfer request:"
                                  "fileId=%1"
                                  "fileName=%2"
                                  "fileSize=%3"
                                  "from=%4")
                              .arg(QString::fromUtf8(req->fileId),QString::fromUtf8(req->fileName))
                              .arg(req->fileSize,header->src_id);
            LOG_INFO(msg);
            emit sigFileTransferRequest(
                QString::fromUtf8(req->fileId),
                QString::fromUtf8(req->fileName),
                req->fileSize,
                header->src_id
                );
            break;
        }
        case MSG_FILE_TRANSFER_RESP:{
            if(body.size() < (int)sizeof(FileTransferResp)){
                LOG_WARN("Invalid FileTransferResp packet size");
                break;
            }

            FileTransferResp *resp = (FileTransferResp*)body.data();
            QString fileId = QString::fromUtf8(resp->fileId);
            bool accepted = (resp->accepted == 1);

            LOG_INFO_FMT("Received file transfer repnese:FileID = %1,Accepted = %2",fileId,accepted);

            emit sigFileTransferResponse(fileId,accepted);
            break;
        }
        case MSG_FILE_CHUNK:{
            // 解析文件分片
            if (body.size() < (int)sizeof(FileChunk)) {
                LOG_WARN("Invalid FileChunk packet size");
                break;
            }

            FileChunk *chunk = (FileChunk*)body.data();
            QString fileId = QString::fromUtf8(chunk->fileId);
            int chunkIndex = chunk->chunkIndex;
            int chunkSize = chunk->chunkSize;

            // 实际的文件数据
            QByteArray chunkData = body.mid(sizeof(FileChunk),chunkSize);

            // 传来的数据分片大小不够实际大小时
            if(chunkData.size() != chunkSize){
                if (chunkData.size() != chunkSize) {
                    LOG_WARN_FMT("Chunk data size mismatch.Expected:%1,Got:%2",chunkSize,chunkData.size());
                    break;
                }
            }

            // 发送接收分片数据信号
            emit receiveChunk(fileId,chunkIndex,chunkData);
            break;
        }
        case MSG_CREATE_GROUP_RESP: {
            CreateGroupResp *resp = (CreateGroupResp*)body.data();
            emit sigCreateGroupResult(resp->result == 1, resp->groupId);
            break;
        }
        case MSG_GROUP_LIST_RESP: {
            char *ptr = body.data();
            int count = 0;
            memcpy(&count, ptr, sizeof(int));
            ptr += sizeof(int);

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
            char *ptr = body.data();
            int groupId = 0;
            int count = 0;
            memcpy(&groupId, ptr, sizeof(int));
            ptr += sizeof(int);
            memcpy(&count, ptr, sizeof(int));
            ptr += sizeof(int);

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
            QString senderName = QString::fromUtf8(msg->senderName);
            QByteArray content = body.mid(sizeof(GroupChatMessage));

            emit sigGroupMsgReceived(groupId, senderId, senderName, content);
            break;
        }
        case MSG_GROUP_CHAT_HISTORY_RESP: {
            QDataStream in(body);
            in.setByteOrder(QDataStream::LittleEndian);

            quint32 groupId, count;
            in >> groupId >> count;

            QList<std::tuple<int, QString, QByteArray>> history;
            for (quint32 i = 0; i < count; ++i) {
                quint32 senderId, nameLen, contentLen;
                in >> senderId >> nameLen;

                QByteArray nameBytes(nameLen, '\0');
                in.readRawData(nameBytes.data(), nameLen);
                QString senderName = QString::fromUtf8(nameBytes);

                in >> contentLen;
                QByteArray content(contentLen, '\0');
                in.readRawData(content.data(), contentLen);

                history.append(std::make_tuple((int)senderId, senderName, content));
            }
            emit sigGroupChatHistoryReceived(groupId, history);
            break;
        }
        case MSG_INVITE_TO_GROUP_NOTIFY: {
            InviteToGroupNotify *notify = (InviteToGroupNotify*)body.data();
            emit sigInviteToGroupNotify(
                notify->groupId,
                QString::fromUtf8(notify->groupName),
                notify->inviterId,
                QString::fromUtf8(notify->inviterName)
                );
            break;
        }
        case MSG_FILE_RESUME_REQ: {
            // 收到恢复传输请求（作为接收方）
            if(body.size() < (int)sizeof(FileResumeReq)){
                LOG_WARN("Invalid FileResumeReq packet size");
                break;
            }
            FileResumeReq *req = (FileResumeReq*)body.data();
            QString fileId = QString::fromUtf8(req->fileId);
            LOG_INFO_FMT("Received file resume request: %1 from %2", fileId, header->src_id);
            emit sigFileResumeReq(fileId, header->src_id);
            break;
        }
        case MSG_FILE_RESUME_RESP: {
            // 收到恢复传输响应（作为发送方）
            if(body.size() < (int)sizeof(FileResumeResp)){
                LOG_WARN("Invalid FileResumeResp packet size");
                break;
            }
            FileResumeResp *resp = (FileResumeResp*)body.data();
            QString fileId = QString::fromUtf8(resp->fileId);
            bool canResume = (resp->canResume == 1);
            int totalChunks = resp->totalChunks;
            int receivedChunks = resp->receivedChunks;

            // 解析已接收分片位图
            QByteArray bitmap;
            if(canResume && totalChunks > 0){
                int bitmapSize = (totalChunks + 7) / 8;
                bitmap = body.mid(sizeof(FileResumeResp), bitmapSize);
            }

            LOG_INFO(QString("Received file resume response: %1, canResume=%2, received=%3/%4")
                         .arg(fileId).arg(canResume).arg(receivedChunks).arg(totalChunks));
            emit sigFileResumeResp(fileId, canResume, totalChunks, receivedChunks, bitmap);
            break;
        }
        case MSG_FILE_VERIFY_REQ: {
            // 收到文件校验请求（作为接收方）
            if(body.size() < (int)sizeof(FileVerifyReq)){
                LOG_WARN("Invalid FileVerifyReq packet size");
                break;
            }
            // 文件校验由FileReceiver处理，这里只转发信号
            FileVerifyReq *req = (FileVerifyReq*)body.data();
            QString fileId = QString::fromUtf8(req->fileId);
            QString fileMD5 = QString::fromUtf8(req->fileMD5);
            LOG_INFO_FMT("Received file verify request: %1, MD5=%2", fileId, fileMD5);
            // TODO: 实现文件校验逻辑
            break;
        }
        case MSG_FILE_VERIFY_RESP: {
            // 收到文件校验响应（作为发送方）
            if(body.size() < (int)sizeof(FileVerifyResp)){
                LOG_WARN("Invalid FileVerifyResp packet size");
                break;
            }
            FileVerifyResp *resp = (FileVerifyResp*)body.data();
            QString fileId = QString::fromUtf8(resp->fileId);
            bool verified = (resp->verified == 1);
            LOG_INFO_FMT("Received file verify response: %1, verified=%2", fileId, verified);
            emit sigFileVerifyResp(fileId, verified);
            break;
        }
        default:
            break;
        }

        m_buffer = m_buffer.right(m_buffer.size() - totalLen);
    }

}
