#ifndef CLIENTSOCKET_H
#define CLIENTSOCKET_H

#include "packet.h"
#include <QTcpSocket>
#include <tuple>

class ClientSocket : public QTcpSocket
{
    Q_OBJECT
    friend class ServerMessageRouter;
public:
    explicit ClientSocket(QObject *parent = nullptr);
    int uid() const;
    void setHeartbeatTimeout(int timeoutMs);

public slots:
    void markLoggedOut();
    void sendPacket(uint32_t type, const QByteArray& body, uint32_t src = 0, uint32_t dest = 0);
    void notifyOfflineToFriends();

private slots:
    void onReadyRead();
    void checkHeartbeatTimeout();

signals:
    void signalMsgReceived(uint32_t msgType, const QByteArray &data);

private:
    QByteArray m_buffer;
    int m_uid = 0;
    QString m_userName;
    bool m_loginPending = false;
    qint64 m_lastActiveMs = 0;
    int m_heartbeatTimeoutMs = 90000;

    void closeForProtocolError(const QString& reason);

    void writePacket(ClientSocket* target, uint32_t type, const QByteArray& body, uint32_t src, uint32_t dest);

    void notifyFriends(int status);

    void pushOfflineMsgs(const QList<QPair<int, QByteArray>> &offlineMsgs);

    void pushFriendRequests(const QList<QPair<int, QString>> &pendingReqs);

    void pushGroupOfflineMsgs(const QList<std::tuple<int, int, QString, QByteArray>>& offlineMsgs);
};

#endif // CLIENTSOCKET_H
