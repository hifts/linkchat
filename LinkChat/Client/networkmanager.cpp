#include "networkmanager.h"
#include "clientmessagerouter.h"
#include "filetransferconstants.h"
#include "logger.h"
#include "packetcodec.h"

#include <cstring>
#include <QTimer>

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

void NetworkManager::sendFilePacket(const QByteArray &packet)
{
    if(packet.isEmpty()){
        return;
    }

    if(m_socket->state() != QAbstractSocket::ConnectedState){
        LOG_WARN("Socket is not connected, cannot send file packet");
        return;
    }

    if(m_fileWriteBuffer.isEmpty()){
        m_fileWriteBuffer.reserve(FILE_TRANSFER_WRITE_BATCH_BYTES + packet.size());
    }
    m_fileWriteBuffer.append(packet);

    if(m_fileWriteBuffer.size() >= FILE_TRANSFER_WRITE_BATCH_BYTES){
        flushFilePackets();
        return;
    }

    if(!m_fileFlushQueued){
        m_fileFlushQueued = true;
        QTimer::singleShot(2, this, &NetworkManager::flushFilePackets);
    }
}

void NetworkManager::flushFilePackets()
{
    m_fileFlushQueued = false;
    if(m_fileWriteBuffer.isEmpty()){
        return;
    }

    if(m_socket->state() == QAbstractSocket::ConnectedState){
        m_socket->write(m_fileWriteBuffer);
    }
    m_fileWriteBuffer.clear();
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

void NetworkManager::requestCancelTransfer(const QString &fileId, int friendId, quint8 reason)
{
    FileTransferCancel req;
    memset(&req, 0, sizeof(req));
    strncpy(req.fileId, fileId.toLatin1().constData(), sizeof(req.fileId) - 1);
    req.reason = reason;

    QByteArray body((char*)&req, sizeof(req));
    QByteArray packet = makePacket(MSG_FILE_TRANSFER_CANCEL, body, 0, friendId);
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

    PacketCodec codec(DEFAULT_MAX_NORMAL_PACKET_LEN, DEFAULT_MAX_FILE_PACKET_LEN);
    ClientMessageRouter router(this);

    while (true) {
        DecodedPacket packet;
        QString error;
        const PacketReadStatus status = codec.takeNextPacket(m_buffer, packet, &error);
        if (status == PacketReadStatus::NeedMoreData) {
            break;
        }
        if (status == PacketReadStatus::ProtocolError) {
            LOG_ERROR_FMT("Invalid packet, resyncing: %1", error);
            continue;
        }

        const uint32_t msgType = packet.header.msg_type;
        router.dispatch(msgType, packet.header.src_id, packet.body);
    }
}


