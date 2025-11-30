#ifndef CLIENTSOCKET_H
#define CLIENTSOCKET_H

#include "packet.h"
#include <QTcpSocket>

class ClientSocket : public QTcpSocket
{
    Q_OBJECT
public:
    explicit ClientSocket(QObject *parent = nullptr);

private slots:
    // 读取数据
    void onReadyRead();

signals:
    // 当解析出一个完整的包时，发送信号给 Server 主逻辑
    void signalMsgReceived(uint32_t msgType, const QByteArray &data);

private:
    QByteArray m_buffer;
    int m_uid = 0;
    QString m_userName;

    // 参数：目标Socket指针，消息类型，包体内容，发送者ID，接收者ID
    void writePacket(ClientSocket* target, int type, const QByteArray& body, int src, int dest);

    // 通知我的好友我的状态变了
    void notifyFriends(int status);

    // 推送离线消息
    void pushOfflineMsgs(const QList<QPair<int, QByteArray>> &offlineMsgs);

    // 推送好友请求
    void pushFriendRequests(const QList<QPair<int, QString>> &pendingReqs);
};

#endif // CLIENTSOCKET_H
