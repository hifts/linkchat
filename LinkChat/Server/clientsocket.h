#ifndef CLIENTSOCKET_H
#define CLIENTSOCKET_H

#include "packet.h"
#include <QTcpSocket>
#include <tuple>

class ClientSocket : public QTcpSocket
{
    Q_OBJECT
public:
    explicit ClientSocket(QObject *parent = nullptr);

private slots:
    void onReadyRead();

signals:
    void signalMsgReceived(uint32_t msgType, const QByteArray &data);

private:
    QByteArray m_buffer;
    int m_uid = 0;
    QString m_userName;

    void writePacket(ClientSocket* target, int type, const QByteArray& body, int src, int dest);

    void notifyFriends(int status);

    void pushOfflineMsgs(const QList<QPair<int, QByteArray>> &offlineMsgs);

    void pushFriendRequests(const QList<QPair<int, QString>> &pendingReqs);

    void pushGroupOfflineMsgs(const QList<std::tuple<int, int, QString, QByteArray>>& offlineMsgs);
};

#endif // CLIENTSOCKET_H
