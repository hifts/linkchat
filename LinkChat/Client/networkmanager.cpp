#include "networkmanager.h"
#include "filereceiver.h"

#include <QDataStream>

NetworkManager &NetworkManager::instance()
{
    static NetworkManager instance;
    return instance;
}

void NetworkManager::connectToServer(const QString &ip, uint16_t port)
{
    m_socket->abort();
    m_socket->connectToHost(ip,port);
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
        qDebug()<<"[Network] Error: Socket not connected. Cannot send raw data.";
    }
}

NetworkManager::NetworkManager(QObject *parent)
    : QObject{parent}
{
    m_socket = new QTcpSocket(this);

    connect(m_socket,&QTcpSocket::connected,this,[=](){
        qDebug()<<"Connect to Server";
    });

    // 关联接收数据信号
    connect(m_socket,&QTcpSocket::readyRead,this,&NetworkManager::onReadyRead);
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
            qDebug() << "[Network] Received file transfer request:"
                     << "fileId=" << QString::fromUtf8(req->fileId)
                     << "fileName=" << QString::fromUtf8(req->fileName)
                     << "fileSize=" << req->fileSize
                     << "from=" << header->src_id;
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
                qWarning() << "[Network] Invalid FileTransferResp packet size";
                break;
            }

            FileTransferResp *resp = (FileTransferResp*)body.data();
            QString fileId = QString::fromUtf8(resp->fileId);
            bool accepted = (resp->accepted == 1);

            qDebug() << "[Network] Received file transfer response:"
                     << "FileID:" << fileId
                     << "Accepted:" << accepted;

            emit sigFileTransferResponse(fileId,accepted);
            break;
        }
        case MSG_FILE_CHUNK:{
            // 解析文件分片
            if (body.size() < (int)sizeof(FileChunk)) {
                qWarning() << "[Network] Invalid FileChunk packet size";
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
                    qWarning() << "[Network] Chunk data size mismatch. Expected:"
                               << chunkSize << "Got:" << chunkData.size();
                    break;
                }
            }

            // 发送接收分片数据信号
            emit receiveChunk(fileId,chunkIndex,chunkData);
            break;
        }
        default:
            break;
        }

        m_buffer = m_buffer.right(m_buffer.size() - totalLen);
    }

}
