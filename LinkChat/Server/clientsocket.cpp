#include "clientsocket.h"
#include "dbmanager.h"
#include "dbworkerpool.h"
#include "packetcodec.h"
#include "servermessagerouter.h"
#include "logger.h"
#include "serverstats.h"
#include "tcpserver.h"

#include <QDateTime>
#include <QMetaObject>
#include <QSharedPointer>
#include <QTimer>
#include <cstring>

Q_DECLARE_METATYPE(QSharedPointer<QList<int>>)

namespace {
constexpr uint32_t MAX_NORMAL_PACKET_LEN = 1u * 1024u * 1024u;
constexpr uint32_t MAX_FILE_PACKET_LEN = 1u * 1024u * 1024u;
constexpr int MAX_SOCKET_BUFFER_LEN = 2 * 1024 * 1024;
constexpr qint64 MAX_PENDING_WRITE_BYTES = 4 * 1024 * 1024;
}

ClientSocket::ClientSocket(QObject *parent)
    : QTcpSocket{parent}
{
    m_lastActiveMs = QDateTime::currentMSecsSinceEpoch();
    connect(this, &QTcpSocket::readyRead, this, &ClientSocket::onReadyRead);

    connect(this, &QTcpSocket::disconnected, this, [this]() {
        const int disconnectedUid = m_uid;
        if (disconnectedUid != 0) {
            notifyFriends(0);
            TcpServer::instance().userLogout(disconnectedUid, this);
        }
        TcpServer::instance().socketDisconnected(this);
    });

    auto* heartbeatTimer = new QTimer(this);
    heartbeatTimer->setInterval(10000);
    connect(heartbeatTimer, &QTimer::timeout, this, &ClientSocket::checkHeartbeatTimeout);
    heartbeatTimer->start();
}

int ClientSocket::uid() const
{
    return m_uid;
}

void ClientSocket::setHeartbeatTimeout(int timeoutMs)
{
    m_heartbeatTimeoutMs = qMax(1000, timeoutMs);
}

void ClientSocket::markLoggedOut()
{
    m_uid = 0;
    m_userName.clear();
}

void ClientSocket::notifyOfflineToFriends()
{
    notifyFriends(0);
}

void ClientSocket::onReadyRead()
{
    m_lastActiveMs = QDateTime::currentMSecsSinceEpoch();
    m_buffer.append(readAll());
    if (m_buffer.size() > MAX_SOCKET_BUFFER_LEN) {
        closeForProtocolError(QString("receive buffer too large: %1").arg(m_buffer.size()));
        return;
    }

    PacketCodec codec(MAX_NORMAL_PACKET_LEN, MAX_FILE_PACKET_LEN);
    ServerMessageRouter router(this);

    while (true) {
        DecodedPacket packet;
        QString error;
        const PacketReadStatus status = codec.takeNextPacket(m_buffer, packet, &error);
        if (status == PacketReadStatus::NeedMoreData) {
            break;
        }
        if (status == PacketReadStatus::ProtocolError) {
            closeForProtocolError(error);
            return;
        }

        ServerStats::instance().packetReceived();
        ServerStats::instance().messageHandled(packet.header.msg_type);
        router.dispatch(packet.header.msg_type, packet.header.src_id, packet.header.dest_id, packet.body);
    }
}


void ClientSocket::sendPacket(uint32_t type, const QByteArray& body, uint32_t src, uint32_t dest)
{
    if (state() != QAbstractSocket::ConnectedState) {
        return;
    }
    if (bytesToWrite() > MAX_PENDING_WRITE_BYTES) {
        LOG_WARN(QString("Closing slow client uid=%1 pending_write=%2").arg(m_uid).arg(bytesToWrite()));
        abort();
        return;
    }
    write(makePacket(type, body, src, dest));
    ServerStats::instance().packetSent();
}

