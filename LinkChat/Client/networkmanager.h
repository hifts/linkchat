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
 * @brief 网络管理器
 * 职责:
 * 1. 管理TCP连接
 * 2. 发送和接收数据包
 * 3. 协调心跳管理器和重连管理器
 * 4. 处理粘包和协议解析
*/

class NetworkManager : public QObject
{
    Q_OBJECT
public:
    static NetworkManager &instance();

    // 连接服务器
    void connectToServer(const QString &ip,uint16_t port);

    // 主动断开连接
    void disconnectFromServer();
    // 是否是连接状态
    bool isConnected() const;

    // 发送信息(NetworkManager类帮打包)
    void sendMsg(uint32_t type,const QByteArray &body);

    // 直接发送原始数据包 (用于外部已经打包好的情况)
    void sendRow(const QByteArray& packet);

    HeartbeatManager* getHeartbeatManager() { return m_heartbeatManager; }
    ReconnectManager* getReconnectManager() { return m_reconnectManager; }

    // 设置当前用户ID（用于加密）
    void setCurrentUserId(int userId) { m_currentUserId = userId; }

private:
    explicit NetworkManager(QObject *parent = nullptr);

    // 发送心跳包
    void sendHeartbeat();

    // 处理自动登录
    void handleAutoLogin(const QString &userName,const QString &password);
private slots:
    // 处理与服务器连接成功信号
    void onConnected();

    // 处理与服务器断开信号
    void onDisconnected();

    // 处理连接错误信号
    void onError(QAbstractSocket::SocketError error);

    void onReadyRead();

signals:
    // 注册是否成功信号
    void sigRegisterResult(bool success);

    // 登录是否成功信号
    void sigLoginResult(bool success,int uid,int errorCode = 0);

    void sigFriendListReceived(QList<FriendInfo> list);

    void sigMsgReceived(uint32_t srcId,QByteArray body);

    void sigFriendStatusChanged(int uid, int status);

    void sigSearchUserResult(QList<FriendInfo> list);

    // 收到好友请求
    void sigFriendRequestReceived(int uid,const QString name);

    // 同意添加好友信号
    void sigFriendRequestAccepted();

    // 拒绝添加好友信号
    void sigFriendRequestRejected();

    void sigChatHistoryReceived(int friendId, const QList<QPair<int,QByteArray>>& history);

    // 发送文件请求信息(征求接收方意见)
    void sigFileTransferRequest(const QString &fileId, const QString &fileName, qint64 fileSize, int senderId);

    // 是否接收文件信号
    void sigFileTransferResponse(const QString &fileId,bool accepted);

    // 接收到分片数据
    void receiveChunk(const QString &fileId,int chunkIndex, const QByteArray &data);

    // 创建群聊响应
    void sigCreateGroupResult(bool success, int groupId);

    // 群列表接收
    void sigGroupListReceived(QList<GroupInfo> list);

    // 群成员列表接收
    void sigGroupMemberListReceived(int groupId, QList<GroupMemberInfo> list);

    // 群聊消息接收（含发送者信息）
    void sigGroupMsgReceived(int groupId, int senderId, const QString &senderName, QByteArray body);

    // 群聊历史消息接收
    void sigGroupChatHistoryReceived(int groupId, const QList<std::tuple<int, QString, QByteArray>>& history);

    // 邀请入群通知
    void sigInviteToGroupNotify(int groupId, const QString &groupName, int inviterId, const QString &inviterName);

    // 网络状态信号
    void sigConnectionStateChanged(bool connected);

    // 断点续传信号
    void sigFileResumeResp(const QString &fileId, bool canResume, int totalChunks, int receivedChunks, const QByteArray &bitmap);
    void sigFileResumeReq(const QString &fileId, int senderId);
    void sigFileVerifyResp(const QString &fileId, bool verified);

public:
    // 发送恢复传输请求
    void requestResumeTransfer(const QString &fileId, int friendId);

    // 发送文件校验请求
    void requestFileVerify(const QString &fileId, const QString &fileMD5, int friendId);
    
private:
    QTcpSocket *m_socket;
    QByteArray m_buffer;        // 同样需要缓冲区处理粘包

    // 心跳包和重连管理器
    HeartbeatManager *m_heartbeatManager;
    ReconnectManager *m_reconnectManager;
    
    // 当前用户ID（用于消息加密）
    int m_currentUserId = 0;
};

#endif // NETWORKMANAGER_H
