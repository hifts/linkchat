#ifndef NETWORKMANAGER_H
#define NETWORKMANAGER_H

#include "packet.h"
#include "filetransfermanager.h"

#include <QObject>
#include <QTcpSocket>

class NetworkManager : public QObject
{
    Q_OBJECT
public:
    static NetworkManager &instance();

    // 连接服务器
    void connectToServer(const QString &ip,uint16_t port);

    // 发送信息(NetworkManager类帮打包)
    void sendMsg(uint32_t type,const QByteArray &body);

    // 直接发送原始数据包 (用于外部已经打包好的情况)
    void sendRow(const QByteArray& packet);

private:
    explicit NetworkManager(QObject *parent = nullptr);

    QTcpSocket *m_socket;
    QByteArray m_buffer; // 同样需要缓冲区处理粘包

private slots:
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
};

#endif // NETWORKMANAGER_H