void ClientSocket::checkHeartbeatTimeout()
{
    if (state() != QAbstractSocket::ConnectedState) {
        return;
    }

    const qint64 idleMs = QDateTime::currentMSecsSinceEpoch() - m_lastActiveMs;
    if (idleMs > m_heartbeatTimeoutMs) {
        LOG_WARN(QString("Closing idle client uid=%1 idle_ms=%2 timeout_ms=%3")
                     .arg(m_uid)
                     .arg(idleMs)
                     .arg(m_heartbeatTimeoutMs));
        abort();
    }
}

void ClientSocket::closeForProtocolError(const QString& reason)
{
    LOG_WARN(QString("Closing client for protocol error: %1").arg(reason));
    abort();
}

void ClientSocket::writePacket(ClientSocket *target, uint32_t type, const QByteArray &body, uint32_t src, uint32_t dest)
{
    if (target && target->state() == QAbstractSocket::ConnectedState) {
        QMetaObject::invokeMethod(target, "sendPacket",
                                  Qt::QueuedConnection,
                                  Q_ARG(uint32_t, type),
                                  Q_ARG(QByteArray, body),
                                  Q_ARG(uint32_t, src),
                                  Q_ARG(uint32_t, dest));
    } else {
        LOG_ERROR("Target socket is invalid or disconnected. Cannot forward message.");
    }
}

void ClientSocket::notifyFriends(int status)
{
    const int currentUid = m_uid;
    if (currentUid == 0) {
        return;
    }
    DbWorkerPool::instance().enqueue(
        [currentUid](DBManager& db) {
            return QVariant::fromValue(QSharedPointer<QList<int>>::create(db.getFriendIds(currentUid)));
        },
        this,
        [this, currentUid, status](const QVariant& result) {
            const auto friendIdsPtr = result.value<QSharedPointer<QList<int>>>();
            const QList<int> friendIds = friendIdsPtr ? *friendIdsPtr : QList<int>();
            for (int friendId : friendIds) {
                ClientSocket* friendSocket = TcpServer::instance().getUserSocket(friendId);
                if (friendSocket) {
                    FriendStatusChange notify;
                    notify.uid = currentUid;
                    notify.status = status;
                    QByteArray data((char*)&notify, sizeof(FriendStatusChange));
                    TcpServer::instance().sendToUser(friendId, MSG_FRIEND_STATUS_NOTIFY, data, 0, friendId);
                }
            }
        });
}

void ClientSocket::pushOfflineMsgs(const QList<QPair<int, QByteArray> > &offlineMsgs)
{
    for (const auto &msgPair : offlineMsgs) {
        sendPacket(MSG_CHAT_TEXT, msgPair.second, msgPair.first, m_uid);
    }
}

void ClientSocket::pushFriendRequests(const QList<QPair<int, QString> > &pendingReqs)
{
    for (const auto &req : pendingReqs) {
        AddFriendNotify notify;
        notify.requesterId = req.first;
        strncpy(notify.requesterName, req.second.toUtf8().constData(), 31);
        notify.requesterName[31] = '\0';
        sendPacket(MSG_ADD_FRIEND_NOTIFY, QByteArray((char*)&notify, sizeof(AddFriendNotify)), 0, m_uid);
    }
}

void ClientSocket::pushGroupOfflineMsgs(const QList<std::tuple<int, int, QString, QByteArray>>& offlineMsgs)
{
    for (const auto &msg : offlineMsgs) {
        GroupChatMessage header;
        header.groupId = std::get<0>(msg);
        header.senderId = std::get<1>(msg);
        strncpy(header.senderName, std::get<2>(msg).toUtf8().constData(), 31);
        header.senderName[31] = '\0';

        QByteArray body;
        body.append((char*)&header, sizeof(GroupChatMessage));
        body.append(std::get<3>(msg));
        sendPacket(MSG_GROUP_CHAT_TEXT, body, header.senderId, m_uid);
    }
}
