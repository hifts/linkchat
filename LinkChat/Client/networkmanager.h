#ifndef NETWORKMANAGER_H
#define NETWORKMANAGER_H

#include "packet.h"
#include "heartbeatmanager.h"
#include "reconnectmanager.h"
#include "encryptionmanager.h"

#include <QObject>
#include <QTcpSocket>
#include <tuple>

/**
 * @brief Network manager.
 * Responsibilities:
 * 1. Manage TCP connection
 * 2. Send and receive packets
 * 3. Coordinate heartbeat and reconnect managers
 * 4. Parse packets and handle sticky packets
*/

class NetworkManager : public QObject
{
    Q_OBJECT
    friend class ClientMessageRouter;
public:
    static NetworkManager &instance();

    void connectToServer(const QString &ip,uint16_t port);

    void disconnectFromServer();
    bool isConnected() const;

    void sendMsg(uint32_t type,const QByteArray &body);

    void sendRow(const QByteArray& packet);

    HeartbeatManager* getHeartbeatManager() { return m_heartbeatManager; }
    ReconnectManager* getReconnectManager() { return m_reconnectManager; }

    void setCurrentUserId(int userId) { m_currentUserId = userId; }

private:
    explicit NetworkManager(QObject *parent = nullptr);

    void sendHeartbeat();

    void handleAutoLogin(const QString &userName,const QString &passwordHashBase64);
private slots:
    void onConnected();

    void onDisconnected();

    void onError(QAbstractSocket::SocketError error);

    void onReadyRead();

signals:
    void sigRegisterResult(bool success);

    void sigLoginResult(bool success,int uid,int errorCode = 0);
    void sigLoginSaltReceived(bool success, const QByteArray &saltBase64);

    void sigFriendListReceived(QList<FriendInfo> list);

    void sigMsgReceived(uint32_t srcId,QByteArray body);

    void sigFriendStatusChanged(int uid, int status);

    void sigSearchUserResult(QList<FriendInfo> list);

    void sigFriendRequestReceived(int uid,const QString name);

    void sigFriendRequestAccepted();

    void sigFriendRequestRejected();

    void sigDeleteFriendResponse(int result, int targetId);

    void sigChatHistoryReceived(int friendId, const QList<std::tuple<int, QByteArray, quint64>>& history);

    void sigFileTransferRequest(const QString &fileId, const QString &fileName, qint64 fileSize, int senderId);

    void sigFileTransferResponse(const QString &fileId,bool accepted);

    void receiveChunk(const QString &fileId, int chunkIndex, const QByteArray &data, int senderId);
    void sigFileTransferAck(const QString &fileId, int chunkIndex, int receiverId);

    void sigCreateGroupResult(bool success, int groupId);

    void sigGroupListReceived(QList<GroupInfo> list);

    void sigGroupMemberListReceived(int groupId, QList<GroupMemberInfo> list);

    void sigGroupMsgReceived(int groupId, int senderId, const QString &senderName, QByteArray body, quint64 messageId);

    void sigGroupChatHistoryReceived(int groupId, const QList<std::tuple<int, QString, QByteArray, quint64>>& history);

    void sigLeaveGroupResponse(int result, int groupId);

    void sigInviteToGroupNotify(int groupId, const QString &groupName, int inviterId, const QString &inviterName);

    void sigConnectionStateChanged(bool connected);

    void sigFileResumeResp(const QString &fileId, bool canResume, int totalChunks, int receivedChunks, const QByteArray &bitmap);
    void sigFileResumeReq(const QString &fileId, int senderId);
    void sigFileVerifyResp(const QString &fileId, bool verified);
    void sigFileTransferCanceled(const QString &fileId, int senderId, int reason);

public:
    void requestResumeTransfer(const QString &fileId, int friendId);

    void requestFileVerify(const QString &fileId, const QString &fileMD5, int friendId);

    void requestCancelTransfer(const QString &fileId, int friendId, quint8 reason = 0);
    
private:
    QTcpSocket *m_socket;
    QByteArray m_buffer;

    HeartbeatManager *m_heartbeatManager;
    ReconnectManager *m_reconnectManager;
    
    int m_currentUserId = 0;
};

#endif // NETWORKMANAGER_H
